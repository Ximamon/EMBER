#!/bin/bash
#SBATCH --partition=primary
#SBATCH --job-name=ember_mpi_cuda
#SBATCH --nodes=4              # 4 physical DGX nodes
#SBATCH --ntasks-per-node=1    # 1 MPI process per node
#SBATCH --cpus-per-task=8      # CPU cores assigned to each process
#SBATCH --gres=gpu:1           # 1 GPU NVIDIA A100-SXM4 reserved per node
#SBATCH --time=00:20:00
#SBATCH --output=mpi_cuda_%j.out
#SBATCH --error=mpi_cuda_%j.err

# Load environment modules
module load gcc/14.2.0
module load openmpi/gcc/64/5.0.7
module load cuda12.8/toolkit/12.8.1

# Prevent PMIx connectivity failures on Curiosity
export SLURM_PMIX_DIRECT_CONN=false

# Force network interface bond0 in OpenMPI and PRRTE
export PRTE_MCA_oob_tcp_if_include=bond0
export OMPI_MCA_oob_tcp_if_include=bond0
export OMPI_MCA_btl_tcp_if_include=bond0

echo "=================================================="
echo "CURIOSITY - DISTRIBUTED MPI + CUDA BENCHMARK (4x A100)"
echo "Job ID: $SLURM_JOB_ID"
echo "Assigned nodes:"
scontrol show hostnames $SLURM_JOB_NODELIST
echo "=================================================="

cd ~/EMBER
mkdir -p results/benchmarking/mpi_cuda/

# Launch pure MPI + CUDA execution without profiling overhead
mpirun -np 4 \
  --mca oob_tcp_if_include bond0 \
  --mca btl_tcp_if_include bond0 \
  --mca btl tcp,self \
  --mca pml ob1 \
  --bind-to core \
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
    --output results/benchmarking/mpi_cuda/

echo "=================================================="
echo "Distributed MPI + CUDA benchmark completed successfully."