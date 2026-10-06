/* SPDX-License-Identifier: MIT */
#ifndef COSMO_LAVAPIPE_REGISTER_H_
#define COSMO_LAVAPIPE_REGISTER_H_
#ifdef __cplusplus
extern "C" {
#endif
/* Register the linked, SysV-ABI lavapipe implementation before creating WebGPU
   instances. This strong reference pulls the driver into a static executable. */
int cosmo_wgpu_lavapipe_register(void);
#ifdef __cplusplus
}
#endif
#endif
