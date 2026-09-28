#!/bin/bash
#SBATCH --partition=primary
#SBATCH --job-name=ember_mpi_cuda_nsys
#SBATCH --nodes=4              # 4 nodos físicos independientes
#SBATCH --ntasks-per-node=1    # 1 proceso MPI por nodo
#SBATCH --cpus-per-task=8      # Cores CPU asignados al proceso
#SBATCH --gres=gpu:1           # 1 GPU NVIDIA A100 reservada por nodo
#SBATCH --time=00:05:00
#SBATCH --output=mpi_cuda_%j.out
#SBATCH --error=mpi_cuda_%j.err

# Cargar módulos del entorno
module load gcc/14.2.0
module load openmpi/gcc/64/5.0.7
module load cuda12.8/toolkit/12.8.1
module load Nsight-Systems/2026.2.1

# Evitar fallos de conectividad PMIx en Curiosity
export SLURM_PMIX_DIRECT_CONN=false

# Forzar interfaz de red bond0 en OpenMPI y PRRTE
export PRTE_MCA_oob_tcp_if_include=bond0
export OMPI_MCA_oob_tcp_if_include=bond0
export OMPI_MCA_btl_tcp_if_include=bond0

echo "=================================================="
echo " Executing EMBER MPI + CUDA Profiling (NSYS)"
echo "Job ID: $SLURM_JOB_ID"
echo "Assigned nodes:"
scontrol show hostnames $SLURM_JOB_NODELIST
echo "=================================================="

# Ejecutar con mpirun perfilando cada nodo de forma independiente
mpirun -np 4 \
  --mca oob_tcp_if_include bond0 \
  --mca btl_tcp_if_include bond0 \
  --mca btl tcp,self \
  --mca pml ob1 \
  --bind-to core \
  nsys profile \
    --trace=cuda,nvtx,osrt,mpi \
    --sample=process-tree \
    --backtrace=dwarf \
    --cuda-memory-usage=true \
    --force-overwrite=true \
    -o results/results_mpi_cuda/ember_rank_%q{OMPI_COMM_WORLD_RANK} \
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
    --output results/results_mpi_cuda/