# LLVM for Cosmopolitan's embedded CPU Vulkan path

This recipe compiles LLVM 19.1.7 into x86-64 Cosmopolitan static libraries.
Mesa lavapipe uses these libraries to generate shader machine code inside the
application. The LLVM libraries shipped in the application share Cosmopolitan's
ABI and runtime. The native `llvm-tblgen` executable is only a build tool.

The source archives and SHA256 values are recorded in `IMPORT.json`. The
Cosmocc 4.0.2 SDK is the same pinned SDK prepared by `third_party/rust_ape`.

## Build

On a Linux x86-64 build host, install Python 3.12+, CMake 3.24+, Ninja, a native
C/C++ compiler, and `patch`. Then run:

```sh
bash third_party/rust_ape/run.sh setup
LLVM_COSMO_JOBS=2 bash third_party/llvm_cosmo/build.sh
```

The first build compiles the native TableGen tool and approximately 1,600 LLVM
source files, generated files, and static archives. It needs substantial build
time. `LLVM_COSMO_JOBS` defaults to 2 to limit compiler memory use. The LLVM
library itself is built with `-O1`; JIT-generated shader code uses the
optimization level selected by Mesa.

Outputs under `o/llvm-cosmo` include:

- `LINK.json`: the exact static archive closure, include directories, compiler
  ABI options, SDK location, host tool paths, and probe paths.
- `bin/llvm-config`: a build-host wrapper that runs the Cosmopolitan tool using
  the SDK's explicit APE loader.
- `llvm_jit_probe.exe`: the portable MCJIT runtime test.
- `llvm_jit_probe.com.dbg`: its ELF symbols and C probe debug information.
- `jit-probe.log`: output from the Linux runtime test.
- `licenses/`: LLVM and the included support-code notices.

`LLVM_COSMO_OUT` selects a different build directory. `CMAKE` and `NINJA` can
select native host tools by absolute path. `LLVM_HOST_CC` and `LLVM_HOST_CXX`
select the native compiler used only for TableGen. `COSMOCC` can select the
pinned SDK directory. `--prepare-only` downloads and patches sources;
`--no-run` builds the probe without executing it on the build host.

Source and configuration fingerprints preserve source modification times and
reuse Ninja's completed objects on a cache hit. A changed SDK compiler/runtime
fingerprint clears the target build because Ninja does not track compiler
binaries as ordinary object dependencies. The source archives are verified
before extraction, and patches apply with zero fuzz.

## Runtime decisions

LLVM's default and host triples are explicitly `x86_64-unknown-linux-musl`.
This chooses ELF relocation and the x86-64 System V calling convention for JIT
code, including when the APE runs on Windows. The triple does not cause calls
into the host Linux C library: the application links Cosmopolitan, and LLVM's
`dlopen`/`dladdr` paths are disabled. A Cosmo-only patch removes MCJIT's
otherwise mandatory attempt to open the running program through `dlopen`.
Mesa registers the portable runtime helpers its generated shaders may call;
unregistered symbols remain unresolved.

The source patch adapts LLVM's byte order, executable-path, filesystem, and
processor-count queries to Cosmopolitan. These adaptations follow the pinned
`ahgamut/superconfigure` LLVM port referenced in `IMPORT.json`. A small C++20
initialization adjustment is included, and the build selects C++20 to match
the working Cosmopolitan libc++ configuration.

The JIT allocator rounds whole `mmap` allocations to Cosmopolitan's runtime
`_SC_GRANSIZE`: 64 KiB on Windows and the normal mapping granularity on Linux.
Protection changes still use the actual page size. LLVM's SectionMemoryManager
retains and releases whole allocations even when it protects smaller code and
data ranges separately. On this SDK, LLVM detects and calls Cosmopolitan's
libunwind frame-registration functions directly.

Threads are enabled. Exceptions, RTTI, shared LLVM libraries, optional external
compression libraries, and foreign dynamic-symbol lookup are disabled. The
static component closure includes Mesa's MCJIT/X86 components and its coroutine
and pass-manager dependencies. `-lcxx` is part of the exported link arguments
because the final application's C/Rust linker does not automatically add the
C++ runtime.

## What the probe checks

The probe creates an LLVM module with the explicit System V target triple,
checks its IR, creates MCJIT machine code, and executes it. It checks integer
addition/multiplication with an explicitly registered C callback, a call with
seven integer and nine floating point arguments crossing register and stack
boundaries (with arguments reordered and changed in the JIT), and 4,000 calls
from four Cosmopolitan threads. JIT functions use
`noredzone`. It also checks that disposal returns without a crash and that the
executable's SHA256 remains unchanged after execution. The disposal marker
alone does not assert that every underlying unmap succeeded; LLVM's destructor
discards those error codes.

These are runtime foundation checks. The WebGPU CPU workflow separately checks
shader dispatch and readback using embedded lavapipe on Linux and Windows.
Measured platform results belong in that workflow's logs; a successful build
alone does not establish Windows JIT or shader execution.
