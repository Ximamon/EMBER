#!/bin/bash
#SBATCH --partition=primary        # Default partition
#SBATCH --job-name=ember_run       # Job name
#SBATCH --nodes=1                  # 1 node
#SBATCH --ntasks=1                 # 1 task
#SBATCH --cpus-per-task=4          # 4 cores asigned
#SBATCH --time=00:15:00            # Max task time
#SBATCH --output=ember_%j.out      # Standard output
#SBATCH --error=ember_%j.err       # Error output

module load gcc/14.2.0
module load Nsight-Systems/2026.2.1

echo "=================================================="
echo "Work ID: $SLURM_JOB_ID"
echo "Node: $(hostname)"
echo "Date: $(date)"
echo "=================================================="

# Check if the current directory is the EMBER project root
# and create the results directory if it doesn't exist
cd ~/EMBER
mkdir -p results/results_curiosity/

echo "--------------------------------------------------"

# Full simulation with specified parameters
nsys profile \
    --trace=cuda,nvtx,osrt \
    --sample=process-tree \
    --backtrace=dwarf \
    --cuda-memory-usage=true \
    --force-overwrite=true \
    -o results/results_curiosity/ember_cpu_profile \
./build/ember \
    --width 1024 \
    --height 1024 \
    --steps 1024 \
    --scenarios 20 \
    --seed 42 \
    --base-spread 0.80 \
    --wind-strength 0.8 \
    --wind-direction 45 \
    --export none \
    --output results/results_curiosity/ \

echo "=================================================="
echo "Execution completed successfully."