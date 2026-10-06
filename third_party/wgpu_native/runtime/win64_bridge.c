/* SPDX-License-Identifier: MIT */
#include "win64_bridge.h"
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#if !defined(__x86_64__)
#error "The experimental WebGPU ABI bridge currently supports x86-64 only."
#endif

/* Every argument is first saved in a private spill area. Loading the Windows
   arguments afterwards prevents register cycles and handles mixed floating
   point/integer arguments and both ABIs' stack overflow rules. Vulkan uses
   only scalar arguments and scalar returns in the pinned dispatch tables. */
struct Code {
  unsigned char *base;
  size_t size;
  size_t used;
};

static void emit(struct Code *c, unsigned char b) {
  c->base[c->used++] = b;
}

static void word32(struct Code *c, uint32_t w) {
  memcpy(c->base + c->used, &w, sizeof(w));
  c->used += sizeof(w);
}

static void word64(struct Code *c, uint64_t w) {
  memcpy(c->base + c->used, &w, sizeof(w));
  c->used += sizeof(w);
}

/* mov r64, [rsp+disp32], or the inverse. */
static void gp(struct Code *c, int load, unsigned reg, uint32_t offset) {
  emit(c, 0x48 | ((reg >> 3) << 2));
  emit(c, load ? 0x8b : 0x89);
  emit(c, 0x84 | ((reg & 7) << 3));
  emit(c, 0x24);
  word32(c, offset);
}

/* movq transfers both float and double payloads without changing their bits. */
static void xmm(struct Code *c, int load, unsigned reg, uint32_t offset) {
  emit(c, load ? 0xf3 : 0x66);
  emit(c, 0x0f);
  emit(c, load ? 0x7e : 0xd6);
  emit(c, 0x84 | (reg << 3));
  emit(c, 0x24);
  word32(c, offset);
}

static void rsp(struct Code *c, int subtract, uint32_t size) {
  emit(c, 0x48);
  emit(c, 0x81);
  emit(c, subtract ? 0xec : 0xc4);
  word32(c, size);
}

static int allocate(struct Code *c) {
  long page = sysconf(_SC_PAGESIZE);
  if (page < 4096) {
    errno = ENOTSUP;
    return -1;
  }
  c->size = (size_t)page;
  c->used = 0;
  c->base = mmap(0, c->size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return c->base == MAP_FAILED ? -1 : 0;
}

static void *finish(struct Code *c) {
  if (mprotect(c->base, c->size, PROT_READ | PROT_EXEC)) {
    int error = errno;
    munmap(c->base, c->size);
    errno = error;
    return 0;
  }
  __builtin___clear_cache((char *)c->base, (char *)c->base + c->used);
  return c->base;
}

void *cosmo_wgpu_win64_bridge(void *function, const char *signature) {
  static const unsigned sysv_gp[] = {7, 6, 2, 1, 8, 9};
  static const unsigned win_gp[] = {1, 2, 8, 9};
  struct Code c;
  if (!signature) {
    errno = EINVAL;
    return 0;
  }
  size_t count = strlen(signature);
  /* 32 arguments generate < 1800 bytes; the pinned Vulkan maximum is 15.
     Bounds are checked before emitting any bytes into the one-page mapping. */
  if (!function || count > 32 || strspn(signature, "ifd") != count) {
    errno = EINVAL;
    return 0;
  }
  if (allocate(&c)) return 0;
  uint32_t outgoing = 32 + (count > 4 ? (count - 4) * 8 : 0);
  uint32_t spill = outgoing;
  uint32_t frame = ((outgoing + count * 8 + 15) & ~15u) + 8;
  unsigned integers = 0, floats = 0, stacked = 0;
  rsp(&c, 1, frame);
  for (unsigned i = 0; i < count; ++i) {
    if (signature[i] == 'i' && integers < 6) {
      gp(&c, 0, sysv_gp[integers++], spill + i * 8);
    } else if (signature[i] != 'i' && floats < 8) {
      xmm(&c, 0, floats++, spill + i * 8);
    } else {
      gp(&c, 1, 0, frame + 8 + stacked++ * 8);
      gp(&c, 0, 0, spill + i * 8);
    }
  }
  for (unsigned i = 0; i < count; ++i) {
    if (i < 4) {
      if (signature[i] == 'i') {
        gp(&c, 1, win_gp[i], spill + i * 8);
      } else {
        xmm(&c, 1, i, spill + i * 8);
      }
    } else {
      gp(&c, 1, 0, spill + i * 8);
      gp(&c, 0, 0, 32 + (i - 4) * 8);
    }
  }
  emit(&c, 0x48);  /* movabs function, %rax */
  emit(&c, 0xb8);
  word64(&c, (uintptr_t)function);
  emit(&c, 0xff);  /* call *%rax */
  emit(&c, 0xd0);
  rsp(&c, 0, frame);
  emit(&c, 0xc3);
  return finish(&c);
}

void *cosmo_wgpu_bind_resolver(void *function, void *context) {
  struct Code c;
  if (!function || !context) {
    errno = EINVAL;
    return 0;
  }
  if (allocate(&c)) return 0;
  emit(&c, 0x48);  /* movabs context, %rdx (third System V argument) */
  emit(&c, 0xba);
  word64(&c, (uintptr_t)context);
  emit(&c, 0x48);  /* movabs function, %rax */
  emit(&c, 0xb8);
  word64(&c, (uintptr_t)function);
  emit(&c, 0xff);  /* jmp *%rax, preserving the caller's return address */
  emit(&c, 0xe0);
  return finish(&c);
}

void cosmo_wgpu_bridge_free(void *code) {
  if (code) munmap(code, (size_t)sysconf(_SC_PAGESIZE));
}
