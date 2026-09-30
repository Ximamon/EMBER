#!/bin/bash
#SBATCH --partition=primary
#SBATCH --job-name=ember_cuda_nsys
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --gres=gpu:1              # Reserve 1 GPU NVIDIA A100-SXM4
#SBATCH --time=00:05:00
#SBATCH --output=cuda_nsys_%j.out
#SBATCH --error=cuda_nsys_%j.err

# Load the necessary modules for CUDA and MPI
module load gcc/14.2.0
module load openmpi/gcc/64/5.0.7
module load cuda12.8/toolkit/12.8.1
module load Nsight-Systems/2026.2.1

echo "=================================================="
echo "CURIOSITY - SINGLE-GPU CUDA PROFILING (NSYS)"
echo "Job ID: $SLURM_JOB_ID"
echo "Node: $(hostname)"
echo "Date: $(date)"
echo "Visible GPU devices according to Slurm:"
nvidia-smi --query-gpu=index,name,memory.total,driver_version --format=csv
echo "=================================================="

cd ~/EMBER
OUTPUT_DIR="results/profiling/cuda_nsys"
mkdir -p "${OUTPUT_DIR}"

# Log management: automatically copy .out and .err to results directory
copy_logs() {
    echo "=================================================="
    echo "Copying Slurm logs (.out and .err) to ${OUTPUT_DIR}..."
    sync
    sleep 1
    SUBMIT_DIR="${SLURM_SUBMIT_DIR:-.}"
    cp "${SUBMIT_DIR}/cuda_nsys_${SLURM_JOB_ID}.out" "${OUTPUT_DIR}/" 2>/dev/null || cp "cuda_nsys_${SLURM_JOB_ID}.out" "${OUTPUT_DIR}/" 2>/dev/null || true
    cp "${SUBMIT_DIR}/cuda_nsys_${SLURM_JOB_ID}.err" "${OUTPUT_DIR}/" 2>/dev/null || cp "cuda_nsys_${SLURM_JOB_ID}.err" "${OUTPUT_DIR}/" 2>/dev/null || true
    echo "Logs successfully copied."
}
trap copy_logs EXIT

# Light workload (2 scenarios) to capture GPU timeline, pipeline overlap, and NVTX phases
nsys profile \
    --trace=cuda,nvtx,osrt \
    --sample=process-tree \
    --backtrace=dwarf \
    --cuda-memory-usage=true \
    --force-overwrite=true \
    -o "${OUTPUT_DIR}/ember_cuda_profile" \
./build/ember \
    --width 1024 \
    --height 1024 \
    --steps 1024 \
    --scenarios 8 \
    --seed 42 \
    --base-spread 0.80 \
    --wind-strength 0.8 \
    --wind-direction 45 \
    --export none \
    --output "${OUTPUT_DIR}/"

echo "=================================================="
echo "CUDA profiling completed successfully on NVIDIA A100 GPU."