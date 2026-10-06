/* SPDX-License-Identifier: MIT */
#ifndef COSMO_WGPU_WIN64_BRIDGE_H_
#define COSMO_WGPU_WIN64_BRIDGE_H_
#include <stddef.h>

/* Build an x86-64 System V -> Microsoft x64 call adapter. Signature letters
   describe scalar arguments: i = integer/pointer, f = float, d = double.
   This intentionally does not support varargs, aggregates, or callbacks. */
void *cosmo_wgpu_win64_bridge(void *, const char *);

/* Bind the third System V argument to a context pointer. Used for Vulkan's
   two procedure resolvers so their returned pointers are adapted as well. */
void *cosmo_wgpu_bind_resolver(void *, void *);

/* Mappings live as long as Vulkan's dispatch tables. Tests can release them. */
void cosmo_wgpu_bridge_free(void *);
#endif
