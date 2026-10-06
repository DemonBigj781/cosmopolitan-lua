/* SPDX-License-Identifier: MIT
 * Host-only native-loader doubles for provider isolation tests. */
#ifndef COSMO_WGPU_TEST_COSMO_H_
#define COSMO_WGPU_TEST_COSMO_H_
int IsLinux(void);
int IsWindows(void);
void *cosmo_dlopen(const char *, int);
void *cosmo_dlsym(void *, const char *);
int cosmo_dlclose(void *);
const char *cosmo_dlerror(void);
void *cosmo_dltramp(void *);
#endif
