#!/bin/bash
#SBATCH --partition=primary
#SBATCH --job-name=ember_1node_4gpu
#SBATCH --nodes=1              # 1 único nodo DGX físico
#SBATCH --ntasks=4             # 4 procesos MPI en paralelo
#SBATCH --cpus-per-task=8      # 8 cores CPU dedicados a cada proceso (para el pipeline)
#SBATCH --gres=gpu:4           # 4 GPUs NVIDIA A100 reservadas
#SBATCH --time=00:05:00
#SBATCH --output=1node_4gpu_%j.out
#SBATCH --error=1node_4gpu_%j.err

# 1. Cargar módulos verificados en Curiosity
module load gcc/14.2.0
module load openmpi/gcc/64/5.0.7
module load cuda12.8/toolkit/12.8.1
module load Nsight-Systems/2026.2.1

echo "=================================================="
echo "EJECUCIÓN INTRA-NODO: 4 PROCESOS MPI x 4 GPUs A100"
echo "Job ID: $SLURM_JOB_ID"
echo "Nodo asignado: $(hostname)"
echo "GPUs visibles según nvidia-smi:"
nvidia-smi --query-gpu=index,name,pci.bus_id --format=csv
echo "=================================================="


# 2. Lanzar con mpirun (comunicación por memoria compartida 'sm/vader')
mpirun -np 4 \
  --bind-to core \
  nsys profile \
    --trace=cuda,nvtx,mpi,osrt \
    --sample=process-tree \
    --backtrace=dwarf \
    --force-overwrite=true \
    -o results/profiling/4gpu/ember_1node_4gpu_%q{OMPI_COMM_WORLD_RANK} \
  ./build/ember \
    --width 1024 \
    --height 1024 \
    --steps 20 \
    --scenarios 32 \
    --seed 42 \
    --base-spread 0.80 \
    --wind-strength 0.8 \
    --wind-direction 45 \
    --export none \
    --output results/profiling/4gpu/