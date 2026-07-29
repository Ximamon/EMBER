# Benchmark protocol

Build EMBER in Release mode and keep the simulator configuration, seed and machine load stable. Grid export must remain disabled. Record compiler/version, operating system, CPU model, logical/physical core count and relevant power settings alongside results.

The initial matrix is:

| Grid | Maximum steps | Scenarios |
|---|---:|---:|
| 256x256 | 250 | 1, 20 |
| 512x512 | 500 | 1, 20 |
| 1024x1024 | 500 | 1, 10 |
| 2048x2048 | 250 | 1, 5 |

`scripts/run_benchmarks.py` performs one unrecorded warm-up and five recorded repetitions per row. Keep raw repetitions and use their median for comparisons. Because fires may extinguish early, compare both elapsed time and the reported cell-update throughput.

Do not enable `fast-math` for the baseline. Future OpenMP/CUDA reports should include speedup against the same sequential executable and verify non-timing results before measuring.
