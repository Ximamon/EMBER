#!/bin/bash
#SBATCH --partition=primary
#SBATCH --job-name=ember_4nodes_16gpu
#SBATCH --nodes=4              # 4 physical DGX nodes
#SBATCH --ntasks-per-node=4    # 4 MPI processes per node (16 processes total)
#SBATCH --cpus-per-task=8      # 8 CPU cores per process (32 cores per node)
#SBATCH --gres=gpu:4           # 4 A100 GPUs reserved per node (16 GPUs total)
#SBATCH --time=00:05:00
#SBATCH --output=16gpu_%j.out
#SBATCH --error=16gpu_%j.err

# 1. Load cluster modules
module load gcc/14.2.0
module load openmpi/gcc/64/5.0.7
module load cuda12.8/toolkit/12.8.1

# 2. Configure inter-node interconnect for Curiosity
export SLURM_PMIX_DIRECT_CONN=false
export PRTE_MCA_oob_tcp_if_include=bond0
export OMPI_MCA_oob_tcp_if_include=bond0
export OMPI_MCA_btl_tcp_if_include=bond0

echo "=================================================="
echo "CURIOSITY - MULTI-NODE PROFILING: 4 NODES x 4 GPUs = 16 A100 GPUs"
echo "Job ID:         $SLURM_JOB_ID"
echo "Assigned nodes:"
scontrol show hostnames $SLURM_JOB_NODELIST
echo "=================================================="

cd ~/EMBER
OUTPUT_DIR="results/profiling/16gpu"
mkdir -p "${OUTPUT_DIR}"

# Log management: automatically copy .out and .err to results directory
copy_logs() {
    echo "=================================================="
    echo "Copying Slurm logs (.out and .err) to ${OUTPUT_DIR}..."
    sync
    sleep 1
    SUBMIT_DIR="${SLURM_SUBMIT_DIR:-.}"
    cp "${SUBMIT_DIR}/16gpu_${SLURM_JOB_ID}.out" "${OUTPUT_DIR}/" 2>/dev/null || cp "16gpu_${SLURM_JOB_ID}.out" "${OUTPUT_DIR}/" 2>/dev/null || true
    cp "${SUBMIT_DIR}/16gpu_${SLURM_JOB_ID}.err" "${OUTPUT_DIR}/" 2>/dev/null || cp "16gpu_${SLURM_JOB_ID}.err" "${OUTPUT_DIR}/" 2>/dev/null || true
    echo "Logs successfully copied."
}
trap copy_logs EXIT

# 3. Launch 16 processes mapping 8 cores per process
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
    -o "${OUTPUT_DIR}/ember_4node_16gpu_%q{OMPI_COMM_WORLD_RANK}" \
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
    --output "${OUTPUT_DIR}/"

echo "=================================================="
echo "Multi-node 16-GPU profiling completed successfully."