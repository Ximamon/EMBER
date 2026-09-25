#!/bin/bash
#SBATCH --partition=primary
#SBATCH --job-name=ember_cuda_a100
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --gres=gpu:1              # Reserve 1 GPU NVIDIA A100-SXM4
#SBATCH --time=00:05:00
#SBATCH --output=cuda_%j.out
#SBATCH --error=cuda_%j.err

# Load the necessary modules for CUDA and MPI
module load gcc/14.2.0
module load openmpi/gcc/64/5.0.7
module load cuda12.8/toolkit/12.8.1

echo "=================================================="
echo "CUDA KERNEL TEST ON NVIDIA A100"
echo "Job ID: $SLURM_JOB_ID"
echo "Node: $(hostname)"
echo "Date: $(date)"
echo "Visible GPU devices according to Slurm:"
nvidia-smi --query-gpu=index,name,memory.total,driver_version --format=csv
echo "=================================================="

cd ~/EMBER
mkdir -p results/results_cuda/

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
    --output results/results_cuda/ \

echo "=================================================="
echo "Simulation completed successfully on NVIDIA A100 GPU."