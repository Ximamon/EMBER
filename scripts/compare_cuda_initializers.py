#!/usr/bin/env python3
"""Verify CUDA initializers, then compare complete batch wall times on one GPU."""

import argparse
import csv
import json
import os
from pathlib import Path
import statistics
import subprocess
import time


def run(executable, output, mode, workload, *, verify=False, export=False):
    output.mkdir(parents=True, exist_ok=True)
    command = [str(executable), "--output", str(output),
               "--export", "csv" if export else "none"]
    if mode is not None:
        command.extend(("--cuda-init", mode))
    for key, value in workload.items():
        if key == "ignitions":
            for point in value:
                command.extend(("--ignition", point))
        else:
            command.extend(("--" + key.replace("_", "-"), str(value)))
    if verify:
        command.append("--verify-cuda-init")
    environment = os.environ.copy()
    environment.setdefault("OMP_NUM_THREADS", "8")
    start = time.perf_counter()
    result = subprocess.run(command, env=environment, capture_output=True, text=True)
    wall = time.perf_counter() - start
    if result.returncode:
        raise RuntimeError(f"{' '.join(command)} failed:\n{result.stdout}\n{result.stderr}")
    with (output / "summary.csv").open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    if len([row for row in rows if row["record_type"] == "scenario"]) != int(workload["scenarios"]):
        raise RuntimeError(f"incomplete scenario results: {output}")
    return {"wall_seconds": wall, "batch": rows[-1], "command": command}


def verify_outputs(executable, root, baseline_executable=None):
    workloads = [
        {"width": 17, "height": 13, "steps": 23, "scenarios": 3, "seed": 1},
        {"width": 128, "height": 96, "steps": 50, "scenarios": 2, "seed": 42},
        {"width": 31, "height": 19, "steps": 17, "scenarios": 4, "seed": 99,
         "ignitions": ["1,1", "28,16"]},
    ]
    results = []
    valid = {"cpu": True, "openmp": True, "gpu": True}
    for index, workload in enumerate(workloads):
        paths = {"cpu": root / "verification" / str(index) / "cpu"}
        run(executable, paths["cpu"], "cpu", workload, export=True)
        if baseline_executable is not None:
            destination = root / "verification" / str(index) / "baseline"
            run(baseline_executable, destination, None, workload, export=True)
            paths["baseline"] = destination
        expected = sorted(paths["cpu"].glob("scenario_*_final.csv"))
        if len(expected) != workload["scenarios"]:
            raise RuntimeError("missing reference grid exports")
        case = {"workload": workload, "baseline": "not supplied"}
        if "baseline" in paths:
            for reference in expected:
                if reference.read_bytes() != (paths["baseline"] / reference.name).read_bytes():
                    raise RuntimeError(f"pre-change baseline differs: {workload}, {reference.name}")
            case["baseline"] = "identical final grids"
        for mode in ("openmp", "gpu"):
            if not valid[mode]:
                case[mode] = "excluded after earlier failure"
                continue
            destination = root / "verification" / str(index) / mode
            try:
                run(executable, destination, mode, workload, verify=mode == "gpu", export=True)
                for reference in expected:
                    if reference.read_bytes() != (destination / reference.name).read_bytes():
                        raise RuntimeError(f"final grid differs: {reference.name}")
                case[mode] = "identical final grids" + (" and initial arrays" if mode == "gpu" else "")
            except (RuntimeError, OSError) as error:
                valid[mode] = False
                case[mode] = f"rejected: {error}"
        results.append(case)
    return results, valid


def metadata():
    values = {}
    for label, command in (("git_revision", ["git", "rev-parse", "HEAD"]),
                           ("gpu", ["nvidia-smi", "--query-gpu=name,driver_version", "--format=csv,noheader"])):
        result = subprocess.run(command, capture_output=True, text=True)
        values[label] = result.stdout.strip() if result.returncode == 0 else "unavailable"
    values["omp_num_threads"] = os.environ.get("OMP_NUM_THREADS", "8")
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--baseline-executable", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    executable = args.executable.resolve()
    baseline_executable = args.baseline_executable.resolve() if args.baseline_executable else None
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    verification, valid = verify_outputs(executable, root, baseline_executable)
    report = {"metadata": metadata(), "verification": verification,
              "valid_modes": valid, "workloads": {}}
    workloads = {
        "small": {"width": 256, "height": 256, "steps": 100, "scenarios": 20,
                  "seed": 42, "base_spread": 0.8, "wind_strength": 0.8, "wind_direction": 45},
        "a100": {"width": 1024, "height": 1024, "steps": 1024, "scenarios": 20,
                 "seed": 42, "base_spread": 0.8, "wind_strength": 0.8, "wind_direction": 45},
    }
    for name, workload in workloads.items():
        if name == "a100" and valid["gpu"]:
            try:
                run(executable, root / name / "gpu" / "verify_full_input", "gpu", workload, verify=True)
                report["full_input_verification"] = "all 20 A100-case inputs match CPU"
            except (RuntimeError, OSError) as error:
                valid["gpu"] = False
                report["full_input_verification"] = f"GPU rejected: {error}"
        measurements = {}
        modes = [mode for mode in ("cpu", "openmp", "gpu") if valid[mode]] + \
                (["baseline"] if baseline_executable else [])
        for mode in modes:
            runner = baseline_executable if mode == "baseline" else executable
            backend = None if mode == "baseline" else mode
            cold = run(runner, root / name / mode / "cold", backend, workload)
            run(runner, root / name / mode / "warmup", backend, workload)
            samples = [run(runner, root / name / mode / f"repeat_{i}", backend, workload)
                       for i in range(5)]
            median = statistics.median(sample["wall_seconds"] for sample in samples)
            measurements[mode] = {
                "cold_wall_seconds": cold["wall_seconds"],
                "cold_cuda_startup_seconds": cold["batch"].get("cuda_startup_seconds", ""),
                "median_batch_wall_seconds": median,
                "repeat_wall_seconds": [sample["wall_seconds"] for sample in samples],
                "command": samples[0]["command"],
            }
        cpu = measurements["cpu"]["median_batch_wall_seconds"]
        fastest = min((mode for mode in ("cpu", "openmp", "gpu") if valid[mode]),
                      key=lambda mode: measurements[mode]["median_batch_wall_seconds"])
        selected = fastest if measurements[fastest]["median_batch_wall_seconds"] <= cpu * 0.95 else "cpu"
        report["workloads"][name] = {"config": workload, "measurements": measurements,
                                     "selected": selected, "selection_rule": "at least 5% faster than cpu"}
        if baseline_executable:
            baseline = measurements["baseline"]["median_batch_wall_seconds"]
            report["workloads"][name]["speedup_vs_baseline"] = baseline / measurements[selected]["median_batch_wall_seconds"]
        print(f"{name}: selected {selected}; medians " + ", ".join(
            f"{mode}={item['median_batch_wall_seconds']:.3f}s" for mode, item in measurements.items()))
    target = root / "report.json"
    target.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(target)


if __name__ == "__main__":
    main()
