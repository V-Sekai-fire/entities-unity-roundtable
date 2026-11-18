#!/bin/sh
# Build the guests and the harness. Needs the riscv64-sysroot pixi env for clang+lld, and checkouts of WAMR and libriscv (UPSTREAM).
set -e
cd "$(dirname "$0")/.."
: "${SYSROOT_BIN:?set SYSROOT_BIN=<repository-riscv64-sysroot>/.pixi/envs/default/bin}"
: "${WAMR_ROOT:?}" "${LIBRISCV_ROOT:?}"
mkdir -p build
"$SYSROOT_BIN/clang" --target=wasm32 -O2 -ffp-contract=off -nostdlib -Wl,--no-entry -Wl,--allow-undefined -Wl,--export-dynamic -fuse-ld=lld guests/bench.c -o build/bench.wasm
"$SYSROOT_BIN/clang" --target=riscv64-unknown-elf -march=rv64imafdc -mabi=lp64d -O2 -ffp-contract=off -fno-math-errno -nostdlib -static -fuse-ld=lld guests/bench.c -o build/bench.elf
cmake -S . -B build/host -G Ninja -DWAMR_ROOT="$WAMR_ROOT" -DLIBRISCV_ROOT="$LIBRISCV_ROOT"
cmake --build build/host
