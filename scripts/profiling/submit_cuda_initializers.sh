#!/bin/bash
#SBATCH --partition=primary
#SBATCH --job-name=ember_cuda_init
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --gres=gpu:1
#SBATCH --time=00:30:00
#SBATCH --output=cuda_init_%j.out
#SBATCH --error=cuda_init_%j.err
set -euo pipefail

module load gcc/14.2.0
module load cuda12.8/toolkit/12.8.1
module load Nsight-Systems/2026.2.1

cd "${SLURM_SUBMIT_DIR:-$(dirname "$0")/..}"
if [[ -n "${EMBER_PREBUILT_EXECUTABLE:-}" ]]; then
    executable="$EMBER_PREBUILT_EXECUTABLE"
else
    cmake -S . -B build-cuda-init -DCMAKE_BUILD_TYPE=Release \
        -DEMBER_ENABLE_CUDA=ON -DEMBER_ENABLE_MPI=OFF
    cmake --build build-cuda-init -j 8
    executable="$(pwd)/build-cuda-init/ember"
fi

report_dir="results/cuda_initializers_${SLURM_JOB_ID}"
if [[ -n "${EMBER_BASELINE_EXECUTABLE:-}" ]]; then
    baseline_executable="$EMBER_BASELINE_EXECUTABLE"
else
    baseline_revision="94bd953b60a58006565ef963cd5216ca82beaa23"
    baseline_source="$report_dir/baseline_source"
    mkdir -p "$baseline_source"
    if [[ -n "${EMBER_BASELINE_SOURCE:-}" ]]; then
        cp -a "$EMBER_BASELINE_SOURCE/." "$baseline_source/"
    else
        git archive "$baseline_revision" CMakeLists.txt src include cmake apps tests | \
            tar -x -C "$baseline_source"
    fi
    cmake -S "$baseline_source" -B "$report_dir/baseline_build" \
        -DCMAKE_BUILD_TYPE=Release -DEMBER_ENABLE_CUDA=ON -DEMBER_ENABLE_MPI=OFF
    cmake --build "$report_dir/baseline_build" -j 8
    baseline_executable="$(pwd)/$report_dir/baseline_build/ember"
fi
OMP_NUM_THREADS=8 python3 scripts/profiling/compare_cuda_initializers.py \
    --executable "$executable" \
    --baseline-executable "$baseline_executable" --output "$report_dir"
selected_mode=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["workloads"]["a100"]["selected"])' \
    "$report_dir/report.json")

# A separate trace makes the phase boundaries visible without contaminating the timings.
nsys profile --trace=cuda,nvtx,osrt --sample=none --force-overwrite=true \
    -o "$report_dir/phase_trace" \
    "$executable" --cuda-init "$selected_mode" --width 1024 --height 1024 \
    --steps 1024 --scenarios 20 --seed 42 --base-spread 0.8 \
    --wind-strength 0.8 --wind-direction 45 --export none \
    --output "$report_dir/nsys_run"
nsys stats --report nvtx_sum "$report_dir/phase_trace.nsys-rep" \
    > "$report_dir/nvtx_summary.txt"
echo "Report: $report_dir/report.json"
echo "Trace: $report_dir/phase_trace.nsys-rep"
