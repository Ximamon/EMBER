#!/bin/bash
#SBATCH --partition=primary
#SBATCH --job-name=ember_4nodes_16gpu
#SBATCH --nodes=4              # 4 nodos físicos DGX distintos
#SBATCH --ntasks-per-node=4    # 4 procesos MPI en cada máquina (16 procesos totales)
#SBATCH --cpus-per-task=8      # 8 cores CPU por proceso (32 cores por nodo)
#SBATCH --gres=gpu:4           # 4 GPUs A100 reservadas por nodo (16 GPUs totales)
#SBATCH --time=00:05:00
#SBATCH --output=16gpu_%j.out
#SBATCH --error=16gpu_%j.err

# 1. Cargar módulos del clúster
module load gcc/14.2.0
module load openmpi/gcc/64/5.0.7
module load cuda12.8/toolkit/12.8.1

# 2. Configurar la red troncal inter-nodo para Curiosity
export SLURM_PMIX_DIRECT_CONN=false
export PRTE_MCA_oob_tcp_if_include=bond0
export OMPI_MCA_oob_tcp_if_include=bond0
export OMPI_MCA_btl_tcp_if_include=bond0

echo "=================================================="
echo "EJECUCIÓN MULTI-NODO: 4 NODOS x 4 GPUs = 16 GPUs A100"
echo "Job ID: $SLURM_JOB_ID"
echo "Nodos asignados:"
scontrol show hostnames $SLURM_JOB_NODELIST
echo "=================================================="

# 3. Lanzar los 16 procesos mapeando 8 cores por proceso
mpirun -np 16 \
  --map-by ppr:4:node:pe=8 \
  --bind-to core \
  --mca oob_tcp_if_include bond0 \
  --mca btl_tcp_if_include bond0 \
  --mca btl tcp,self \
  --mca pml ob1 \
  nsys profile \
    --trace=cuda,nvtx,mpi,osrt \
    --sample=process-tree \
    --backtrace=dwarf \
    --force-overwrite=true \
    -o results/profiling/16gpu/ember_4node_16gpu_%q{OMPI_COMM_WORLD_RANK} \
  ./build/ember \
    --width 1024 \
    --height 1024 \
    --steps 20 \
    --scenarios 128 \
    --seed 42 \
    --base-spread 0.80 \
    --wind-strength 0.8 \
    --wind-direction 45 \
    --export none \
    --output results/profiling/16gpu/