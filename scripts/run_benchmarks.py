#!/usr/bin/env python3
"""Run the documented EMBER benchmark matrix using only the Python standard library."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import pathlib
import shutil
import subprocess
import tempfile


MATRIX = (
    (256, 256, 250, 1),
    (256, 256, 250, 20),
    (512, 512, 500, 1),
    (512, 512, 500, 20),
    (1024, 1024, 500, 1),
    (1024, 1024, 500, 10),
    (2048, 2048, 250, 1),
    (2048, 2048, 250, 5),
)

FIELDS = (
    "timestamp_utc", "width", "height", "max_steps", "scenarios", "seed",
    "repetition", "cell_updates", "initialization_seconds", "simulation_seconds",
    "total_core_seconds", "throughput_cell_updates_per_second", "mean_scenario_seconds",
    "mean_burned_percent", "completed_scenarios", "extinguished_scenarios",
    "max_steps_scenarios", "executable",
)


def run_once(executable: pathlib.Path, workload: tuple[int, int, int, int], seed: int,
             output_dir: pathlib.Path) -> dict[str, str]:
    width, height, steps, scenarios = workload
    command = [
        str(executable), "--width", str(width), "--height", str(height),
        "--steps", str(steps), "--scenarios", str(scenarios), "--seed", str(seed),
        "--output", str(output_dir), "--export", "none",
    ]
    subprocess.run(command, check=True)
    with (output_dir / "summary.csv").open(newline="", encoding="utf-8") as source:
        batch = next(row for row in csv.DictReader(source) if row["record_type"] == "batch")
    return batch


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path,
                        default=pathlib.Path("benchmarks/benchmark_results.csv"))
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--repetitions", type=int, default=5)
    args = parser.parse_args()

    executable = args.executable.resolve()
    if not executable.is_file():
        parser.error(f"executable does not exist: {executable}")
    if args.repetitions < 1:
        parser.error("--repetitions must be at least 1")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_header = not args.output.exists() or args.output.stat().st_size == 0
    temporary_root = pathlib.Path(tempfile.mkdtemp(prefix="ember-bench-"))
    try:
        with args.output.open("a", newline="", encoding="utf-8") as destination:
            writer = csv.DictWriter(destination, fieldnames=FIELDS)
            if write_header:
                writer.writeheader()
            for workload_index, workload in enumerate(MATRIX):
                warmup_dir = temporary_root / f"w{workload_index}-warmup"
                run_once(executable, workload, args.seed, warmup_dir)
                for repetition in range(1, args.repetitions + 1):
                    run_dir = temporary_root / f"w{workload_index}-r{repetition}"
                    batch = run_once(executable, workload, args.seed, run_dir)
                    width, height, steps, scenarios = workload
                    writer.writerow({
                        "timestamp_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
                        "width": width, "height": height, "max_steps": steps,
                        "scenarios": scenarios, "seed": args.seed, "repetition": repetition,
                        "cell_updates": batch["cell_updates"],
                        "initialization_seconds": batch["initialization_seconds"],
                        "simulation_seconds": batch["simulation_seconds"],
                        "total_core_seconds": batch["total_core_seconds"],
                        "throughput_cell_updates_per_second": batch["throughput_cell_updates_per_second"],
                        "mean_scenario_seconds": batch["mean_scenario_seconds"],
                        "mean_burned_percent": batch["burned_percent"],
                        "completed_scenarios": batch["completed_scenarios"],
                        "extinguished_scenarios": batch["extinguished_scenarios"],
                        "max_steps_scenarios": batch["max_steps_scenarios"],
                        "executable": executable,
                    })
                    destination.flush()
    finally:
        shutil.rmtree(temporary_root, ignore_errors=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
