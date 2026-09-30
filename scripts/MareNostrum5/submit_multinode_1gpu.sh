#!/bin/bash
#SBATCH --job-name=ember_mn5_multinode_1gpu
#SBATCH --account=nct_394
#SBATCH --qos=acc_training
#SBATCH --time=00:20:00
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=20
#SBATCH --gres=gpu:1
#SBATCH --output=ember_multinode_1gpu_%j.out
#SBATCH --error=ember_multinode_1gpu_%j.err

echo "=================================================="
echo "MN5 ACC - DISTRIBUTED MULTI-NODE (1 GPU H100 PER NODE)"
echo "Job ID:         $SLURM_JOB_ID"
echo "Assigned nodes:"
scontrol show hostnames $SLURM_JOB_NODELIST
echo "Account / QoS:  $SLURM_JOB_ACCOUNT / $SLURM_JOB_QOS"
echo "CPUs per task:  ${SLURM_CPUS_PER_TASK}"
echo "Date:           $(date)"
echo "=================================================="

# 1. Load official module stack for MPI + CUDA on MN5 ACC
module purge
module load gcc
module load cmake
module load cuda
module load openmpi

# 2. Path definitions
PROJECT_DIR="${HOME}/EMBER"
BUILD_DIR="${PROJECT_DIR}/build"
OUTPUT_DIR="${SCRATCH:-/gpfs/scratch/nct_394/${USER}}/results/mn5_multinode_1gpu"

mkdir -p "${OUTPUT_DIR}"

# 3. Log management: automatically copy .out and .err to results directory
copy_logs() {
    echo "=================================================="
    echo "Copying Slurm logs (.out and .err) to ${OUTPUT_DIR}..."
    sync
    sleep 1
    SUBMIT_DIR="${SLURM_SUBMIT_DIR:-${PROJECT_DIR}}"
    cp "${SUBMIT_DIR}/ember_multinode_1gpu_${SLURM_JOB_ID}.out" "${OUTPUT_DIR}/" 2>/dev/null || cp "ember_multinode_1gpu_${SLURM_JOB_ID}.out" "${OUTPUT_DIR}/" 2>/dev/null || true
    cp "${SUBMIT_DIR}/ember_multinode_1gpu_${SLURM_JOB_ID}.err" "${OUTPUT_DIR}/" 2>/dev/null || cp "ember_multinode_1gpu_${SLURM_JOB_ID}.err" "${OUTPUT_DIR}/" 2>/dev/null || true
    echo "Logs successfully copied."
}
trap copy_logs EXIT

# ==============================================================================
# PREPARATION PHASE: CLEAN, CONFIGURE AND COMPILE (MPI + CUDA sm_90)
# ==============================================================================
echo ">>> [1/3] Cleaning previous build..."
rm -rf "${BUILD_DIR}"

echo ">>> [2/3] Configuring CMake for MPI + NVIDIA Hopper (sm_90)..."
cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DEMBER_ENABLE_CUDA=ON \
    -DEMBER_ENABLE_MPI=ON \
    -DCMAKE_CUDA_ARCHITECTURES=90

if [ $? -ne 0 ]; then
    echo "ERROR: CMake configuration failed."
    exit 1
fi

echo ">>> [3/3] Compiling with ${SLURM_CPUS_PER_TASK} cores..."
cmake --build "${BUILD_DIR}" -j ${SLURM_CPUS_PER_TASK}

if [ $? -ne 0 ]; then
    echo "ERROR: Project build failed."
    exit 1
fi

EMBER_BIN="${BUILD_DIR}/ember"

if [ ! -f "${EMBER_BIN}" ]; then
    echo "ERROR: Binary was not generated at ${EMBER_BIN}"
    exit 1
fi

echo "=================================================="
echo "Build succeeded. GPU information on each node:"
srun --ntasks-per-node=1 nvidia-smi --query-gpu=index,name,memory.total,driver_version --format=csv
echo "=================================================="

# ==============================================================================
# EXECUTION PHASE (MPI + CUDA MULTI-NODE)
# ==============================================================================
export SRUN_CPUS_PER_TASK=${SLURM_CPUS_PER_TASK}

# Based on benchmarking suite: 2048x2048 grid with 80 distributed scenarios
srun --cpus-per-task=${SLURM_CPUS_PER_TASK} \
    "${EMBER_BIN}" \
    --width 2048 \
    --height 2048 \
    --steps 2048 \
    --scenarios 80 \
    --seed 42 \
    --wind-direction 45 \
    --wind-strength 0.8 \
    --base-spread 0.80 \
    --export none \
    --output "${OUTPUT_DIR}"

echo "=================================================="
echo "Simulation finished. Results saved in: ${OUTPUT_DIR}"
