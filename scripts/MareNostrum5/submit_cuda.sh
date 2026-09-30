#!/bin/bash
#SBATCH --job-name=ember_mn5_cuda
#SBATCH --account=nct_394
#SBATCH --qos=acc_training
#SBATCH --time=00:15:00
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=20
#SBATCH --gres=gpu:1
#SBATCH --output=ember_cuda_%j.out
#SBATCH --error=ember_cuda_%j.err

echo "=================================================="
echo "MN5 ACC - COMPILACIÓN Y EJECUCIÓN (H100)"
echo "Job ID:         $SLURM_JOB_ID"
echo "Nodo asignado:  $(hostname)"
echo "CPUs asignadas: ${SLURM_CPUS_PER_TASK}"
echo "Fecha:          $(date)"
echo "=================================================="

# 1. Cargar el stack oficial de módulos en el nodo de cómputo
module purge
module load gcc
module load cmake
module load cuda

# 2. Definición de rutas
PROJECT_DIR="${HOME}/EMBER"
BUILD_DIR="${PROJECT_DIR}/build"
OUTPUT_DIR="${SCRATCH:-/gpfs/scratch/nct_394/${USER}}/results/mn5_cuda"

mkdir -p "${OUTPUT_DIR}"

# ==============================================================================
# FASE PREVIA: LIMPIEZA, CONFIGURACIÓN Y COMPILACIÓN
# ==============================================================================
echo ">>> [1/3] Limpiando build previo..."
rm -rf "${BUILD_DIR}"

echo ">>> [2/3] Configurando CMake para NVIDIA Hopper (sm_90)..."
cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DEMBER_ENABLE_CUDA=ON \
    -DEMBER_ENABLE_MPI=OFF \
    -DCMAKE_CUDA_ARCHITECTURES=90

if [ $? -ne 0 ]; then
    echo "ERROR: Falló la configuración de CMake."
    exit 1
fi

echo ">>> [3/3] Compilando con ${SLURM_CPUS_PER_TASK} núcleos..."
cmake --build "${BUILD_DIR}" -j ${SLURM_CPUS_PER_TASK}

if [ $? -ne 0 ]; then
    echo "ERROR: Falló la compilación del proyecto."
    exit 1
fi

EMBER_BIN="${BUILD_DIR}/ember"

if [ ! -f "${EMBER_BIN}" ]; then
    echo "ERROR: No se generó el binario en ${EMBER_BIN}"
    exit 1
fi

echo "=================================================="
echo "Compilación exitosa. Información de GPU:"
nvidia-smi --query-gpu=index,name,memory.total,driver_version --format=csv
echo "=================================================="

# ==============================================================================
# FASE DE EJECUCIÓN
# ==============================================================================
export SRUN_CPUS_PER_TASK=${SLURM_CPUS_PER_TASK}

srun --cpus-per-task=${SLURM_CPUS_PER_TASK} \
    "${EMBER_BIN}" \
    --width 1024 \
    --height 1024 \
    --steps 500 \
    --scenarios 20 \
    --seed 42 \
    --wind-direction 45 \
    --wind-strength 0.4 \
    --base-spread 0.25 \
    --export none \
    --output "${OUTPUT_DIR}"

echo "=================================================="
echo "Simulación finalizada. Resultados guardados en: ${OUTPUT_DIR}"