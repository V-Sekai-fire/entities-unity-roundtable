# Results

Mac arm64, libriscv interpreter, WAMR fast interpreter, one run, host-array transforms, no Unity. Bits of the L-BFGS result equal on all three.

```
3 load+instantiate+first update    wamr     1.018583e-03 s  bench.wasm
3 load+instantiate+first update    libriscv 6.446292e-03 s  bench.elf
1 empty export x calls             native   1.179458e-03 s  control
1 empty export x calls             wamr     2.679864e-01 s  
1 empty export x calls             libriscv 4.108083e-02 s  
2 transform read+write             native   3.904200e-05 s  control
2 transform read+write             wamr     1.556667e-03 s  
2 transform read+write             libriscv 9.579170e-04 s  
4 L-BFGS saddle x iters            native   2.475676e-01 s  control
4 L-BFGS saddle x iters            wamr     4.020040e+00 s  
4 L-BFGS saddle x iters            libriscv 1.258898e+01 s  
   residual sum native 1.8151297802299044e-13, bits equal: wamr yes libriscv yes
RESULT: PASS
```
