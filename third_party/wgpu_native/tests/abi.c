/* SPDX-License-Identifier: MIT
 *
 * Host x86-64 tests of the generated System V -> Microsoft x64 adapters.
 * GCC emits the reference callees with ms_abi; no Windows OS or GPU is used.
 * Failures remain active with NDEBUG.
 */
#include "win64_bridge.h"
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !defined(__x86_64__) || !defined(__GNUC__)
#error "These ABI tests require an x86-64 compiler with GCC-style attributes."
#endif

#define MS_ABI __attribute__((ms_abi, noinline))
#define SYSV_ABI __attribute__((sysv_abi))
#define SYSV_NOINLINE __attribute__((sysv_abi, noinline))

static unsigned checks;
static unsigned failures;

static void check(int success, const char *name) {
  ++checks;
  if (!success) {
    ++failures;
    fprintf(stderr, "FAIL: %s\n", name);
  }
}

static void *bridge(void *function, const char *signature, const char *name) {
  void *result = cosmo_wgpu_win64_bridge(function, signature);
  if (!result) {
    ++checks;
    ++failures;
    fprintf(stderr, "FAIL: creating %s adapter: %s\n", name, strerror(errno));
  }
  return result;
}

static uint64_t MS_ABI no_arguments(void) {
  return UINT64_C(0xfedcba9876543210);
}

static const uint64_t integer_values[15] = {
    UINT64_C(0x0123456789abcdef), UINT64_C(0xfedcba9876543210),
    UINT64_C(0x1111222233334444), UINT64_C(0x88889999aaaabbbb),
    UINT64_C(0xdeadbeef12345678), UINT64_C(0x7fffffffffffffff),
    UINT64_C(0x8000000000000000), UINT64_C(0xffff00000000ffff),
    UINT64_C(0x0000ffffffff0000), UINT64_C(0x3141592653589793),
    UINT64_C(0x2718281828459045), UINT64_C(0xa5a5a5a55a5a5a5a),
    UINT64_C(0x0000000000000000), UINT64_C(0xffffffffffffffff),
    UINT64_C(0x12340000abcd0001),
};

static uint64_t MS_ABI fifteen_integers(
    uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
    uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8, uint64_t a9,
    uint64_t a10, uint64_t a11, uint64_t a12, uint64_t a13, uint64_t a14) {
  const uint64_t received[] = {
      a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14,
  };
  uint64_t bad_arguments = 0;
  for (unsigned i = 0; i < 15; ++i)
    if (received[i] != integer_values[i])
      bad_arguments |= UINT64_C(1) << i;
  return bad_arguments;
}

static uint64_t MS_ABI thirty_two_integers(
    uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
    uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8, uint64_t a9,
    uint64_t a10, uint64_t a11, uint64_t a12, uint64_t a13, uint64_t a14,
    uint64_t a15, uint64_t a16, uint64_t a17, uint64_t a18, uint64_t a19,
    uint64_t a20, uint64_t a21, uint64_t a22, uint64_t a23, uint64_t a24,
    uint64_t a25, uint64_t a26, uint64_t a27, uint64_t a28, uint64_t a29,
    uint64_t a30, uint64_t a31) {
  const uint64_t received[] = {
      a0,  a1,  a2,  a3,  a4,  a5,  a6,  a7,  a8,  a9,  a10,
      a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21,
      a22, a23, a24, a25, a26, a27, a28, a29, a30, a31,
  };
  uint64_t bad_arguments = 0;
  for (unsigned i = 0; i < 32; ++i)
    if (received[i] != UINT64_C(0x1020304050607000) + i)
      bad_arguments |= UINT64_C(1) << i;
  return bad_arguments;
}

struct MixedOutput {
  uint64_t integers[7];
  float floats[6];
  double doubles[6];
};

