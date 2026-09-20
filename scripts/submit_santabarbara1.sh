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
./build/ember --width 2048 --height 2048 --steps 4096 --scenarios 5 --output results/results_base_server/ --export both

echo "=================================================="
echo "Simulación finalizada con éxito."