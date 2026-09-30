#!/bin/bash
#SBATCH --partition=primary
#SBATCH --job-name=ember_mpi_cpu
#SBATCH --nodes=4              # 4 physical DGX nodes
#SBATCH --ntasks-per-node=1    # 1 MPI process per node
#SBATCH --cpus-per-task=8      # CPU cores assigned to each process
#SBATCH --time=00:45:00
#SBATCH --output=mpi_cpu_%j.out
#SBATCH --error=mpi_cpu_%j.err

module load gcc/14.2.0
module load openmpi/gcc/64/5.0.7

# Disable broken direct connectivity of Slurm PMIx
export SLURM_PMIX_DIRECT_CONN=false

# Force bond0 interface in both OpenMPI and PRRTE (process manager of OMPI 5)
export PRTE_MCA_oob_tcp_if_include=bond0
export OMPI_MCA_oob_tcp_if_include=bond0
export OMPI_MCA_btl_tcp_if_include=bond0

echo "=================================================="
echo "CURIOSITY - DISTRIBUTED MPI CPU BENCHMARK (4 NODES)"
echo "Job ID: $SLURM_JOB_ID"
echo "Assigned nodes:"
scontrol show hostnames $SLURM_JOB_NODELIST
echo "=================================================="

cd ~/EMBER
OUTPUT_DIR="results/benchmarking/mpi"
mkdir -p "${OUTPUT_DIR}"

# Log management: automatically copy .out and .err to results directory
copy_logs() {
    echo "=================================================="
    echo "Copying Slurm logs (.out and .err) to ${OUTPUT_DIR}..."
    sync
    sleep 1
    SUBMIT_DIR="${SLURM_SUBMIT_DIR:-.}"
    cp "${SUBMIT_DIR}/mpi_cpu_${SLURM_JOB_ID}.out" "${OUTPUT_DIR}/" 2>/dev/null || cp "mpi_cpu_${SLURM_JOB_ID}.out" "${OUTPUT_DIR}/" 2>/dev/null || true
    cp "${SUBMIT_DIR}/mpi_cpu_${SLURM_JOB_ID}.err" "${OUTPUT_DIR}/" 2>/dev/null || cp "mpi_cpu_${SLURM_JOB_ID}.err" "${OUTPUT_DIR}/" 2>/dev/null || true
    echo "Logs successfully copied."
}
trap copy_logs EXIT

# Launch clean MPI execution without nsys observer overhead
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
    --output "${OUTPUT_DIR}/"

echo "=================================================="
echo "Distributed MPI CPU simulation completed successfully."