static double MS_ABI mixed_arguments(
    uint64_t i0, float f0, double d0, uint64_t i1, float f1, double d1,
    uint64_t i2, float f2, double d2, uint64_t i3, float f3, double d3,
    uint64_t i4, float f4, double d4, uint64_t i5, uint64_t i6, double d5,
    float f5, struct MixedOutput *output) {
  const uint64_t integers[] = {i0, i1, i2, i3, i4, i5, i6};
  const float floats[] = {f0, f1, f2, f3, f4, f5};
  const double doubles[] = {d0, d1, d2, d3, d4, d5};
  memcpy(output->integers, integers, sizeof(integers));
  memcpy(output->floats, floats, sizeof(floats));
  memcpy(output->doubles, doubles, sizeof(doubles));
  return -1234.625;
}

static float MS_ABI twelve_floats(
    float a0, float a1, float a2, float a3, float a4, float a5, float a6,
    float a7, float a8, float a9, float a10, float a11, float *output) {
  const float values[] = {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11};
  memcpy(output, values, sizeof(values));
  return -7.75f;
}

static double MS_ABI twelve_doubles(
    double a0, double a1, double a2, double a3, double a4, double a5, double a6,
    double a7, double a8, double a9, double a10, double a11, double *output) {
  const double values[] = {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11};
  memcpy(output, values, sizeof(values));
  return 8192.125;
}

static int MS_ABI narrow_integers(int32_t a, uint32_t b, int32_t c,
                                 uint32_t d, int32_t e, uint32_t f,
                                 int32_t g, uint32_t h) {
  return a == INT32_MIN && b == UINT32_MAX && c == -3 &&
         d == UINT32_C(0xdeadbeef) && e == -5 &&
         f == UINT32_C(0x80000001) && g == -7 &&
         h == UINT32_C(0xfedcba98);
}

struct ResolverContext {
  void *expected_handle;
  const char *expected_name;
  void *return_value;
  unsigned calls;
  unsigned bad_arguments;
};

static void *SYSV_NOINLINE resolver(void *handle, const char *name,
                                    struct ResolverContext *context) {
  ++context->calls;
  if (handle != context->expected_handle || name != context->expected_name)
    ++context->bad_arguments;
  return context->return_value;
}

static void test_no_arguments(void) {
  typedef uint64_t(SYSV_ABI * Call)(void);
  void *code = bridge((void *)no_arguments, "", "zero argument");
  if (code) {
    check(((Call)code)() == UINT64_C(0xfedcba9876543210),
          "zero arguments and full-width integer return");
    cosmo_wgpu_bridge_free(code);
  }
}

static void test_integers(void) {
  typedef uint64_t(SYSV_ABI * Call15)(
      uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
      uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
      uint64_t);
  void *code = bridge((void *)fifteen_integers, "iiiiiiiiiiiiiii",
                      "fifteen integer");
  if (code) {
    const uint64_t *a = integer_values;
    uint64_t bad = ((Call15)code)(a[0], a[1], a[2], a[3], a[4], a[5], a[6],
                                 a[7], a[8], a[9], a[10], a[11], a[12], a[13],
                                 a[14]);
    if (bad)
      fprintf(stderr, "15-argument mismatch mask: 0x%" PRIx64 "\n", bad);
    check(bad == 0, "all 15 integer arguments across both stack conventions");
    cosmo_wgpu_bridge_free(code);
  }

  typedef uint64_t(SYSV_ABI * Call32)(
      uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
      uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
      uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
      uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
      uint64_t, uint64_t, uint64_t, uint64_t);
  code = bridge((void *)thirty_two_integers, "iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii",
                "maximum integer");
  if (code) {
    uint64_t a[32];
    for (unsigned i = 0; i < 32; ++i)
      a[i] = UINT64_C(0x1020304050607000) + i;
    uint64_t bad = ((Call32)code)(
        a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10],
        a[11], a[12], a[13], a[14], a[15], a[16], a[17], a[18], a[19], a[20],
        a[21], a[22], a[23], a[24], a[25], a[26], a[27], a[28], a[29], a[30],
        a[31]);
    if (bad)
      fprintf(stderr, "32-argument mismatch mask: 0x%" PRIx64 "\n", bad);
    check(bad == 0, "all 32 integer arguments at the emitter's permitted bound");
    cosmo_wgpu_bridge_free(code);
  }

  typedef int(SYSV_ABI * NarrowCall)(int32_t, uint32_t, int32_t, uint32_t,
                                    int32_t, uint32_t, int32_t, uint32_t);
  code = bridge((void *)narrow_integers, "iiiiiiii", "32-bit integer");
  if (code) {
    check(((NarrowCall)code)(INT32_MIN, UINT32_MAX, -3, UINT32_C(0xdeadbeef),
                             -5, UINT32_C(0x80000001), -7,
                             UINT32_C(0xfedcba98)),
          "signed and unsigned 32-bit integer arguments and return");
    cosmo_wgpu_bridge_free(code);
  }
}

