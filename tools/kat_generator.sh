#!/bin/sh

CURRENT_DIR=$(pwd)

# ref
rm -rf build-ref
cmake -S . -B build-ref -DCMAKE_BUILD_TYPE=Release -DHQC_ARCH=ref
cmake --build build-ref
make

$CURRENT_DIR/build-ref/tests/kats/test_kat_hqc_1
mv $CURRENT_DIR/PQCkemKAT_32.req $CURRENT_DIR/kats/ref/hqc-1/
mv $CURRENT_DIR/PQCkemKAT_32.rsp $CURRENT_DIR/kats/ref/hqc-1/

$CURRENT_DIR/build-ref/tests/kats/test_kat_hqc_3
mv $CURRENT_DIR/PQCkemKAT_32.req $CURRENT_DIR/kats/ref/hqc-3/
mv $CURRENT_DIR/PQCkemKAT_32.rsp $CURRENT_DIR/kats/ref/hqc-3/

$CURRENT_DIR/build-ref/tests/kats/test_kat_hqc_5
mv $CURRENT_DIR/PQCkemKAT_32.req $CURRENT_DIR/kats/ref/hqc-5/
mv $CURRENT_DIR/PQCkemKAT_32.rsp $CURRENT_DIR/kats/ref/hqc-5/

$CURRENT_DIR/build-ref/tests/kats/test_intermediates_values_hqc_1 > $CURRENT_DIR/kats/ref/hqc-1/intermediates_values
$CURRENT_DIR/build-ref/tests/kats/test_intermediates_values_hqc_3 > $CURRENT_DIR/kats/ref/hqc-3/intermediates_values
$CURRENT_DIR/build-ref/tests/kats/test_intermediates_values_hqc_5 > $CURRENT_DIR/kats/ref/hqc-5/intermediates_values

# x86_64
rm -rf build-avx256
cmake -S . -B build-avx256 -DCMAKE_BUILD_TYPE=Release -DHQC_ARCH=x86_64 -DHQC_X86_IMPL=avx256
cmake --build build-avx256
make

$CURRENT_DIR/build-avx256/tests/kats/test_kat_hqc_1
mv $CURRENT_DIR/PQCkemKAT_32.req $CURRENT_DIR/kats/x86_64/avx256/hqc-1/
mv $CURRENT_DIR/PQCkemKAT_32.rsp $CURRENT_DIR/kats/x86_64/avx256/hqc-1/

$CURRENT_DIR/build-avx256/tests/kats/test_kat_hqc_3
mv $CURRENT_DIR/PQCkemKAT_32.req $CURRENT_DIR/kats/x86_64/avx256/hqc-3/
mv $CURRENT_DIR/PQCkemKAT_32.rsp $CURRENT_DIR/kats/x86_64/avx256/hqc-3/

$CURRENT_DIR/build-avx256/tests/kats/test_kat_hqc_5
mv $CURRENT_DIR/PQCkemKAT_32.req $CURRENT_DIR/kats/x86_64/avx256/hqc-5/
mv $CURRENT_DIR/PQCkemKAT_32.rsp $CURRENT_DIR/kats/x86_64/avx256/hqc-5/

$CURRENT_DIR/build-avx256/tests/kats/test_intermediates_values_hqc_1 > $CURRENT_DIR/kats/x86_64/avx256/hqc-1/intermediates_values
$CURRENT_DIR/build-avx256/tests/kats/test_intermediates_values_hqc_3 > $CURRENT_DIR/kats/x86_64/avx256/hqc-3/intermediates_values
$CURRENT_DIR/build-avx256/tests/kats/test_intermediates_values_hqc_5 > $CURRENT_DIR/kats/x86_64/avx256/hqc-5/intermediates_values
