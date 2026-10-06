# Embedded CPU Vulkan experiment

The `webgpu-cpu` branch adds an explicit software execution mode to the
Cosmopolitan WebGPU prototype. Its intended deployment is one x86-64 APE
containing the C application, wgpu-native, Mesa lavapipe/llvmpipe, and LLVM's
CPU code generator. The embedded mode uses the CPU and needs no OS-native
Vulkan loader or Mesa driver package.

The project favors C where practical. Its deployment requirement is one
identical executable for Windows and Linux; it does not require every
dependency to be written in C. The application and driver registration are
C, while wgpu-native contributes Rust and LLVM contributes C++ code linked
into the same executable.

**Status: embedded shader execution passes locally on Linux.** The combined
prototype passes both numerical checks below using the embedded CPU provider,
with zero native Vulkan loader opens and an unchanged executable hash. The
reproducible source-build and native Windows/isolation CI gates remain pending.
The earlier Windows/Linux results in [README.md](README.md) used external Mesa
drivers; they are a separate baseline and do not establish this embedded mode.

## Build and run

On a Linux x86-64 build machine, use the pinned Cosmocc/Rust SDK already used
by the WebGPU target:

```sh
bash third_party/wgpu_native/build.sh --software
```

This additionally builds LLVM 19.1.7 and Mesa 25.2.8 as Cosmopolitan static
libraries. A native host table generator and glslang compile build inputs;
they are build tools and are not invoked by the deployed application. See
the recipes under `../llvm_cosmo/` and `../lavapipe/` for dependency pins and
portability patches. The first build is substantially larger than the original
WebGPU-only build; subsequent builds can reuse the LLVM cache.

The application is still `o/webgpu/webgpu_compute.exe`. On Linux:

```sh
o/webgpu/ape-x86_64.elf o/webgpu/webgpu_compute.exe --software
o/webgpu/ape-x86_64.elf o/webgpu/webgpu_compute.exe --software --matmul
```

For distribution as a single file, Linux can bootstrap the loader already
bundled inside the application:

```sh
sh ./webgpu_compute.exe --software --matmul
```

The initial shell path needs `uname`, `mkdir`, `dd`, `gzip`, `chmod`, and `mv`.
It extracts a small APE loader into temporary storage, then executes the
application without rewriting its bytes. The local check passed with only
those utilities on `PATH`, no installed `ape` command, and an initially empty
temporary directory. The explicit loader above provides a separate startup
route for environments where those shell utilities are unavailable.

On Windows x86-64, copy that same executable and run:

```powershell
.\webgpu_compute.exe --software
.\webgpu_compute.exe --software --matmul
```

`--version` checks the linked C/Rust WebGPU API without creating a Vulkan
instance. Omitting `--software` selects the existing host-native Vulkan path.
The software option is strict: an executable built without the embedded
driver fails with an explanation. It never substitutes an external software
driver for the requested embedded implementation.

Shader compilation at runtime is performed by the compilers linked into the
executable: WGSL is translated to SPIR-V, Mesa lowers it to its shader
representation, and LLVM generates CPU machine code for that host. There is
no invocation of an installed C compiler in this execution path.

## What the numerical checks exercise

The default compute check runs the embedded Collatz shader and compares every
readback value against `[0, 1, 7, 2]`. It exercises integer arithmetic, loops,
buffer upload, queue submission, synchronization, and readback.

`--matmul` multiplies a 13-by-17 matrix by a 17-by-11 matrix. It uses 8-by-8
workgroups, shared workgroup arrays, and barriers between tiles. The odd
dimensions require both partially occupied workgroups and a final partial
tile. The host independently computes a double-precision reference and
checks all 143 f32 results with a documented absolute/relative tolerance in
the source. The inputs must also remain unchanged. This is a correctness
check, not an inference benchmark.

Both software checks require a WebGPU CPU adapter, identify the embedded
provider, and require a zero native Vulkan loader-open count. The count covers
the application's loader boundary. The separate filesystem isolation check
tests the full implementation's dependency on external files and libraries.

## How the static driver is reached

