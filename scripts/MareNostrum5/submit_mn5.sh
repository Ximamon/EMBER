#!/bin/bash
#SBATCH --job-name=ember_mn5_cpu
#SBATCH --account=nct_394
#SBATCH --qos=gp_training
#SBATCH --time=01:00:00
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --output=ember_cpu_%j.out
#SBATCH --error=ember_cpu_%j.err

echo "=================================================="
echo "MN5 GPP - CPU BUILD AND EXECUTION"
echo "Job ID:         $SLURM_JOB_ID"
echo "Assigned node:  $(hostname)"
echo "Account / QoS:  $SLURM_JOB_ACCOUNT / $SLURM_JOB_QOS"
echo "CPUs allocated: ${SLURM_CPUS_PER_TASK}"
echo "Date:           $(date)"
echo "=================================================="

# 1. Load official module environment for CPU on MN5
module purge
module load gcc/13.2.0
module load cmake/3.30.5

# 2. Environment variables setup for OpenMP and Slurm
export OMP_NUM_THREADS=${SLURM_CPUS_PER_TASK}
export SRUN_CPUS_PER_TASK=${SLURM_CPUS_PER_TASK}

# 3. Path definitions (code in $HOME, outputs in $SCRATCH)
PROJECT_DIR="${HOME}/EMBER"
BUILD_DIR="${PROJECT_DIR}/build_cpu"
OUTPUT_DIR="${SCRATCH:-/gpfs/scratch/nct_394/${USER}}/results/mn5_cpu"
mkdir -p "${OUTPUT_DIR}"
 
# 4. Log management: automatically copy .out and .err to results directory
copy_logs() {
    echo "=================================================="
    echo "Copying Slurm logs (.out and .err) to ${OUTPUT_DIR}..."
    sync
    sleep 1
    SUBMIT_DIR="${SLURM_SUBMIT_DIR:-${PROJECT_DIR}}"
    cp "${SUBMIT_DIR}/ember_cpu_${SLURM_JOB_ID}.out" "${OUTPUT_DIR}/" 2>/dev/null || cp "ember_cpu_${SLURM_JOB_ID}.out" "${OUTPUT_DIR}/" 2>/dev/null || true
    cp "${SUBMIT_DIR}/ember_cpu_${SLURM_JOB_ID}.err" "${OUTPUT_DIR}/" 2>/dev/null || cp "ember_cpu_${SLURM_JOB_ID}.err" "${OUTPUT_DIR}/" 2>/dev/null || true
    echo "Logs successfully copied."
}
trap copy_logs EXIT

# ==============================================================================
# PREPARATION PHASE: CLEAN, CONFIGURE AND COMPILE ON COMPUTE NODE
# ==============================================================================
echo ">>> [1/3] Cleaning previous build..."
rm -rf "${BUILD_DIR}"

echo ">>> [2/3] Configuring CMake for CPU (Release)..."
cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DEMBER_ENABLE_CUDA=OFF \
    -DEMBER_ENABLE_NVTX=OFF \
    -DEMBER_ENABLE_MPI=OFF

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
    echo "ERROR: Binary not found at ${EMBER_BIN}"
    exit 1
fi

echo "=================================================="
echo "Build succeeded. CPU information on compute node:"
lscpu | grep "Model name\|CPU(s):\|Thread(s) per core:"
echo "=================================================="

# ==============================================================================
# EXECUTION PHASE (EMBER BASELINE CPU)
# ==============================================================================
srun --cpus-per-task=${SLURM_CPUS_PER_TASK} \
    "${EMBER_BIN}" \
    --width 2048 \
    --height 2048 \
    --steps 2048 \
    --scenarios 80 \
    --seed 42 \
    --wind-direction 45 \
    --wind-strength 0.4 \
    --base-spread 0.25 \
    --export none \
    --output "${OUTPUT_DIR}"

echo "=================================================="
echo "Simulation finished. Results saved in: ${OUTPUT_DIR}"