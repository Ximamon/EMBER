#!/bin/bash
#SBATCH --partition=primary
#SBATCH --job-name=ember_mpi_nsys
#SBATCH --nodes=4              # 4 physical DGX nodes
#SBATCH --ntasks-per-node=1    # 1 MPI process per node
#SBATCH --cpus-per-task=8      # CPU cores assigned to each process
#SBATCH --time=00:05:00
#SBATCH --output=mpi_nsys_%j.out
#SBATCH --error=mpi_nsys_%j.err

module load gcc/14.2.0
module load openmpi/gcc/64/5.0.7
module load Nsight-Systems/2026.2.1

# Disable broken direct connectivity of Slurm PMIx
export SLURM_PMIX_DIRECT_CONN=false

# Force bond0 interface in both OpenMPI and PRRTE (process manager of OMPI 5)
export PRTE_MCA_oob_tcp_if_include=bond0
export OMPI_MCA_oob_tcp_if_include=bond0
export OMPI_MCA_btl_tcp_if_include=bond0

echo "=================================================="
echo "CURIOSITY - DISTRIBUTED MPI CPU PROFILING (NSYS)"
echo "Job ID: $SLURM_JOB_ID"
echo "Assigned nodes:"
scontrol show hostnames $SLURM_JOB_NODELIST
echo "=================================================="

cd ~/EMBER
mkdir -p results/profiling/mpi_nsys/

# Light workload (4 scenarios = 1 per node) to inspect MPI messaging without disk explosion
mpirun -np 4 \
  --mca oob_tcp_if_include bond0 \
  --mca btl_tcp_if_include bond0 \
  --mca btl tcp,self \
  --mca pml ob1 \
  --bind-to core \
  nsys profile \
    --trace=osrt,mpi,nvtx \
    --sample=process-tree \
    --backtrace=dwarf \
    --force-overwrite=true \
    -o results/profiling/mpi_nsys/ember_mpi_rank_%q{OMPI_COMM_WORLD_RANK} \
  ./build/ember \
    --width 1024 \
    --height 1024 \
    --steps 1024 \
    --scenarios 32 \
    --seed 42 \
    --base-spread 0.80 \
    --wind-strength 0.8 \
    --wind-direction 45 \
    --export none \
    --output results/profiling/mpi_nsys/

echo "=================================================="
echo "MPI CPU profiling completed successfully."
