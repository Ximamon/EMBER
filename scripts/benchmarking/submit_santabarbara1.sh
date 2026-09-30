#!/bin/bash
#SBATCH --job-name=ember_baseline
#SBATCH --partition=gpp
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --time=02:00:00
#SBATCH --output=baseline_%j.out
#SBATCH --error=baseline_%j.err
#SBATCH --chdir=/home/sb1user/EMBER

cd /home/sb1user/EMBER

echo "=================================================="
echo "SANTABARBARA1 - SEQUENTIAL CPU BASELINE BENCHMARK"
echo "Job ID: $SLURM_JOB_ID"
echo "Node: $(hostname)"
echo "Working directory: $(pwd)"
echo "Date: $(date)"
echo "=================================================="

OUTPUT_DIR="results/benchmarking/santabarbara1"
mkdir -p "${OUTPUT_DIR}"

# Log management: automatically copy .out and .err to results directory
copy_logs() {
    echo "=================================================="
    echo "Copying Slurm logs (.out and .err) to ${OUTPUT_DIR}..."
    sync
    sleep 1
    SUBMIT_DIR="${SLURM_SUBMIT_DIR:-/home/sb1user/EMBER}"
    cp "${SUBMIT_DIR}/baseline_${SLURM_JOB_ID}.out" "${OUTPUT_DIR}/" 2>/dev/null || cp "baseline_${SLURM_JOB_ID}.out" "${OUTPUT_DIR}/" 2>/dev/null || true
    cp "${SUBMIT_DIR}/baseline_${SLURM_JOB_ID}.err" "${OUTPUT_DIR}/" 2>/dev/null || cp "baseline_${SLURM_JOB_ID}.err" "${OUTPUT_DIR}/" 2>/dev/null || true
    echo "Logs successfully copied."
}
trap copy_logs EXIT

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
echo "Simulation completed successfully on Santa Barbara 1."