# WebGPU inside a Cosmopolitan executable

This experimental branch builds a **C application and wgpu-native's Rust
implementation into one x86-64 Actually Portable Executable (APE)**. The selected
backend is Vulkan. It does not bundle separately compiled Windows and Linux
copies of wgpu-native.

The initial program is a headless compute check. A local Linux run has compiled
the embedded WGSL shader, dispatched four workgroups, and verified every result
through Mesa lavapipe. The output identifies lavapipe as a CPU/software adapter.
Actual Windows execution and physical GPU acceleration still need their own
successful runs. This is a working Linux compute prototype, not a completed
all-OS AI runtime.

## Build

Use a Linux x86-64 build host with Python 3.12 or later. On Ubuntu 24.04:

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  build-essential ca-certificates clang curl git libclang-dev \
  patch pkg-config python3 unzip

bash third_party/wgpu_native/build.sh
```

The first build downloads the pinned compilers and Rust dependencies. All
generated files live under `o/`. The build uses Rust `nightly-2026-07-28`,
Cosmocc **4.0.2**, and the patched Rust standard library from `rust-ape`.
This target explicitly uses that SDK; it does not rebuild the repository's
ordinary Cosmocc 3.9.2 toolchain. Expect several gigabytes of build/cache space.

The build produces:

| Output | Purpose |
| --- | --- |
| `o/webgpu/webgpu_compute.exe` | The one x86-64 APE containing C, Rust, WebGPU, and the Vulkan backend |
| `o/webgpu/ape-x86_64.elf` | Matching explicit Linux loader, useful where APE is not registered |
| `o/webgpu/BUILD.json` | Source/toolchain revisions, lockfile hash, executable hash |
| `o/webgpu/SHA256SUMS` | Identity checks before and after execution |
| `o/webgpu/licenses/` | Dependency license notices for redistribution |
| `o/rust-ape/build/x86_64-unknown-linux-musl/release/libwgpu_native.a` | Cosmopolitan-compatible static library for the final link |

The sample build measured **5,723,789 bytes** for the executable. Its size and
hash can change with later source edits; use the manifest from your build.

If libclang is installed outside the system search path, set `LIBCLANG_PATH`
to the directory containing its shared library. Bindgen reads Cosmopolitan's
headers and normalization include, so it does not infer C types from a
different host runtime.

## Run

On Linux, check the C/Rust API without loading Vulkan:

```sh
bash third_party/wgpu_native/test.sh --version
```

For compute, install a host Vulkan loader and driver. A software driver is
enough to check functionality:

```sh
sudo apt-get install -y build-essential libvulkan1 mesa-vulkan-drivers
bash third_party/wgpu_native/test.sh
```

The script uses the explicit APE loader and checks the executable's hash before
and after execution. It imposes a process timeout in addition to the sample's
bounded callback polling. To choose a particular Vulkan implementation, set
`VK_DRIVER_FILES` to the driver's ICD JSON file before running.

On Windows x86-64, copy the **same** `webgpu_compute.exe` and run in PowerShell:

```powershell
.\webgpu_compute.exe --version
.\webgpu_compute.exe
```

The compute command needs a compatible installed Vulkan loader/driver. A
Windows PE header and successful host ABI simulation are not substitutes for
testing those commands on Windows. The branch's CI downloads the one Linux
build artifact for independent Linux and Windows startup tests, and separately
tests compute on both systems using lavapipe. The Windows compute job downloads
checksum-pinned Mesa and LunarG runtime components; it does not rebuild wgpu.

## What has been measured

Local validation on 2026-10-06:

| Check | Result | What it establishes |
| --- | --- | --- |
| Patched Rust std + `parking_lot` APE | Passed on Linux | Runtime linking and threaded synchronization |
| Native call adapter tests | 27 passed with host GCC `ms_abi` functions | Scalar register/stack conversion, mixed floating point, resolver contexts, and rejection cases |
| Full C + Rust + WebGPU build | Passed | A statically linked WebGPU application packaged as x86-64 APE/PE |
| `--version` | Passed | Calls the actual Rust `wgpuGetVersion`, returning `29.0.1.1` |
| WebGPU shader dispatch/readback | Passed on Linux lavapipe | Buffer upload, WGSL compilation, Vulkan execution, synchronization, and exact output |
| Executable hash before/after both runs | Unchanged | Both checks executed the same file without rewriting it |
| Native Windows execution | Pending | Must be verified by the Windows runtime job or an actual machine |
| Physical GPU acceleration | Pending | Software Vulkan does not establish hardware support or performance |

The successful compute run reported:

```text
wgpu-native 29.0.1.1 (0x1d000101)
C -> Rust version check: PASS
adapter: llvmpipe (LLVM 20.1.2, 256 bits); Mesa 25.2.8-0ubuntu0.24.04.2 (LLVM 20.1.2); backend=6 (Vulkan=6); type=CPU/software
input: [1, 2, 3, 4]
readback: [0, 1, 7, 2]
headless Vulkan compute and readback: PASS
```

## How the integration works

1. `upstream/` contains **wgpu-native v29.0.1.1** and its exact WebGPU header
   submodule as ordinary source files. The implementation stays in Rust; the
   application calls its C interface.
2. `../rust_ape/` supplies the experimental Rust/Cosmopolitan standard-library
   port. It rebuilds `std`, patches `libc` and `errno`, and links runtime shims
   so a Linux compile-time personality does not directly issue Linux-only
   operations when the executable runs on another OS.
3. `prepare.py` checks the pristine source hashes, downloads checksum-pinned
   ash and wgpu-hal crates, and applies the small reviewed patches under `o/`.
   `Cargo.cosmo.lock` pins the modified graph. The original upstream lockfile
   remains available for comparison.
4. The ash patch uses `runtime/vulkan_loader.c`. It chooses `vulkan-1.dll` or
   `libvulkan.so.1` at runtime with Cosmopolitan's native-library API.
5. The loader adapts **every function returned by both Vulkan procedure
   resolvers**, including device-specific addresses. Linux calls use
   `cosmo_dltramp` for the foreign runtime boundary. Windows uses a generated
   scalar call adapter with metadata for **684** procedures from the exact ash
   dispatch tables. The Windows adapter handles mixed integer/float arguments
   and stack arguments; a generic integer-only cast would not suffice.
6. The C smoke program embeds WGSL and checks its numerical results. It needs
   no browser, window, display server, or external shader file.

## Current boundaries

- **CPU/OS scope:** x86-64 Windows and Linux are the intended targets. This
  integration does not claim ARM64, macOS/Metal, Direct3D 12, browser execution,
  or universal OS support.
- **Native callbacks:** Vulkan debug/validation callbacks need a reverse ABI
  and foreign-thread TLS design. The patch rejects debug/validation flags and
  custom native instance callbacks before loading Vulkan. The sample uses
  flags zero. WebGPU's normal API validation remains enabled.
- **Headless scope:** surfaces, swapchains, rendering, external resource
  sharing, and broader multithreaded driver behavior require further tests.
  Forward scalar ABI coverage alone does not establish those features.
- **Host dependency:** Vulkan loaders and drivers remain OS-specific system
  software. They are not included in the executable.
- **Linux loader helper:** Cosmopolitan 4.0.2's `cosmo_dlopen` invokes the host
  `cc` to compile a small native helper on first use. Therefore this version
  does **not** yet prove deployment to a compiler-free Linux installation.
  The application and wgpu implementation are built once; the native helper
  is a remaining runtime packaging issue.
- **Experimental runtime:** `rust-ape` is a community port, with its own
  documented limitations. Building with a normal Linux Rust toolchain and
  merely renaming its library would not provide the same runtime.
- **AI work remains:** WebGPU supplies compute operations. A model loader,
  tensor engine, operator kernels, numerical tests, and CPU fallback must
  still be integrated to run a particular model.

Successful startup is kept separate from successful compute, and software
compute is kept separate from hardware acceleration in the CI job names and
test output. Build once, then compare the same artifact's hash on each host
before making a broader portability claim.

## Sources and licenses

See [README.cosmo](README.cosmo) and [IMPORT.json](IMPORT.json) for immutable
source revisions, original file hashes, pruning, licenses, and local changes.
The port's small C/Python/shell helpers use the MIT license; the adapted compute
example keeps upstream's MIT OR Apache-2.0 choice. Original dependency notices
are retained and packaged with the build.
