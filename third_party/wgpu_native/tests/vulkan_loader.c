/* SPDX-License-Identifier: MIT
 * Provider selection tests with native-loader doubles and GCC ms_abi callees.
 * These check isolation and lifetimes; no Vulkan implementation is executed. */
#include "vulkan_loader.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MS_ABI __attribute__((ms_abi, noinline))
typedef void *(*Resolver)(uint64_t, const char *);
typedef uint64_t (*FourArgs)(uint64_t, uint64_t, uint64_t, uint64_t);
typedef int32_t (*Layers)(uint32_t *, void *);
static unsigned checks, failures, opens, symbols, foreign_tramps;
static int windows, linux_host = 1;

static void check(int success, const char *name) {
  ++checks;
  if (!success) {
    ++failures;
    fprintf(stderr, "FAIL: %s\n", name);
  }
}

static uint64_t software_call(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
  return a + 3 * b + 5 * c + 7 * d;
}

static uint64_t native_call(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
  return software_call(a, b, c, d) + 100;
}

static uint64_t MS_ABI windows_call(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
  return software_call(a, b, c, d) + 200;
}

static void *software_device(uint64_t device, const char *name) {
  return device == 17 && !strcmp(name, "vkCreateDevice") ? software_call : 0;
}

static void *software_instance(uint64_t instance, const char *name) {
  if (instance == 11 && !strcmp(name, "vkGetDeviceProcAddr"))
    return software_device;
  if (!strcmp(name, "vkCreateDevice")) return software_call;
  return 0;
}

static void *native_device(uint64_t device, const char *name) {
  return device == 27 && !strcmp(name, "vkCreateDevice") ? native_call : 0;
}

static void *native_instance(uint64_t instance, const char *name) {
  if (!strcmp(name, "vkGetInstanceProcAddr")) return native_instance;
  if (instance == 21 && !strcmp(name, "vkGetDeviceProcAddr")) return native_device;
  if (!strcmp(name, "vkCreateDevice")) return native_call;
  return 0;
}

static void *MS_ABI windows_device(uint64_t device, const char *name) {
  return device == 37 && !strcmp(name, "vkCreateDevice") ? windows_call : 0;
}

static void *MS_ABI windows_instance(uint64_t instance, const char *name) {
  if (!strcmp(name, "vkGetInstanceProcAddr")) return windows_instance;
  if (instance == 31 && !strcmp(name, "vkGetDeviceProcAddr")) return windows_device;
  if (!strcmp(name, "vkCreateDevice")) return windows_call;
  return 0;
}

int IsLinux(void) { return linux_host; }
int IsWindows(void) { return windows; }
void *cosmo_dlopen(const char *path, int flags) {
  (void)path;
  (void)flags;
  ++opens;
  return &opens;
}
void *cosmo_dlsym(void *library, const char *name) {
  (void)library;
  ++symbols;
  return !strcmp(name, "vkGetInstanceProcAddr")
             ? (windows ? (void *)windows_instance : (void *)native_instance) : 0;
}
int cosmo_dlclose(void *library) { (void)library; return 0; }
const char *cosmo_dlerror(void) { return "test native loader error"; }
void *cosmo_dltramp(void *function) { ++foreign_tramps; return function; }

static uint64_t call(Resolver entry, uint64_t instance, uint64_t device) {
  Resolver get_device = entry(instance, "vkGetDeviceProcAddr");
  FourArgs operation = get_device ? get_device(device, "vkCreateDevice") : 0;
  if (!operation) return UINT64_MAX;
  return operation(1, 2, 3, 4);
}

