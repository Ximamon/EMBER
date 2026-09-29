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

mkdir -p results/benchmarking/santabarbara1/

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
    --output results/benchmarking/santabarbara1/

echo "=================================================="
echo "Simulation completed successfully on Santa Barbara 1."