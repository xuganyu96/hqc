### Tools

This directory contains small C utilities used to inspect and validate HQC
building blocks.

## Files

- `kat_generator.sh`: script to compute the KATs and update the `kats` folder

- `rs/main.c`: command-line entry point for the Reed-Solomon tool
- `rs/constants.c`, `rs/constants.h`: Reed-Solomon parameters, generation logic, packing, and printing
- `rs/verify.c`, `rs/verify.h`: verification against Reed-Solomon constants stored in the repository

## KAT generator

Run sh ./tools/`kat_generator.sh` script from the repository root directory to compile both ref and avx versions of the scheme, compute the KATs and update the `kats` folder

## Reed-Solomon Tools

# Build 

Run from the repository root:

```bash
cmake -S . -B build -DHQC_BUILD_TOOLS=ON
cmake --build build --target rs_constants
```

# Commands

```bash
./build/tools/rs/rs_constants gf-256-field-info
./build/tools/rs/rs_constants generate_reed_solomon_constants hqc-1
./build/tools/rs/rs_constants verify-repo hqc-1
```

Supported parameter sets: `hqc-1`, `hqc-3`, `hqc-5`.

`generate_reed_solomon_constants hqc-X` prints `RS_POLY_COEFS`,
`alpha_ij_pow`, `alpha_ij_vecs_*`, `rs_generator_vecs`, `eval_alpha_pow`, and
`beta_inv_pow_lanes`, with comments showing the destination file for each block.

`verify-repo hqc-X` checks the generated Reed-Solomon constants against the
repository files under `src/ref` and `src/x86_64`.
