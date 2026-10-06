/* SPDX-License-Identifier: MIT */
#ifndef COSMO_WGPU_VULKAN_LOADER_H_
#define COSMO_WGPU_VULKAN_LOADER_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Register a statically linked Vulkan implementation. The entry point and all
   pointers it returns must use the application's System V/Cosmopolitan ABI,
   including when the APE runs on Windows. The implementation must remain alive
   until process exit. Its display name is copied.

   One embedded implementation can be registered. An identical registration is
   idempotent; replacing it fails with EEXIST so existing Ash dispatch tables
   cannot change provider. Call this explicitly from linked driver glue: a weak
   reference alone would not pull a driver out of a static archive. */
int cosmo_wgpu_vulkan_register_embedded(const char *name,
                                      void *sysv_get_instance_proc_addr);

/* Choose "native" (the default) or "embedded" for subsequent entry loads.
   Selection is process-wide. Call before wgpuCreateInstance and serialize
   select/create sequences when choosing different providers. Existing
   instances retain their provider, so separately created native and embedded
   instances can coexist.

   Selecting an unavailable embedded implementation returns -1/ENODEV and keeps
   the selection on "embedded": even an ignored error cannot open a native
   loader as a software fallback. Invalid selectors fail with EINVAL. */
int cosmo_wgpu_vulkan_select(const char *provider);
const char *cosmo_wgpu_vulkan_selected_provider(void);
const char *cosmo_wgpu_vulkan_embedded_name(void);

/* Number of attempts to open an OS-native Vulkan loader through cosmo_dlopen.
   Software-only execution must leave this count unchanged (normally zero). */
unsigned long cosmo_wgpu_vulkan_native_open_count(void);

/* Ash entry-loader seam. An empty path uses the selected provider. Explicit
   library paths apply only to "native"; "embedded" rejects them. */
void *cosmo_wgpu_vulkan_entry(const char *path);

#ifdef __cplusplus
}
#endif
#endif
