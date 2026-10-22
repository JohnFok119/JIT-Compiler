# Benchmarks

How fast does each backend compile an expression, and how fast does the compiled code run? Every number below comes from `docs/bench/results.csv`, which `bench/run.sh` writes (`make bench` runs it).

![Compile time and time per evaluation, log scale](bench/chart.svg)

## Method

- **Command:** `taskset -c 2 build/jit --bench 1000000 '<expr>'` for each expression in `bench/suite.txt`. `bench/run.sh` builds, runs all four backends, and writes `results.csv` and `machine.txt`. Regenerate the chart with `node scripts/bench-chart.mjs`.
- **Pinned:** the process runs on one CPU core (`taskset -c 2`). It is a laptop running WSL2, not an isolated benchmark machine, so see "Noise" below.
- **Backends:** `interp-O2` (the interpreter), `x86-O0` and `x86-O2` (the hand-written x86-64 backend without and with the optimizer), `gcc` (the C backend, optimized, compiled by `gcc` and loaded with `dlopen`).
- **Compile time:** median of 15 compiles, in microseconds. Each compile is timed from just before the optimizer (when the row has one) to just after `jit_compile` returns. Parsing, and freeing the previous result, are not timed. For `gcc` this includes starting the `gcc` process and loading the library, which is why it is slow.
- **Time per evaluation:** median of 11 timed runs, each of N = 1 000 000 calls, in nanoseconds per call. One extra untimed run comes first as a warm-up. The inputs are 1024 pre-generated variable vectors (values between 0.1 and 2.1), reused in turn, and every result is added into a `volatile` sink so the calls cannot be dropped. The native rows call the raw pointer from `jit_entry`; the interpreter has none, so it goes through `jit_call`, which adds a small wrapper cost to its figure.
- **Clock:** `clock_gettime(CLOCK_MONOTONIC)`, which counts in nanoseconds; the interpreter's compile times of a microsecond or so are only a few timer ticks.
- **Not timed:** parsing, the warm-up run, memory freeing, process start-up.

## Expressions

Two groups. `tiny`, `horner` and `arith` call no library functions, so they mostly measure the evaluator itself. The others call libm (`sin`, `exp`, `pow`, ...), so a large part of their time is spent inside glibc, which every backend shares.

| name | what |
| --- | --- |
| tiny | `x*2+1` |
| horner | degree-8 polynomial in nested (Horner) form; only `*`, `+`, `-`; no calls |
| arith | hand-written, 4 variables, only `+ - * /`, `abs`, `min`, `max`, `relu`; 90 IR lines; no calls; finite on the bench inputs |
| poly | degree-8 polynomial in plain powers (`x^8` stays a `pow` call); libm-bound |
| trig | `sin(x)*cos(y) + exp(-x*x) * tanh(y)`; libm-bound |
| big | random expression from `tests/fuzz/gen.c` (seed 42, depth 19, 3 variables): 272 IR lines before the optimizer, 216 after -O2. It contains `1e999` and other extreme constants, so some results are `inf` or `nan`, and it calls libm. |

## Results

| expr | backend | compile (us) | ns per evaluation |
| --- | --- | ---: | ---: |
| tiny | interp-O2 | 1.2 | 15.55 |
| tiny | x86-O0 | 18.0 | 2.75 |
| tiny | x86-O2 | 14.3 | 2.23 |
| tiny | gcc | 292159.1 | 3.06 |
| poly | interp-O2 | 4.2 | 214.73 |
| poly | x86-O0 | 75.4 | 123.12 |
| poly | x86-O2 | 165.3 | 187.61 |
| poly | gcc | 466842.1 | 156.86 |
| horner | interp-O2 | 2.2 | 94.06 |
| horner | x86-O0 | 74.9 | 4.83 |
| horner | x86-O2 | 39.6 | 5.07 |
| horner | gcc | 338443.6 | 4.67 |
| arith | interp-O2 | 4.9 | 259.18 |
| arith | x86-O0 | 128.7 | 13.37 |
| arith | x86-O2 | 235.8 | 13.77 |
| arith | gcc | 313344.9 | 13.58 |
| trig | interp-O2 | 0.8 | 97.83 |
| trig | x86-O0 | 33.3 | 64.16 |
| trig | x86-O2 | 32.4 | 61.75 |
| trig | gcc | 301390.8 | 60.28 |
| big | interp-O2 | 37.4 | 1702.85 |
| big | x86-O0 | 650.7 | 1176.50 |
| big | x86-O2 | 577.3 | 988.05 |
| big | gcc | 328338.3 | 918.04 |

