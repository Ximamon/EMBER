#!/bin/bash
#SBATCH --job-name=ember_mn5_cpu
#SBATCH --account=nct_394
#SBATCH --qos=gp_training
#SBATCH --time=00:15:00
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --output=ember_cpu_%j.out
#SBATCH --error=ember_cpu_%j.err

echo "=================================================="
echo "MN5 GPP - COMPILACIÓN Y EJECUCIÓN CPU"
echo "Job ID:         $SLURM_JOB_ID"
echo "Nodo asignado:  $(hostname)"
echo "Account / QoS:  $SLURM_JOB_ACCOUNT / $SLURM_JOB_QOS"
echo "CPUs asignadas: ${SLURM_CPUS_PER_TASK}"
echo "Fecha:          $(date)"
echo "=================================================="

# 1. Cargar el entorno oficial de módulos para CPU en MN5
module purge
module load gcc
module load cmake

# 2. Configuración de variables de entorno para OpenMP y Slurm
export OMP_NUM_THREADS=${SLURM_CPUS_PER_TASK}
export SRUN_CPUS_PER_TASK=${SLURM_CPUS_PER_TASK}

# 3. Definición de rutas (código en $HOME, salidas en $SCRATCH)
PROJECT_DIR="${HOME}/EMBER"
BUILD_DIR="${PROJECT_DIR}/build"
OUTPUT_DIR="${SCRATCH:-/gpfs/scratch/nct_394/${USER}}/results/mn5_cpu"
mkdir -p "${OUTPUT_DIR}"

# ==============================================================================
# FASE PREVIA: LIMPIEZA, CONFIGURACIÓN Y COMPILACIÓN EN NODO DE CÓMPUTO
# ==============================================================================
echo ">>> [1/3] Limpiando compilación previa..."
rm -rf "${BUILD_DIR}"

echo ">>> [2/3] Configurando CMake para CPU (Release)..."
cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DEMBER_ENABLE_CUDA=OFF \
    -DEMBER_ENABLE_MPI=OFF

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
    echo "ERROR: No se encontró el binario en ${EMBER_BIN}"
    exit 1
fi

echo "=================================================="
echo "Compilación exitosa. Información de CPU en nodo de cómputo:"
lscpu | grep "Model name\|CPU(s):\|Thread(s) per core:"
echo "=================================================="

# ==============================================================================
# FASE DE EJECUCIÓN (EMBER BASELINE CPU)
# ==============================================================================
srun --cpus-per-task=${SLURM_CPUS_PER_TASK} \
    "${EMBER_BIN}" \
    --width 512 \
    --height 512 \
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