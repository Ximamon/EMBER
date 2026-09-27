# CUDA scenario preparation experiment

The CUDA runner accepts `--cuda-init cpu|openmp|gpu` (default `gpu` in CUDA
builds, `cpu` in CPU-only builds). Every mode
reuses one set of device buffers per process for the entire scenario batch. `cpu`
retains the original synthetic input generator. `openmp` runs the same per-cell
generator with OpenMP. `gpu` generates the input on the device using the CPU
seed/hash algorithm; select it only after exact input and final-grid checks.
`openmp` requires a build where CMake found OpenMP.

On a node with CMake, submit `sbatch scripts/submit_cuda_initializers.sh`.
The job builds a fresh Release CUDA executable and the pre-change revision
`94bd953`, then checks exact output for several
seeds, sizes and repeated scenarios, then measures the 256² and 1024² cases.
Each mode gets one cold invocation, one warm-up invocation and five independent
measured invocations. `report.json` contains all process wall times, hardware and
revision metadata. The selected mode must be at least 5% faster than `cpu` by
the median **whole-process batch time**; otherwise keep `cpu`. The job also
records an NSYS trace of the selected mode separately from the timed runs.
On the Axis cluster, the A100 compute image has no CMake. Compile both
revisions on its login node and submit with absolute paths in
`EMBER_PREBUILT_EXECUTABLE` and `EMBER_BASELINE_EXECUTABLE`. The job then runs
all verification, measurements and profiling on the allocated A100.

`--verify-cuda-init` copies the GPU-generated initial arrays back and compares
them byte for byte with CPU before the fire kernel runs. It is for correctness
checks only and must not be used for performance runs. The verification script
also compares exported final grids across all three modes. A candidate with
different results is excluded from timing and selection. A mismatch against
the pre-change baseline or a missing reference scenario fails the job.

The appended CSV columns split host initialization, GPU allocation, host-to-device
copy, GPU initialization, device-to-host copy and scenario wall time. On CUDA,
`initialization_seconds` is the sum of the preparation phases,
`simulation_seconds` remains the timed stencil loop, and `total_core_seconds`
for the batch includes startup and release. `cuda_startup_seconds` and
`cuda_release_seconds` are one-time batch costs. A scenario's wall time includes
its result counting and optional export; the benchmark disables export.
`nsys` and `ncu` add profiling overhead and are never used for the speedup
decision.

## A100 result, 2026-09-27

Slurm job `4089` ran on `gpu003` (NVIDIA A100-SXM4-80GB, driver 580.95.05).
The measured code was a snapshot of the local worktree based on commit
`94bd953b60a58006565ef963cd5216ca82beaa23`, with SHA-256
`92fab4ec8d530430ccbfac625dbdaa95970f5ddad189b838993fcdabe0721337`
for the transferred source tar. The old baseline was exactly that commit.
`report.json` records `e536b681` as its Git revision because the isolated
remote clone received the source snapshot as an overlay; that field is the
clone's original HEAD, not the measured source revision. Both executables used
GCC 14.2.0, CUDA Toolkit 12.8.1, Release mode, one MPI task, eight CPU cores,
`OMP_NUM_THREADS=8`, and one A100. Each variant had one cold invocation, one
warm-up and five separate measured invocations without grid export.

| Grid, steps, scenarios | Old baseline | CPU initializer | OpenMP initializer | GPU initializer |
| --- | ---: | ---: | ---: | ---: |
| 256 × 256, 100, 20 | 0.519 s | 0.623 s | 0.689 s | **0.587 s** |
| 1024 × 1024, 1024, 20 | 1.819 s | 2.120 s | 1.322 s | **1.221 s** |

Values are medians of whole-process batch wall time. GPU is 42.4% faster
than the new CPU initializer and 32.8% faster than the old baseline for the
target A100 case. It exceeds the 5% selection threshold for both measured
sizes, so CUDA builds now default to GPU initialization. The small case is
13.1% slower than the old baseline despite GPU being the fastest new mode;
startup and fixed overhead dominate that workload. The five individual
measurements and cold startup timings are in
[`report.json`](../results/cuda-initializers-a100-4089/report.json).

For three workloads with different seeds, dimensions and repeated scenarios,
all modes produced byte-identical final grids; GPU initial states and fields
also matched CPU byte for byte. The full A100 case verified all 20 GPU-generated
inputs against CPU. In the separately profiled run, the CSV has 20 scenario
rows and one GPU allocation; each scenario's initialization time equals its
recorded preparation phases, and its wall time covers those phases, kernel
steps and device-to-host copy. The NVTX summary shows one `cuda.startup`, one
`cuda.allocate`, 20 `cuda.initialize_synthetic`, 20 `cuda.timesteps`, 20
`cuda.device_to_host`, and cleanup ranges. The trace was excluded from the
speedup calculation. See
[`nvtx_summary.txt`](../results/cuda-initializers-a100-4089/nvtx_summary.txt)
and [`nsys_summary.csv`](../results/cuda-initializers-a100-4089/nsys_summary.csv).