## Machine

```
cpu: Intel(R) Core(TM) i9-10885H CPU @ 2.40GHz
gcc: gcc (Ubuntu 9.4.0-1ubuntu1~20.04.1) 9.4.0
kernel: Linux 6.18.54.1-microsoft-standard-WSL2
pinned: taskset -c 2
evaluations per run: 1000000
```

## Noise

The suite was run twice, back to back; the table above is the second run. The first run is not committed. Difference of the second run against the first:

| expr | backend | compile | ns per evaluation |
| --- | --- | ---: | ---: |
| tiny | interp-O2 | +100.0 % | +18.2 % |
| tiny | x86-O0 | +17.6 % | +25.0 % |
| tiny | x86-O2 | -41.6 % | -0.9 % |
| tiny | gcc | +12.3 % | +45.7 % |
| poly | interp-O2 | +100.0 % | +15.8 % |
| poly | x86-O0 | +12.2 % | +8.1 % |
| poly | x86-O2 | +154.7 % | +74.6 % |
| poly | gcc | +76.0 % | +64.4 % |
| horner | interp-O2 | +100.0 % | +40.9 % |
| horner | x86-O0 | +123.6 % | +9.8 % |
| horner | x86-O2 | +4.2 % | +3.9 % |
| horner | gcc | +26.0 % | +1.7 % |
| arith | interp-O2 | +0.0 % | +9.5 % |
| arith | x86-O0 | +5.5 % | +17.0 % |
| arith | x86-O2 | +103.3 % | +22.6 % |
| arith | gcc | +0.3 % | -15.0 % |
| trig | interp-O2 | +14.3 % | +12.3 % |
| trig | x86-O0 | +20.2 % | +21.7 % |
| trig | x86-O2 | +18.7 % | +14.7 % |
| trig | gcc | -5.2 % | +3.1 % |
| big | interp-O2 | -2.1 % | +7.3 % |
| big | x86-O0 | +17.9 % | +14.4 % |
| big | x86-O2 | +12.9 % | +10.4 % |
| big | gcc | -2.1 % | +3.0 % |

The target was 10 % or less. It was **not met** for most compile times and many evaluation times (more than 10 % apart: 17 of 24 compile rows, 15 of 24 evaluation rows). The laptop was shared with other work, and the second run was slower overall. Compile times of the interpreter are a microsecond or two, so one timer tick moves them by tens of percent. Treat every individual row as uncertain by tens of percent (the worst moved by 155 %), and trust only differences that are much larger than that and that appear in both runs.

## Reading the results

Only what both runs support:

- **The x86 backend compiles in microseconds; the gcc backend compiles in hundreds of milliseconds.** gcc took 260 000 to 470 000 us in every row (a quarter to half a second); the x86 backend took 14 to 651 us. Even comparing the slowest x86 compile with the fastest gcc compile, gcc is roughly 400 times slower or more. Almost all of that is the cost of starting `gcc`.
- **The interpreter compiles almost instantly** (under 40 us on every row, about 5 us or less except for `big`) **but it is the slowest to run.**
- **Where the work is plain arithmetic, the interpreter is much slower than compiled code.** On `horner` it took 67 to 94 ns per evaluation against 4.4 to 5.1 ns for the x86 backend, about 14 to 20 times slower. On `arith` it took 237 to 259 ns against 11 to 14 ns for x86, about 19 to 21 times slower. This is the per-node dispatch cost that compiling removes. (The interpreter figure also includes the `jit_call` wrapper.)
- **Where libm dominates, the gap is small.** On `poly`, `trig` and `big`, the interpreter was about 1.2 to 1.8 times slower than x86-O2 (`trig`: 87 to 98 vs 54 to 62 ns). Most of the time is inside glibc for everyone, so these three rows say little about the JIT itself.
- **The gcc backend's code is not shown to be faster than the x86 backend's.** On `horner` they are within 0.5 ns of each other in both runs. On `arith` and the libm-bound expressions the order flips between runs or is inside the noise.
- **-O2 shows no consistent benefit over -O0 for the x86 backend** in evaluation time, and compile time is noisy. These numbers neither show that the optimizer helps nor that it hurts on these expressions.
- **`tiny`** takes 2 to 3 ns native, close to the cost of the timing loop, so differences among native backends there mean nothing.

What this does not tell you: behaviour on other CPUs, with other value ranges (the extreme constants in `big` may take slow paths), or on an unloaded machine.
