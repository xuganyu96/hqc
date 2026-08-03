# Tools

This directory contains small C utilities used to inspect and validate HQC
building blocks.

## Files

- `rs/main.c`: command-line entry point for the Reed-Solomon tool
- `rs/constants.c`, `rs/constants.h`: Reed-Solomon parameters, generation logic, packing, and printing
- `rs/verify.c`, `rs/verify.h`: verification against Reed-Solomon constants stored in the repository

## Build Reed-Solomon Tool

Run from the repository root:

```bash
cmake -S . -B build -DHQC_BUILD_TOOLS=ON
cmake --build build --target rs_constants
```

## Commands

```bash
./build/tools/rs/rs_constants gf-256-field-info
./build/tools/rs/rs_constants generate_reed_solomon_constants hqc-1
./build/tools/rs/rs_constants verify-repo hqc-1
```

Supported parameter sets: `hqc-1`, `hqc-3`, `hqc-5`.

`generate_reed_solomon_constants hqc-X` prints `RS_POLY_COEFS`,
`alpha_ij_pow`, `alpha_ij256_*`, and `param256`, with comments showing the
destination file for each block.

`verify-repo hqc-X` checks the generated Reed-Solomon constants against the
repository files under `src/ref` and `src/x86_64`.
