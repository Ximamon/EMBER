#!/bin/bash
#SBATCH --partition=primary        # Default partition
#SBATCH --job-name=ember_run       # Job name
#SBATCH --nodes=1                  # 1 node
#SBATCH --ntasks=1                 # 1 task
#SBATCH --cpus-per-task=4          # 4 cores asigned
#SBATCH --time=00:15:00            # Max task time
#SBATCH --output=ember_%j.out      # Standard output
#SBATCH --error=ember_%j.err       # Error output

echo "=================================================="
echo "Work ID: $SLURM_JOB_ID"
echo "Node: $(hostname)"
echo "Date: $(date)"
echo "=================================================="

# Check if the current directory is the EMBER project root
cd ~/EMBER

# Create the results directory if it doesn't exist
mkdir -p results_curiosity/

# 1. Launch the RNG benchmark
./build/ember --benchmark-rng

echo "--------------------------------------------------"

# 2. Full simulation with specified parameters
./build/ember --width 1024 --height 1024 --steps 500 --scenarios 5 --output results_curiosity/

echo "=================================================="
echo "Execution completed successfully."