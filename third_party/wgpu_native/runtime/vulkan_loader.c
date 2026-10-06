/* SPDX-License-Identifier: MIT */
#include "win64_bridge.h"
#include "vulkan_loader.h"
#include <cosmo.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct VulkanSignature {
  const char *name;
  const char *arguments;
};
#include "vulkan_signatures.inc"

struct Dispatch {
  struct Dispatch *next;
  const struct VulkanSignature *signature;
  void *native;
  void *call;
};

/* Vulkan loaders and their generated call adapters stay resident until process
   exit. This preserves the lifetime of cloned ash Entry/Instance/Device tables.
   There is a separate adapter for each native address, including multiple
   instances, devices, and loaders; no single global device dispatch pointer. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct Dispatch *dispatches;
static enum { PROVIDER_NATIVE, PROVIDER_EMBEDDED } selected_provider;
static void *embedded_get_instance_proc_addr;
static char *embedded_name;
static unsigned long native_open_count;

static void *adapt(void *, const char *);

static int trace_enabled(void) {
  const char *value = getenv("COSMO_WGPU_TRACE");
  return value && *value && strcmp(value, "0");
}

int cosmo_wgpu_vulkan_register_embedded(const char *name, void *entry) {
  if (!name || !*name || !entry) {
    errno = EINVAL;
    return -1;
  }
  pthread_mutex_lock(&lock);
  if (embedded_get_instance_proc_addr) {
    int identical = embedded_get_instance_proc_addr == entry &&
                    !strcmp(embedded_name, name);
    pthread_mutex_unlock(&lock);
    if (!identical) errno = EEXIST;
    return identical ? 0 : -1;
  }
  char *copy = strdup(name);
  if (!copy) {
    pthread_mutex_unlock(&lock);
    return -1;
  }
  embedded_name = copy;
  embedded_get_instance_proc_addr = entry;
  pthread_mutex_unlock(&lock);
  return 0;
}

int cosmo_wgpu_vulkan_select(const char *provider) {
  if (!provider || (strcmp(provider, "native") && strcmp(provider, "embedded"))) {
    errno = EINVAL;
    return -1;
  }
  pthread_mutex_lock(&lock);
  selected_provider = !strcmp(provider, "embedded") ? PROVIDER_EMBEDDED
                                                     : PROVIDER_NATIVE;
  int available = selected_provider == PROVIDER_NATIVE ||
                  embedded_get_instance_proc_addr != 0;
  pthread_mutex_unlock(&lock);
  if (!available) errno = ENODEV;
  return available ? 0 : -1;
}

const char *cosmo_wgpu_vulkan_selected_provider(void) {
  pthread_mutex_lock(&lock);
  int embedded = selected_provider == PROVIDER_EMBEDDED;
  pthread_mutex_unlock(&lock);
  return embedded ? "embedded" : "native";
}

const char *cosmo_wgpu_vulkan_embedded_name(void) {
  pthread_mutex_lock(&lock);
  const char *name = embedded_name;
  pthread_mutex_unlock(&lock);
  return name;
}

unsigned long cosmo_wgpu_vulkan_native_open_count(void) {
  pthread_mutex_lock(&lock);
  unsigned long count = native_open_count;
  pthread_mutex_unlock(&lock);
  return count;
}

/* A directly embedded ICD has no external loader layers. Mesa's ICD-only
   enumeration reports zero on the count query but ERROR_LAYER_NOT_PRESENT on
   the second call with a non-null output pointer. Ash makes both calls even
   for count zero. Present the layer-free loader contract on both calls. */
static int32_t embedded_enumerate_layers(uint32_t *count, void *properties) {
  (void)properties;
  if (!count) return -3; /* VK_ERROR_INITIALIZATION_FAILED */
  *count = 0;
  return 0; /* VK_SUCCESS */
}

static void *embedded_entry(uint64_t instance, const char *name) {
  typedef void *(*Resolver)(uint64_t, const char *);
  if (!name) return 0;
  if (!strcmp(name, "vkGetInstanceProcAddr")) return embedded_entry;
  if (!strcmp(name, "vkEnumerateInstanceLayerProperties"))
    return embedded_enumerate_layers;
  pthread_mutex_lock(&lock);
  void *entry = embedded_get_instance_proc_addr;
  pthread_mutex_unlock(&lock);
  /* Registration cannot be replaced. In particular, provider selection for a
     later native instance must not retarget this existing embedded entry.
     Both this call and all returned pointers use the application ABI/TLS. */
  return entry ? ((Resolver)entry)(instance, name) : 0;
}

