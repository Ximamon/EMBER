# EMBER Execution & HPC Scripts

This directory organizes the execution, benchmarking, and profiling scripts for the EMBER fire simulation engine across single-node workstations, the Santa Barbara 1 baseline server, and the Curiosity DGX A100 multi-node HPC cluster.

---

## Methodological Separation: Benchmarking vs. Profiling

In High-Performance Computing (HPC), **quantitative benchmarking** (measuring speedup, wall-clock time, and throughput) and **qualitative profiling** (inspecting hardware counters, timeline ranges, and kernel execution) are strictly segregated into two independent suites:

```
scripts/
├── benchmarking/                  # Clean executions (NO nsys overhead)
│   ├── run_all_benchmarks.sh     # Convenience orchestrator to submit all benchmark jobs
│   ├── submit_santabarbara1.sh   # Sequential baseline on Santa Barbara 1 (1 CPU core)
│   ├── submit_curiosity.sh       # Single-node multi-core CPU benchmark on Curiosity
│   ├── submit_cuda.sh            # Single-node NVIDIA A100 GPU benchmark
│   ├── submit_mpi.sh             # Distributed OpenMPI CPU benchmark (4 DGX nodes)
│   └── submit_mpi_cuda.sh        # Distributed OpenMPI + CUDA GPU benchmark (4x A100)
│
├── profiling/                     # Qualitative inspection with NSYS / NCU (Light workloads)
│   ├── submit_cuda_nsys.sh       # NVIDIA Nsight Systems timeline & NVTX trace (2 scenarios)
│   ├── submit_cuda_ncu.sh        # NVIDIA Nsight Compute stencil kernel deep-dive (5 launches)
│   ├── submit_curiosity_nsys.sh  # CPU runtime & thread activity trace with NSYS (2 scenarios)
│   ├── submit_mpi_nsys.sh        # Distributed MPI communication tracing with NSYS (4 scenarios)
│   ├── submit_mpi_cuda_nsys.sh   # Multi-GPU + MPI timeline overlap trace with NSYS (4 scenarios)
│   ├── submit_cuda_initializers.sh # VRAM synthetic input generation comparison
│   └── compare_cuda_initializers.py# Analysis script for CUDA initializer validation
│
└── run_benchmarks.py              # Automated parameter sweep benchmark driver (Python stdlib)
```

The output directory [`results/`](../results/) **strictly mirrors** this structure:
```
results/
├── benchmarking/
│   ├── santabarbara1/
│   ├── curiosity/
│   ├── cuda/
│   ├── mpi/
│   └── mpi_cuda/
└── profiling/
    ├── cuda_nsys/
    ├── cuda_ncu/
    ├── curiosity_nsys/
    ├── mpi_nsys/
    └── mpi_cuda_nsys/
```

---

## 1. Benchmarking Suite (`scripts/benchmarking/`)

> [!IMPORTANT]
> All scripts in this directory execute `./build/ember` or `mpirun ... ./build/ember` directly without profilers (`nsys`, `ncu`). This eliminates the observer effect, false network bottlenecks over NFS, and MPI process skew, ensuring pure and accurate wall-clock and throughput measurements.
> 
> **Standard HPC Benchmark Workload:** $2048 \times 2048$ grid, 2048 steps, 80 independent scenarios.

| Script | Platform / Target | Resources | Workload | Output Directory |
| :--- | :--- | :--- | :--- | :--- |
| [`submit_santabarbara1.sh`](benchmarking/submit_santabarbara1.sh) | Santa Barbara 1 | 1 CPU Core | $2048^2$, 2048 steps, 80 sc. | `results/benchmarking/santabarbara1/` |
| [`submit_curiosity.sh`](benchmarking/submit_curiosity.sh) | Curiosity Cluster | 1 Node (8 Cores) | $2048^2$, 2048 steps, 80 sc. | `results/benchmarking/curiosity/` |
| [`submit_cuda.sh`](benchmarking/submit_cuda.sh) | Curiosity Cluster | 1 Node, 1x A100 | $2048^2$, 2048 steps, 80 sc. | `results/benchmarking/cuda/` |
| [`submit_mpi.sh`](benchmarking/submit_mpi.sh) | Curiosity Cluster | 4 Nodes (4 ranks) | $2048^2$, 2048 steps, 80 sc. | `results/benchmarking/mpi/` |
| [`submit_mpi_cuda.sh`](benchmarking/submit_mpi_cuda.sh) | Curiosity Cluster | 4 Nodes, 4x A100 | $2048^2$, 2048 steps, 80 sc. | `results/benchmarking/mpi_cuda/` |

To submit all Curiosity benchmarks in a single command:
```bash
bash scripts/benchmarking/run_all_benchmarks.sh all
```

---

## 2. Profiling Suite (`scripts/profiling/`)

> [!TIP]
> Profiling scripts use NVIDIA Nsight Systems and Nsight Compute exclusively on **light workloads** (1 to 4 scenarios). This prevents NFS disk saturation, avoids massive multi-gigabyte trace files, and isolates timeline visualization and SM occupancy metrics for GUI analysis.

| Script | Tool | Focus | Workload | Output Directory |
| :--- | :--- | :--- | :--- | :--- |
| [`submit_cuda_nsys.sh`](profiling/submit_cuda_nsys.sh) | `nsys profile` | Kernel execution, H2D/D2H transfers, NVTX | 2 scenarios | `results/profiling/cuda_nsys/` |
| [`submit_cuda_ncu.sh`](profiling/submit_cuda_ncu.sh) | `ncu` | `step_stencil_kernel` memory & compute roofline | 5 launches | `results/profiling/cuda_ncu/` |
| [`submit_curiosity_nsys.sh`](profiling/submit_curiosity_nsys.sh) | `nsys profile` | CPU thread activity & NVTX ranges | 2 scenarios | `results/profiling/curiosity_nsys/` |
| [`submit_mpi_nsys.sh`](profiling/submit_mpi_nsys.sh) | `nsys profile` | MPI communication & collective bottlenecks | 4 scenarios (1/rank) | `results/profiling/mpi_nsys/` |
| [`submit_mpi_cuda_nsys.sh`](profiling/submit_mpi_cuda_nsys.sh) | `nsys profile` | Multi-GPU concurrency & pipeline overlap | 4 scenarios (1/GPU) | `results/profiling/mpi_cuda_nsys/` |
| [`submit_cuda_initializers.sh`](profiling/submit_cuda_initializers.sh) | `python3` + `nsys` | CPU vs OpenMP vs CUDA synthetic input generation | Grid sweep | `results/cuda_initializers_<jobid>/` |
