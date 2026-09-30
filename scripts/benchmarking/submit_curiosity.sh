#!/bin/bash
#SBATCH --partition=primary
#SBATCH --job-name=ember_cpu_curiosity
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --time=01:30:00
#SBATCH --output=cpu_curiosity_%j.out
#SBATCH --error=cpu_curiosity_%j.err

module load gcc/14.2.0

echo "=================================================="
echo "CURIOSITY - SINGLE-NODE CPU BENCHMARK"
echo "Job ID: $SLURM_JOB_ID"
echo "Node: $(hostname)"
echo "Date: $(date)"
echo "=================================================="

cd ~/EMBER
OUTPUT_DIR="results/benchmarking/curiosity"
mkdir -p "${OUTPUT_DIR}"

# Log management: automatically copy .out and .err to results directory
copy_logs() {
    echo "=================================================="
    echo "Copying Slurm logs (.out and .err) to ${OUTPUT_DIR}..."
    sync
    sleep 1
    SUBMIT_DIR="${SLURM_SUBMIT_DIR:-.}"
    cp "${SUBMIT_DIR}/cpu_curiosity_${SLURM_JOB_ID}.out" "${OUTPUT_DIR}/" 2>/dev/null || cp "cpu_curiosity_${SLURM_JOB_ID}.out" "${OUTPUT_DIR}/" 2>/dev/null || true
    cp "${SUBMIT_DIR}/cpu_curiosity_${SLURM_JOB_ID}.err" "${OUTPUT_DIR}/" 2>/dev/null || cp "cpu_curiosity_${SLURM_JOB_ID}.err" "${OUTPUT_DIR}/" 2>/dev/null || true
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
echo "Execution completed successfully on Curiosity CPU."