static const struct VulkanSignature *signature(const char *name) {
  size_t lo = 0, hi = sizeof(kVulkanSignatures) / sizeof(*kVulkanSignatures);
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    int order = strcmp(name, kVulkanSignatures[mid].name);
    if (!order) return &kVulkanSignatures[mid];
    if (order < 0) hi = mid;
    else lo = mid + 1;
  }
  return 0;
}

/* This runs back on the application's ABI/TLS after the native lookup returns.
   Both Vulkan procedure resolvers return native pointers; adapting only their
   exported symbols would leave every other Vulkan call with the wrong ABI. */
static void *resolve(uint64_t handle, const char *name, void *procedure) {
  typedef void *(*Resolver)(uint64_t, const char *);
  void *address = ((Resolver)procedure)(handle, name);
  return address ? adapt(address, name) : 0;
}

static void *adapt(void *native, const char *name) {
  const struct VulkanSignature *sig = signature(name);
  if (!sig) {
    fprintf(stderr, "WebGPU: refusing unaudited Vulkan entry point %s\n", name);
    return 0;
  }
  pthread_mutex_lock(&lock);
  for (struct Dispatch *d = dispatches; d; d = d->next) {
    if (d->native == native && d->signature == sig) {
      pthread_mutex_unlock(&lock);
      return d->call;
    }
  }
  void *call = IsWindows()
                   ? cosmo_wgpu_win64_bridge(native, sig->arguments)
                   : cosmo_dltramp(native);
  if (call && (!strcmp(name, "vkGetInstanceProcAddr") ||
               !strcmp(name, "vkGetDeviceProcAddr"))) {
    call = cosmo_wgpu_bind_resolver(resolve, call);
  }
  struct Dispatch *d = call ? malloc(sizeof(*d)) : 0;
  if (d) {
    *d = (struct Dispatch){dispatches, sig, native, call};
    dispatches = d;
  } else {
    fprintf(stderr, "WebGPU: cannot allocate Vulkan call adapter for %s\n", name);
    call = 0;
  }
  pthread_mutex_unlock(&lock);
  return call;
}

/* Ash's experimental Cosmopolitan entry loader calls this function. An empty
   path chooses the library at runtime, independent of Rust's Linux cfg. */
void *cosmo_wgpu_vulkan_entry(const char *path) {
  pthread_mutex_lock(&lock);
  int embedded = selected_provider == PROVIDER_EMBEDDED;
  void *entry = embedded_get_instance_proc_addr;
  const char *name = embedded_name;
  pthread_mutex_unlock(&lock);
  if (embedded) {
    if (path && *path) {
      fprintf(stderr, "WebGPU: an embedded Vulkan instance cannot use a native library path.\n");
      errno = EINVAL;
      return 0;
    }
    if (!entry) {
      fprintf(stderr, "WebGPU: embedded Vulkan was requested but no implementation is linked and registered.\n");
      errno = ENODEV;
      return 0;
    }
    if (trace_enabled())
      fprintf(stderr, "WebGPU: Vulkan provider=embedded; implementation=%s; native loader bypassed\n", name);
    return embedded_entry;
  }
  if (!IsLinux() && !IsWindows()) {
    fprintf(stderr, "WebGPU: this experimental port supports Linux/Windows x64.\n");
    return 0;
  }
  if (!path || !*path) path = IsWindows() ? "vulkan-1.dll" : "libvulkan.so.1";
  if (trace_enabled())
    fprintf(stderr, "WebGPU: Vulkan provider=native; library=%s\n", path);
  pthread_mutex_lock(&lock);
  ++native_open_count;
  pthread_mutex_unlock(&lock);
  void *library = cosmo_dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    const char *error = cosmo_dlerror();
    fprintf(stderr, "WebGPU: cannot open %s: %s\n", path,
            error ? error : "unknown loader error");
    return 0;
  }
  entry = cosmo_dlsym(library, "vkGetInstanceProcAddr");
  if (!entry) {
    fprintf(stderr, "WebGPU: %s has no vkGetInstanceProcAddr\n", path);
    cosmo_dlclose(library);
    return 0;
  }
  /* Keep the successful library handle alive for all dispatch tables. */
  return adapt(entry, "vkGetInstanceProcAddr");
}
