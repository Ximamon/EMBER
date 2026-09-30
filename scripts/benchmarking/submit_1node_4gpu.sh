#!/bin/bash
#SBATCH --partition=primary
#SBATCH --job-name=ember_1node_4gpu
#SBATCH --nodes=1              # Single physical DGX node
#SBATCH --ntasks=4             # 4 parallel MPI processes
#SBATCH --cpus-per-task=8      # 8 CPU cores dedicated to each process
#SBATCH --gres=gpu:4           # 4 NVIDIA A100 GPUs reserved
#SBATCH --time=00:20:00
#SBATCH --output=1node_4gpu_%j.out
#SBATCH --error=1node_4gpu_%j.err

# 1. Load verified modules on Curiosity
module load gcc/14.2.0
module load openmpi/gcc/64/5.0.7
module load cuda12.8/toolkit/12.8.1

echo "=================================================="
echo "CURIOSITY - INTRA-NODE BENCHMARK (4 MPI RANKS x 4 A100 GPUs)"
echo "Job ID:         $SLURM_JOB_ID"
echo "Assigned node:  $(hostname)"
echo "Visible GPUs according to nvidia-smi:"
nvidia-smi --query-gpu=index,name,pci.bus_id --format=csv
echo "=================================================="

cd ~/EMBER
OUTPUT_DIR="results/benchmarking/4gpu"
mkdir -p "${OUTPUT_DIR}"

# Log management: automatically copy .out and .err to results directory
copy_logs() {
    echo "=================================================="
    echo "Copying Slurm logs (.out and .err) to ${OUTPUT_DIR}..."
    sync
    sleep 1
    SUBMIT_DIR="${SLURM_SUBMIT_DIR:-.}"
    cp "${SUBMIT_DIR}/1node_4gpu_${SLURM_JOB_ID}.out" "${OUTPUT_DIR}/" 2>/dev/null || cp "1node_4gpu_${SLURM_JOB_ID}.out" "${OUTPUT_DIR}/" 2>/dev/null || true
    cp "${SUBMIT_DIR}/1node_4gpu_${SLURM_JOB_ID}.err" "${OUTPUT_DIR}/" 2>/dev/null || cp "1node_4gpu_${SLURM_JOB_ID}.err" "${OUTPUT_DIR}/" 2>/dev/null || true
    echo "Logs successfully copied."
}
trap copy_logs EXIT

# 2. Launch with mpirun (shared memory communication via 'sm/vader')
mpirun -np 4 \
  --bind-to core \
  ./build/ember \
    --width 2048 \
    --height 2048 \
    --steps 2048 \
    --scenarios 80 \
    --seed 42 \
    --base-spread 0.80 \
    --wind-strength 0.8 \
    --wind-direction 45 \
    --export none \
    --output "${OUTPUT_DIR}/"

echo "=================================================="
echo "Single-node 4-GPU benchmark completed successfully."