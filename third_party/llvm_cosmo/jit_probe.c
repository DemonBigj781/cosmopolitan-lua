/* SPDX-License-Identifier: MIT */
#include <cosmo.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/IRReader.h>
#include <llvm-c/Support.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

typedef int64_t (*JitIntFn)(int64_t, int64_t);
typedef double (*JitMixedFn)(int64_t, double, int64_t, double, int64_t, double,
                             int64_t, double, int64_t, double, int64_t, double,
                             int64_t, double, double, double);
static int64_t host_helper(int64_t sum, int64_t product) {
  return sum * 11 + product;
}
static double host_mixed(int64_t a, double x, int64_t b, double y,
                          int64_t c, double z, int64_t d, double w,
                          int64_t e, double u, int64_t f, double v,
                          int64_t g, double q, double r, double s) {
  return a + 2*b + 3*c + 4*d + 5*e + 6*f + 7*g +
         x + 2*y + 3*z + 4*w + 5*u + 6*v + 7*q + 8*r + 9*s;
}
struct ThreadArgs { JitIntFn fn; int index; int failed; };
static void *worker(void *opaque) {
  struct ThreadArgs *args = opaque;
  for (int64_t i = 0; i < 1000; ++i) {
    int64_t a = i + args->index;
    int64_t b = i * 3 + 17;
    if (args->fn(a, b) != host_helper(a + b, a * b)) {
      args->failed = 1;
      break;
    }
  }
  return NULL;
}
static void fail(const char *stage, char *message) {
  fprintf(stderr, "FAIL %s: %s\n", stage, message ? message : "unknown error");
  if (message) LLVMDisposeMessage(message);
  exit(1);
}
int main(void) {
  ShowCrashReports();
  setvbuf(stdout, NULL, _IONBF, 0);
  struct utsname os;
  if (!uname(&os)) printf("host: %s %s\n", os.sysname, os.machine);
  printf("page size: %ld, allocation granularity: %ld\n",
         sysconf(_SC_PAGESIZE), sysconf(_SC_GRANSIZE));
  unsigned major, minor, patch;
  LLVMGetVersion(&major, &minor, &patch);
  printf("Cosmopolitan static LLVM: %u.%u.%u\n", major, minor, patch);
  if (LLVMInitializeNativeTarget() || LLVMInitializeNativeAsmPrinter())
    fail("native target initialization", NULL);
  LLVMLinkInMCJIT();
  LLVMAddSymbol("cosmo_probe_helper", (void *)(uintptr_t)&host_helper);
  LLVMAddSymbol("cosmo_probe_mixed", (void *)(uintptr_t)&host_mixed);
  static const char ir[] =
    "target triple = \"x86_64-unknown-linux-musl\"\n"
    "declare i64 @cosmo_probe_helper(i64, i64)\n"
    "declare double @cosmo_probe_mixed(i64,double,i64,double,i64,double,i64,double,i64,double,i64,double,i64,double,double,double)\n"
    "define i64 @jit_integer(i64 %a, i64 %b) noredzone {\n"
    "  %s = add i64 %a, %b\n"
    "  %p = mul i64 %a, %b\n"
    "  %r = call i64 @cosmo_probe_helper(i64 %s, i64 %p)\n"
    "  ret i64 %r\n"
    "}\n"
    "define double @jit_mixed(i64 %a,double %x,i64 %b,double %y,i64 %c,double %z,i64 %d,double %w,i64 %e,double %u,i64 %f,double %v,i64 %g,double %q,double %r,double %s) noredzone {\n"
    "  %aa = add i64 %a, 10\n"
    "  %xx = fadd double %x, 1.000000e+00\n"
    "  %answer = call double @cosmo_probe_mixed(i64 %g,double %s,i64 %f,double %r,i64 %e,double %q,i64 %d,double %v,i64 %c,double %u,i64 %b,double %w,i64 %aa,double %z,double %y,double %xx)\n"
    "  %adjusted = fadd double %answer, 1.250000e+00\n"
    "  ret double %adjusted\n"
    "}\n";
  LLVMContextRef context = LLVMContextCreate();
  LLVMMemoryBufferRef buffer = LLVMCreateMemoryBufferWithMemoryRangeCopy(ir, sizeof(ir)-1, "cosmo-jit-probe");
  LLVMModuleRef module = NULL;
  char *message = NULL;
  if (LLVMParseIRInContext(context, buffer, &module, &message)) fail("parse IR", message);
  if (LLVMVerifyModule(module, LLVMReturnStatusAction, &message)) fail("verify IR", message);
  if (message) { LLVMDisposeMessage(message); message = NULL; }
  printf("JIT target triple: %s\n", LLVMGetTarget(module));
  struct LLVMMCJITCompilerOptions options;
  LLVMInitializeMCJITCompilerOptions(&options, sizeof(options));
  options.OptLevel = 2;
  options.CodeModel = LLVMCodeModelLarge;
  options.NoFramePointerElim = 1;
  LLVMExecutionEngineRef engine = NULL;
  if (LLVMCreateMCJITCompilerForModule(&engine, module, &options, sizeof(options), &message))
    fail("create MCJIT", message);
  JitIntFn integer = (JitIntFn)(uintptr_t)LLVMGetFunctionAddress(engine, "jit_integer");
  JitMixedFn mixed = (JitMixedFn)(uintptr_t)LLVMGetFunctionAddress(engine, "jit_mixed");
  if (!integer || !mixed) fail("get JIT function address", NULL);
  if (integer(6,7) != 185) fail("JIT integer add/multiply and external C call", NULL);
  printf("JIT integer add/multiply + external C callback: PASS (185)\n");
  double expected = host_mixed(7,8.5,6,7.5,5,6.5,4,5.5,3,4.5,2,3.5,11,2.5,1.5,1.5) + 1.25;
  double got = mixed(1,0.5,2,1.5,3,2.5,4,3.5,5,4.5,6,5.5,7,6.5,7.5,8.5);
  if (got != expected) {
    fprintf(stderr, "mixed result %.17g != %.17g\n", got, expected);
    fail("mixed scalar SysV registers and stack", NULL);
  }
  printf("JIT mixed scalar SysV registers and stack: PASS (%.2f)\n", got);
  pthread_t threads[4];
  struct ThreadArgs args[4];
  for (int i=0;i<4;++i) {
    args[i]=(struct ThreadArgs){.fn=integer,.index=i};
    if (pthread_create(&threads[i],NULL,worker,&args[i])) fail("pthread_create",NULL);
  }
  for (int i=0;i<4;++i) {
    if (pthread_join(threads[i],NULL) || args[i].failed) fail("threaded JIT execution",NULL);
  }
  printf("JIT execution from 4 Cosmopolitan threads (4000 calls): PASS\n");
  LLVMDisposeExecutionEngine(engine);
  LLVMContextDispose(context);
  LLVMShutdown();
  printf("JIT engine disposal completed without a crash: PASS\n");
  return 0;
}
