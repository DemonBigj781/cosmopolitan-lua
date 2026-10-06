/* SPDX-License-Identifier: MIT */
#include "win64_bridge.h"
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

static void *adapt(void *, const char *);

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
  if (!IsLinux() && !IsWindows()) {
    fprintf(stderr, "WebGPU: this experimental port supports Linux/Windows x64.\n");
    return 0;
  }
  if (!path || !*path) path = IsWindows() ? "vulkan-1.dll" : "libvulkan.so.1";
  void *library = cosmo_dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    const char *error = cosmo_dlerror();
    fprintf(stderr, "WebGPU: cannot open %s: %s\n", path,
            error ? error : "unknown loader error");
    return 0;
  }
  void *entry = cosmo_dlsym(library, "vkGetInstanceProcAddr");
  if (!entry) {
    fprintf(stderr, "WebGPU: %s has no vkGetInstanceProcAddr\n", path);
    cosmo_dlclose(library);
    return 0;
  }
  /* Keep the successful library handle alive for all dispatch tables. */
  return adapt(entry, "vkGetInstanceProcAddr");
}