The application explicitly calls `cosmo_wgpu_lavapipe_register()`. Its strong
reference brings the Mesa entry point into the final link. The driver registers
`lvp_GetInstanceProcAddr` with `runtime/vulkan_loader.c`, then the application
selects the `embedded` provider before creating its WebGPU instance.

Embedded Vulkan function pointers use the same System V/Cosmopolitan calling
convention as the application, on both operating systems. They do not pass
through the Windows native-call adapter or Linux foreign-library/TLS bridge.
A small layer-enumeration facade presents the zero-layer contract expected by
Ash when calling an ICD directly. The remaining entry points come from Mesa.

The original native-provider path still uses the OS loader and its existing
ABI adapters. Provider selection applies to future instance creation; already
created instances keep their own entry tables. One instance does not merge the
embedded adapter with the native driver's adapters. This milestone supplies
explicit selection, not an automatic device-selection or multi-GPU scheduler.

## Portability changes that matter at runtime

- Mesa recognizes Cosmopolitan as a POSIX runtime without selecting Linux-only
  device, external-memory, or DRM implementation paths.
- LLVM's mapped allocations use the runtime allocation granularity, including
  Windows' 64 KiB requirement. Memory protection operates at page granularity.
- JIT code uses the application's SysV/ELF conventions on both OSes. Shader
  functions disable use of the x86 red zone.
- Generated shader calls to C and math helpers resolve to explicitly registered
  functions already linked into the application. LLVM's native dynamic-library
  lookup is disabled in this build.
- The driver is headless and does not create windows or use a display server.
  Disk shader caching and native external-resource sharing are disabled for
  the initial portable target.

Cosmopolitan's earlier host-`cc` requirement belongs to its Linux
`cosmo_dlopen` path. Embedded execution bypasses that path. Hardware execution
through an OS-native driver retains the earlier native-loader boundary.

## Same-artifact verification

`.github/workflows/webgpu-cpu.yml` builds the application once on Linux and
uploads it for independent Linux and Windows execution. Each runtime job
checks the executable's SHA-256 before and after running it. The Windows job
also runs the independently built LLVM JIT diagnostic before testing the full
WebGPU/Mesa path.

The Linux job first copies only the application into a fresh directory and
runs its shell bootstrap with a controlled path containing the six utilities
listed above. It verifies matrix computation, extraction of the bundled
loader, and an unchanged application hash. This checks the one-file
distribution path separately from the dependency-isolation check below.

The Linux job runs this verifier with actual chroot capability:

```sh
sudo python3 third_party/wgpu_native/tests/run_isolated.py \
  --artifact-dir o/webgpu
```

Each numerical test receives a fresh temporary root containing only the
application, its explicit APE loader, and writable `/tmp`. No compiler,
Vulkan loader, Mesa library, model, shader source file, or shell is copied
inside. The verifier uses a sanitized environment, bounded execution time,
provider/adapter checks, numerical-result checks, and checksums of both the
source and executed copies. Logs and a JSON report remain outside the
temporary root for review.

Unavailable chroot capability is a failure of this verification gate, never
a pass or a substitute host-environment run. The current local execution
container reports `EPERM` for chroot; the actual deployment-isolation result
must therefore come from the capable CI runner.

The Windows job downloads the same application and installs no Vulkan runtime
or Mesa package. It requires the full numerical result markers, a CPU adapter,
the embedded entry trace, and exactly zero native Vulkan loader opens.

## Boundaries and later AI work

The first target is integer and f32 compute on x86-64 Windows/Linux. Half
precision, other CPU architectures, rendering, physical GPU compatibility,
and performance require their own evidence. In particular, f16 lowering can
require additional statically registered conversion and rounding helpers.

This program contains compute checks rather than a model loader or inference
engine. A C inference engine could call WebGPU while keeping direct CPU
kernels as an additional execution option. The software Vulkan path provides
reuse of WebGPU shaders; a direct C kernel can make different implementation
and performance tradeoffs.

[Colibri](https://github.com/JustVugg/colibri) is a relevant design reference
for that later layer: its C inference core, quantized compute routines, and
expert streaming illustrate how model execution and weight placement can be
separated from backend choice. Its current frontend and native GPU packaging
have their own dependencies and platform paths. No Colibri code is imported
or tested by this software Vulkan experiment.
