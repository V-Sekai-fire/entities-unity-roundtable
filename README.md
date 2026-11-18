# entities-unity-roundtable

WAMR against libriscv (the VM under godot-sandbox) for Unity, on four problems. One C source, `guests/bench.c`, builds to wasm32, riscv64 and the host, so the host build is the control.

| # | Problem | Export |
|---|---------|--------|
| 1 | Call an exported empty function 1,000,000 times | `empty` |
| 2 | 10,000 transform reads and writes through the host API (3 reads + 3 writes each) | `transforms(n)` |
| 3 | Load a module, instantiate it, run its first update | `update` |
| 4 | 1,000,000 L-BFGS iterations on a saddle-point problem (n = 16, m = 5) | `lbfgs_saddle(iters)` |

Problem 4 finds the saddle of h(x,y) = ½xᵀAx − ½yᵀBy + xᵀCy by minimising |∇h|², restarting from a fixed pseudo-random start when a solve converges. Every VM must return the same residual bits as the host build.

## Build and run

```sh
SYSROOT_BIN=<repository-riscv64-sysroot>/.pixi/envs/default/bin WAMR_ROOT=<wasm-micro-runtime> LIBRISCV_ROOT=<libriscv> tools/build.sh
build/host/rtbench_cli build/bench.wasm build/bench.elf [calls] [transforms] [iters] [usd.elf]
```

Pinned upstreams are in `UPSTREAM`. `librtbench` is the shared library Unity P/Invokes (`native/rt.h`).

## Measured (Mac arm64, interpreters, seconds)

See `docs/RESULTS.md`.

## Not done yet

- Unity project and C# transform callbacks (problem 2 here uses a host array).
- Problem 3 with `interactor-usd-guest`'s `usd.elf`: it needs the sandbox API host, and there is no wasm build of OpenUSD for WAMR. `contract-tris-to-quads-guest` has no ELF yet.
- libriscv binary translation and WAMR AOT.