static void test_mixed(void) {
  typedef double(SYSV_ABI * Call)(
      uint64_t, float, double, uint64_t, float, double, uint64_t, float,
      double, uint64_t, float, double, uint64_t, float, double, uint64_t,
      uint64_t, double, float, struct MixedOutput *);
  const float floats[] = {-0.0f, 1.25f, -2.5f, 0x1.23456p5f, 4096.5f, -0.125f};
  const double doubles[] = {0x1.123456789abcp-3, -0.0, 1.5, -32768.25,
                            0x1.abcdef0123456p12, 0.0625};
  void *code = bridge((void *)mixed_arguments,
                      "ifd" "ifd" "ifd" "ifd" "ifd" "iidfi", "mixed scalar");
  if (code) {
    struct MixedOutput output;
    memset(&output, 0xa5, sizeof(output));
    const uint64_t *a = integer_values;
    double value = ((Call)code)(
        a[0], floats[0], doubles[0], a[1], floats[1], doubles[1], a[2],
        floats[2], doubles[2], a[3], floats[3], doubles[3], a[4], floats[4],
        doubles[4], a[5], a[6], doubles[5], floats[5], &output);
    check(!memcmp(output.integers, integer_values, sizeof(output.integers)),
          "mixed arguments: all integer registers and overflow slots");
    check(!memcmp(output.floats, floats, sizeof(floats)),
          "mixed arguments: f32 bit patterns including negative zero");
    check(!memcmp(output.doubles, doubles, sizeof(doubles)),
          "mixed arguments: f64 bit patterns including negative zero");
    check(value == -1234.625, "mixed arguments: scalar double return");
    cosmo_wgpu_bridge_free(code);
  }
}

static void test_float_overflow(void) {
  typedef float(SYSV_ABI * FloatCall)(
      float, float, float, float, float, float, float, float, float, float,
      float, float, float *);
  const float f[12] = {-0.0f, 1.25f, -2.5f, 3.75f, 0x1.123456p8f, -5.25f,
                       6.5f, -7.75f, 8.125f, -9.5f, 0x1.65432p-7f, 11.875f};
  void *code = bridge((void *)twelve_floats, "ffffffffffffi", "twelve f32");
  if (code) {
    float output[12];
    memset(output, 0xa5, sizeof(output));
    float value = ((FloatCall)code)(f[0], f[1], f[2], f[3], f[4], f[5], f[6],
                                     f[7], f[8], f[9], f[10], f[11], output);
    check(!memcmp(output, f, sizeof(f)),
          "12 f32 arguments, beyond eight SysV XMM registers, and output pointer");
    check(value == -7.75f, "scalar float return");
    cosmo_wgpu_bridge_free(code);
  }

  typedef double(SYSV_ABI * DoubleCall)(
      double, double, double, double, double, double, double, double, double,
      double, double, double, double *);
  const double d[12] = {
      -0.0, 1.25, -2.5, 3.75, 0x1.123456789abcdp8, -5.25,
      6.5, -7.75, 8.125, -9.5, 0x1.6543210123456p-7, 11.875,
  };
  code = bridge((void *)twelve_doubles, "ddddddddddddi", "twelve f64");
  if (code) {
    double output[12];
    memset(output, 0xa5, sizeof(output));
    double value = ((DoubleCall)code)(d[0], d[1], d[2], d[3], d[4], d[5],
                                      d[6], d[7], d[8], d[9], d[10], d[11],
                                      output);
    check(!memcmp(output, d, sizeof(d)),
          "12 f64 arguments, beyond eight SysV XMM registers, and output pointer");
    check(value == 8192.125, "double return after stack floating-point arguments");
    cosmo_wgpu_bridge_free(code);
  }
}

