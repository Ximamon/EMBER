#!/bin/bash
#SBATCH --job-name=ember_baseline
#SBATCH --partition=gpp
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --time=00:45:00
#SBATCH --output=baseline_%j.out
#SBATCH --error=baseline_%j.err
#SBATCH --chdir=/home/sb1user/EMBER

# Entrar explícitamente a la raíz del proyecto
cd /home/sb1user/EMBER

echo "=================================================="
echo "SANTABARBARA1 - MEDIDA BASELINE SECUENCIAL"
echo "Job ID: $SLURM_JOB_ID"
echo "Nodo de ejecución: $(hostname)"
echo "Directorio de trabajo: $(pwd)"
echo "Fecha: $(date)"
echo "=================================================="

# Crear la carpeta de resultados si no existe
mkdir -p results/results_base_server

# Ejecutar el binario con ruta absoluta o relativa a la raíz
./build/ember \
    --width 1024 \
    --height 1024 \
    --steps 500 \
    --scenarios 5 \
    --base-spread 0.80 \
    --wind-strength 0.8 \
    --wind-direction 45 \
    --output results/results_server/ \

echo "=================================================="
echo "Simulación finalizada con éxito."