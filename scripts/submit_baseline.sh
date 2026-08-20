#!/bin/bash
#SBATCH --job-name=ember_baseline
#SBATCH --partition=gpp
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --time=00:05:00
#SBATCH --output=baseline_%j.out
#SBATCH --error=baseline_%j.err

echo "=================================================="
echo " SANTABARBARA1 - MEDIDA BASELINE SECUENCIAL"
echo "Job ID: $SLURM_JOB_ID"
echo "Nodo de ejecución: $(hostname)"
echo "Fecha: $(date)"
echo "=================================================="

# Ejecutar simulación estándar de prueba (Malla 1024x1024, 1 escenario, 500 pasos)
./build/ember --width 1024 --height 1024 --steps 500 --scenarios 1 --output baseline_summary.csv

echo "=================================================="
echo "Simulación finalizada con éxito."
