#!/bin/bash
#SBATCH --partition=primary
#SBATCH --job-name=ember_cpu_nsys
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --time=00:05:00
#SBATCH --output=cpu_nsys_%j.out
#SBATCH --error=cpu_nsys_%j.err

module load gcc/14.2.0
module load Nsight-Systems/2026.2.1

echo "=================================================="
echo "CURIOSITY - SINGLE-NODE CPU PROFILING (NSYS)"
echo "Job ID: $SLURM_JOB_ID"
echo "Node: $(hostname)"
echo "Date: $(date)"
echo "=================================================="

cd ~/EMBER
OUTPUT_DIR="results/profiling/curiosity_nsys"
mkdir -p "${OUTPUT_DIR}"

# Log management: automatically copy .out and .err to results directory
copy_logs() {
    echo "=================================================="
    echo "Copying Slurm logs (.out and .err) to ${OUTPUT_DIR}..."
    sync
    sleep 1
    SUBMIT_DIR="${SLURM_SUBMIT_DIR:-.}"
    cp "${SUBMIT_DIR}/cpu_nsys_${SLURM_JOB_ID}.out" "${OUTPUT_DIR}/" 2>/dev/null || cp "cpu_nsys_${SLURM_JOB_ID}.out" "${OUTPUT_DIR}/" 2>/dev/null || true
    cp "${SUBMIT_DIR}/cpu_nsys_${SLURM_JOB_ID}.err" "${OUTPUT_DIR}/" 2>/dev/null || cp "cpu_nsys_${SLURM_JOB_ID}.err" "${OUTPUT_DIR}/" 2>/dev/null || true
    echo "Logs successfully copied."
}
trap copy_logs EXIT

# Light workload (2 scenarios) to capture CPU execution without disk bloat
nsys profile \
    --trace=osrt,nvtx \
    --sample=process-tree \
    --backtrace=dwarf \
    --force-overwrite=true \
    -o "${OUTPUT_DIR}/ember_cpu_profile" \
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
echo "CPU profiling completed successfully."
