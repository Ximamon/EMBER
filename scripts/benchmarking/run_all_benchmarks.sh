#!/bin/bash
# Master orchestrator for EMBER benchmarking suite (Clean runs without profiling overhead)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "=========================================================="
echo "          EMBER HPC BENCHMARKING SUITE LAUNCHER          "
echo "=========================================================="
echo "Project Root: $PROJECT_ROOT"
echo "Mode: Quantitative Benchmarking (Zero NSYS overhead)"
echo "=========================================================="

mkdir -p "$PROJECT_ROOT/results/benchmarking/santabarbara1"
mkdir -p "$PROJECT_ROOT/results/benchmarking/curiosity"
mkdir -p "$PROJECT_ROOT/results/benchmarking/cuda"
mkdir -p "$PROJECT_ROOT/results/benchmarking/mpi"
mkdir -p "$PROJECT_ROOT/results/benchmarking/mpi_cuda"

usage() {
    echo "Usage: $0 [all|cpu|cuda|mpi|mpi-cuda]"
    echo ""
    echo "Targets:"
    echo "  cpu       Submit Curiosity single-node CPU benchmark (submit_curiosity.sh)"
    echo "  cuda      Submit Curiosity single-GPU A100 benchmark (submit_cuda.sh)"
    echo "  mpi       Submit Curiosity 4-node MPI CPU benchmark (submit_mpi.sh)"
    echo "  mpi-cuda  Submit Curiosity 4-node MPI + CUDA benchmark (submit_mpi_cuda.sh)"
    echo "  all       Submit all Curiosity benchmarks in sequence"
    echo ""
    exit 1
}

TARGET="${1:-all}"

case "$TARGET" in
    cpu)
        echo "Submitting CPU benchmark..."
        sbatch "$SCRIPT_DIR/submit_curiosity.sh"
        ;;
    cuda)
        echo "Submitting CUDA GPU benchmark..."
        sbatch "$SCRIPT_DIR/submit_cuda.sh"
        ;;
    mpi)
        echo "Submitting MPI CPU benchmark (4 nodes)..."
        sbatch "$SCRIPT_DIR/submit_mpi.sh"
        ;;
    mpi-cuda)
        echo "Submitting MPI + CUDA benchmark (4 nodes, 4x A100)..."
        sbatch "$SCRIPT_DIR/submit_mpi_cuda.sh"
        ;;
    all)
        echo "Submitting complete Curiosity benchmarking battery..."
        sbatch "$SCRIPT_DIR/submit_curiosity.sh"
        sbatch "$SCRIPT_DIR/submit_cuda.sh"
        sbatch "$SCRIPT_DIR/submit_mpi.sh"
        sbatch "$SCRIPT_DIR/submit_mpi_cuda.sh"
        echo "All 4 Slurm benchmark jobs submitted."
        ;;
    *)
        usage
        ;;
esac
