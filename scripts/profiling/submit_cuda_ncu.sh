#!/bin/bash
#SBATCH --partition=primary
#SBATCH --job-name=ember_cuda_ncu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --gres=gpu:1              # Reserve 1 GPU NVIDIA A100-SXM4
#SBATCH --time=00:15:00
#SBATCH --output=cuda_ncu_%j.out
#SBATCH --error=cuda_ncu_%j.err

# Load the necessary modules for CUDA and NCU
module load gcc/14.2.0
module load openmpi/gcc/64/5.0.7
module load cuda12.8/toolkit/12.8.1
module load Nsight-Compute/2026.1

echo "=================================================="
echo "CURIOSITY - CUDA NCU KERNEL PROFILING (NVIDIA A100)"
echo "Job ID: $SLURM_JOB_ID"
echo "Node: $(hostname)"
echo "Date: $(date)"
echo "Visible GPU devices according to Slurm:"
nvidia-smi --query-gpu=index,name,memory.total,driver_version --format=csv
echo "=================================================="

cd ~/EMBER
OUTPUT_DIR="results/profiling/cuda_ncu"
mkdir -p "${OUTPUT_DIR}"

# Log management: automatically copy .out and .err to results directory
copy_logs() {
    echo "=================================================="
    echo "Copying Slurm logs (.out and .err) to ${OUTPUT_DIR}..."
    sync
    sleep 1
    SUBMIT_DIR="${SLURM_SUBMIT_DIR:-.}"
    cp "${SUBMIT_DIR}/cuda_ncu_${SLURM_JOB_ID}.out" "${OUTPUT_DIR}/" 2>/dev/null || cp "cuda_ncu_${SLURM_JOB_ID}.out" "${OUTPUT_DIR}/" 2>/dev/null || true
    cp "${SUBMIT_DIR}/cuda_ncu_${SLURM_JOB_ID}.err" "${OUTPUT_DIR}/" 2>/dev/null || cp "cuda_ncu_${SLURM_JOB_ID}.err" "${OUTPUT_DIR}/" 2>/dev/null || true
    echo "Logs successfully copied."
}
trap copy_logs EXIT

# Profile 5 kernel executions of step_stencil_kernel with full hardware metrics
ncu --import-source=yes --clock-control=none -k step_stencil_kernel -c 5 \
    -o "${OUTPUT_DIR}/ember_cuda_profile" --set=full -f \
./build/ember \
    --width 1024 \
    --height 1024 \
    --steps 20 \
    --scenarios 8 \
    --seed 42 \
    --base-spread 0.80 \
    --wind-strength 0.8 \
    --wind-direction 45 \
    --export none \
    --output "${OUTPUT_DIR}/"

echo "=================================================="
echo "NCU kernel profiling completed successfully on NVIDIA A100 GPU."