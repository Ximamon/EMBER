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
mkdir -p results/profiling/curiosity_nsys/

# Light workload (2 scenarios) to capture CPU execution without disk bloat
nsys profile \
    --trace=osrt,nvtx \
    --sample=process-tree \
    --backtrace=dwarf \
    --force-overwrite=true \
    -o results/profiling/curiosity_nsys/ember_cpu_profile \
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
    --output results/profiling/curiosity_nsys/

echo "=================================================="
echo "CPU profiling completed successfully."