int main(void) {
  check(!strcmp(cosmo_wgpu_vulkan_selected_provider(), "native"), "native default");
  check(!cosmo_wgpu_vulkan_embedded_name(), "no embedded driver assumed");
  check(cosmo_wgpu_vulkan_select("automatic") == -1 && errno == EINVAL,
        "unknown provider rejected");
  check(cosmo_wgpu_vulkan_select("embedded") == -1 && errno == ENODEV,
        "unavailable software selection fails");
  check(!strcmp(cosmo_wgpu_vulkan_selected_provider(), "embedded"),
        "failed software selection remains software");
  check(!cosmo_wgpu_vulkan_entry(0) && opens == 0 && symbols == 0,
        "ignored selection error cannot fall back to native");

  check(cosmo_wgpu_vulkan_register_embedded("", software_instance) == -1 &&
            errno == EINVAL, "empty implementation name rejected");
  check(cosmo_wgpu_vulkan_register_embedded("test lavapipe", 0) == -1 &&
            errno == EINVAL, "null entry rejected");
  char name[] = "test lavapipe";
  check(!cosmo_wgpu_vulkan_register_embedded(name, software_instance),
        "embedded registration");
  name[0] = 'X';
  check(!strcmp(cosmo_wgpu_vulkan_embedded_name(), "test lavapipe"),
        "registered name owns its lifetime");
  check(!cosmo_wgpu_vulkan_register_embedded("test lavapipe", software_instance),
        "same registration is idempotent");
  check(cosmo_wgpu_vulkan_register_embedded("replacement", native_instance) == -1 &&
            errno == EEXIST, "existing driver cannot be replaced");
  check(!cosmo_wgpu_vulkan_select("embedded"), "registered software selected");
  Resolver software = cosmo_wgpu_vulkan_entry("");
  check(software && call(software, 11, 17) == 50,
        "software device pointers use direct System V calls");
  check(software && software(0, "vkGetInstanceProcAddr") == (void *)software,
        "software self-lookup retains layer-free facade");
  Layers layers = software ? software(0, "vkEnumerateInstanceLayerProperties") : 0;
  uint32_t count = 19;
  check(layers && !layers(&count, 0) && count == 0, "embedded layer count is zero");
  count = 19;
  check(layers && !layers(&count, &count) && count == 0,
        "Ash second zero-layer call succeeds");
  check(software && !software(11, "unknown") && !software(0, 0),
        "unknown or invalid software lookup returns null");
  check(!cosmo_wgpu_vulkan_entry("vulkan-1.dll") && errno == EINVAL,
        "software rejects an external library override");
  check(opens == 0 && symbols == 0 && foreign_tramps == 0 &&
            cosmo_wgpu_vulkan_native_open_count() == 0,
        "software performs no native opens, lookups, or TLS adapters");

  check(!cosmo_wgpu_vulkan_select("native"), "native selection restored");
  Resolver native = cosmo_wgpu_vulkan_entry(0);
  check(native && call(native, 21, 27) == 150, "native resolver chain still adapted");
  check(software && call(software, 11, 17) == 50,
        "old software entry survives native selection");
  check(opens == 1 && cosmo_wgpu_vulkan_native_open_count() == 1,
        "native open attempts are reported");

  linux_host = 0;
  windows = 1;
  Resolver windows_entry = cosmo_wgpu_vulkan_entry(0);
  check(windows_entry && call(windows_entry, 31, 37) == 250,
        "native Windows still uses forward Microsoft ABI adapters");
  unsigned old_symbols = symbols, old_tramps = foreign_tramps;
  check(!cosmo_wgpu_vulkan_select("embedded"), "software selected on Windows");
  Resolver windows_software = cosmo_wgpu_vulkan_entry(0);
  check(windows_software && call(windows_software, 11, 17) == 50,
        "Windows software stays System V without a Windows bridge");
  check(opens == 2 && symbols == old_symbols && foreign_tramps == old_tramps &&
            cosmo_wgpu_vulkan_native_open_count() == 2,
        "Windows software cannot touch a native loader");
  check(windows_entry && call(windows_entry, 31, 37) == 250,
        "old native entry survives software selection");

  linux_host = 0;
  windows = 0;
  check(cosmo_wgpu_vulkan_entry(0) && call(software, 11, 17) == 50,
        "embedded entry bypasses native OS gating");
  printf("Vulkan provider isolation: %u checks, %u failures\n", checks, failures);
  return failures != 0;
}
