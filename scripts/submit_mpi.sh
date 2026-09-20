#!/bin/bash
#SBATCH --partition=primary
#SBATCH --job-name=ember_mpi_4nodes
#SBATCH --nodes=4              # 4 fisical nodes
#SBATCH --ntasks-per-node=1    # 1 MPI process per node
#SBATCH --cpus-per-task=8      # Cores asignados a cada proceso
#SBATCH --time=00:05:00
#SBATCH --output=mpi_%j.out
#SBATCH --error=mpi_%j.err

module load gcc/14.2.0
module load openmpi/gcc/64/5.0.7

# Disable broken direct connectivity of Slurm PMIx
export SLURM_PMIX_DIRECT_CONN=false

# Force bond0 interface in both OpenMPI and PRRTE (process manager of OMPI 5)
export PRTE_MCA_oob_tcp_if_include=bond0
export OMPI_MCA_oob_tcp_if_include=bond0
export OMPI_MCA_btl_tcp_if_include=bond0

echo "=================================================="
echo " Executing EMBER distributed on 4 physical DGX nodes"
echo "Job ID: $SLURM_JOB_ID"
echo "Asigned nodes:"
scontrol show hostnames $SLURM_JOB_NODELIST
echo "=================================================="

# Lanzar forzando la capa de transporte TCP sobre bond0
mpirun -np 4 \
  --mca oob_tcp_if_include bond0 \
  --mca btl_tcp_if_include bond0 \
  --mca btl tcp,self \
  --mca pml ob1 \
  --bind-to core \
  ./build/ember \
    --width 1024 \
    --height 1024 \
    --steps 500 \
    --scenarios 20 \
    --seed 42 \
    --base-spread 0.80 \
    --wind-strength 0.8 \
    --wind-direction 45 \
    --export none \
    --output results/results_mpi/