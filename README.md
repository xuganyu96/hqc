[![pipeline status](https://gitlab.com/pqc-hqc/hqc/badges/main/pipeline.svg)](https://gitlab.com/pqc-hqc/hqc/-/commits/main)
[![Latest Release](https://gitlab.com/pqc-hqc/hqc/-/badges/release.svg)](https://gitlab.com/pqc-hqc/hqc/-/releases)

---
# Hamming Quasi-Cyclic (HQC)
---

[HQC](https://pqc-hqc.org) is a code-based Key Encapsulation Mechanism (KEM) selected by NIST during the [NIST's Post-Quantum Cryptography Standardization Project](https://csrc.nist.gov/projects/post-quantum-cryptography). This repository contains the implementation provided by the HQC team.

# Structure
---

The project is organized into modular components, including cross-platform code, tests, known-answer vectors, and packaging tools.

+ **src/** – HQC implementations
  + **common/** – shared cross-platform source/header files
  + **ref/** – reference implementation (no SIMD)
  + **x86_64/** – optimized SIMD code
    + **avx256/** – AVX2/AVX-256 optimized implementation
    + **common/** – shared x86-specific source/header files
+ **lib/** – third-party libraries (e.g. **fips202/** for SHA-3/Shake)
+ **tests/** – test-suite root (built with CMake)
  + **unit/** – unit tests (GF, Reed–Solomon, …)
  + **api/** – KEM/PKE API tests
  + **bench/** – benchmarks
  + **kats/** – KAT files tests
  + **external/munit/** – copy of the *munit* testing framework
+ **kats/** – official Known-Answer Test (KAT) files
  + **ref/** – files generated with the reference build
  + **x86_64/avx256** – files generated with the optimized build
  + **rs/** – Reed-Solomon test vectors shared by the ref and x86_64/avx256 builds
+ **packaging/** – scripts and Doxygen docs for packaging and documentation
+ **tools/** – developer utilities
+ **CHANGELOG** – project changelog
+ **CMakeLists.txt** – top-level CMake build script (sub-directories have their own)
+ **LICENSE** – project license
+ **README.md** – this file





# Usage
---

## Requirements

- **C compiler** (GCC ≥ 11, Clang, etc.) with C11 support
- **CMake** ≥ 3.21
- **Ninja** or **Make** or another CMake generator
- **clang-format** for code formatting
- **NTL + GMP** (optional) for unit tests (skipped is dependecies are missing).


## Building & Testing

One can build any variant in an out-of-tree directory and run the full test-suite
with a single command line.  Choose the architecture (`HQC_ARCH`), the exact
SIMD back-end if applicable (`HQC_X86_IMPL`), and an optional runtime sanitizer
(`HQC_SANITIZER`).

```bash
# 1) Configure
cmake -S . -B build-<arch> \
      -DCMAKE_BUILD_TYPE=Release \
      -DHQC_ARCH=<arch> \
      -DHQC_X86_IMPL=<impl> \
      -DHQC_SANITIZER=<sanitizer>

# 2) Build
cmake --build build-<arch> -- -j$(nproc)

# 3) Test
ctest --test-dir build-<arch> --output-on-failure -j$(nproc)
```

**Configuration Options**

- **`<arch>`**: `ref`, `x86_64`
- **`<impl>`**: `avx256`
- **`<sanitizer>`**: `NONE`, `ASAN`, `LSAN`, `MSAN`, `UBSAN`


**Example 1 (Portable reference implementation)**

```bash
rm -rf build-ref &&
cmake -S . -B build-ref \
-DCMAKE_BUILD_TYPE=Release \
-DHQC_ARCH=ref &&
cmake --build build-ref -j$(nproc) &&
ctest  --test-dir build-ref -j$(nproc)
```


**Example 2 (Optimized AVX2 implementation (Haswell/AVX256))**

```bash
rm -rf build-avx256 &&
cmake -S . -B build-avx256 \
-DCMAKE_BUILD_TYPE=Release \
-DHQC_ARCH=x86_64 \
-DHQC_X86_IMPL=avx256 &&
cmake --build build-avx256 -j$(nproc) &&
ctest  --test-dir build-avx256 -j$(nproc)
```


## Developer Tools

Optional developer tools live under `tools/` and can be built using `-DHQC_BUILD_TOOLS=ON` (see [`tools/README.md`](tools/README.md) for commands and usage).





# Security
---

**Scope.** This project targets resistance against timing attacks only, other side-channels attacks are not currently considered.

This project is expected to avoid secret-dependent control flows and access to secret memory locations in order to thwart timing attacks.
Hardening the code against compiler-induced timing attacks is a work in progress and only vulnerabilities introduced when compiled following our compilation instructions on targetted architectures will be considered (see #11 and #12).





# Contributing
---

Till the FIPS 207 standard is available, people are encouraged to report issues here by opening a `work item`. \
Merge requests addressing work items are also appreciated.

All sources must be formatted with `clang-format` before every commit.
```bash
# Run from the build directory you already created
cmake --build build-<arch> --target clang-format        # rewrites files in-place
cmake --build build-<arch> --target check-format        # check format 
```