static void test_bound_context(void) {
  typedef void *(SYSV_ABI * Call)(void *, const char *);
  int handle_a, handle_b, result_a, result_b;
  static const char name_a[] = "vkFirstResolverTest";
  static const char name_b[] = "vkSecondResolverTest";
  struct ResolverContext a = {&handle_a, name_a, &result_a, 0, 0};
  struct ResolverContext b = {&handle_b, name_b, &result_b, 0, 0};
  void *first = cosmo_wgpu_bind_resolver((void *)resolver, &a);
  void *second = cosmo_wgpu_bind_resolver((void *)resolver, &b);
  check(first != NULL && second != NULL, "allocate two resolver contexts");
  if (first && second) {
    check(((Call)first)(&handle_a, name_a) == &result_a,
          "first resolver forwards two arguments and binds third context");
    check(((Call)second)(&handle_b, name_b) == &result_b,
          "second resolver binds an independent context");
    check(((Call)first)(&handle_a, name_a) == &result_a,
          "first resolver remains valid after creating another adapter");
    check(a.calls == 2 && b.calls == 1 && !a.bad_arguments && !b.bad_arguments,
          "resolver invocation counts and exact argument identities");
    cosmo_wgpu_bridge_free(first);
    first = NULL;
    check(((Call)second)(&handle_b, name_b) == &result_b && b.calls == 2,
          "freeing one mapping leaves the other resolver callable");
  }
  cosmo_wgpu_bridge_free(first);
  cosmo_wgpu_bridge_free(second);
}

static void reject_signature(void *function, const char *signature,
                              const char *name) {
  errno = 0;
  void *code = cosmo_wgpu_win64_bridge(function, signature);
  check(!code && errno == EINVAL, name);
  cosmo_wgpu_bridge_free(code);
}

static void test_rejections(void) {
  reject_signature(NULL, "", "reject null target function");
  reject_signature((void *)no_arguments, NULL, "reject null signature");
  reject_signature(NULL, NULL, "reject null target and null signature");
  reject_signature((void *)no_arguments, "iq", "reject unsupported argument type");
  reject_signature((void *)no_arguments, "I", "reject uppercase argument code");
  reject_signature((void *)no_arguments, "i f", "reject whitespace in signature");
  reject_signature((void *)no_arguments, "iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii",
                    "reject more than 32 arguments");
  errno = 0;
  void *code = cosmo_wgpu_bind_resolver(NULL, (void *)&checks);
  check(!code && errno == EINVAL, "reject null bound resolver target");
  cosmo_wgpu_bridge_free(code);
  errno = 0;
  code = cosmo_wgpu_bind_resolver((void *)resolver, NULL);
  check(!code && errno == EINVAL, "reject null bound resolver context");
  cosmo_wgpu_bridge_free(code);
  cosmo_wgpu_bridge_free(NULL);
}

int main(void) {
  test_no_arguments();
  test_integers();
  test_mixed();
  test_float_overflow();
  test_bound_context();
  test_rejections();
  if (failures) {
    fprintf(stderr, "ABI adapter checks: %u of %u failed\n", failures, checks);
    return 1;
  }
  printf("ABI adapter checks: %u passed (host x86-64 ms_abi simulation)\n",
         checks);
  puts("This checks calling conventions only; it is not a native Windows "
       "or Vulkan execution test.");
  return 0;
}
