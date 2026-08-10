# EMBER

EMBER is a small, deterministic CPU baseline for experimenting with stochastic wildfire propagation and future CPU/GPU optimization. It is an educational simulator, not an operational fire prediction system.

The model uses a rectangular Structure-of-Arrays grid, Moore neighborhoods, double buffering and stateless random values derived from `(seed, scenario, step, cell)`. A run is reproducible for the same configuration and build environment, independent of scenario batching or cell traversal order.

## Requirements

- CMake 3.20 or newer.
- A C++17 compiler: GCC, Clang or MSVC.
- Python 3 only if using the optional benchmark automation script.

No third-party C++ libraries or package manager are required.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On single-configuration generators the executable is normally `build/ember`. With Visual Studio it is normally `build/Release/ember.exe`.

Optional AddressSanitizer and UndefinedBehaviorSanitizer build for GCC/Clang:

```sh
cmake -S . -B build-sanitize -DEMBER_ENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

## Run

```sh
./build/ember \
  --width 512 \
  --height 512 \
  --steps 500 \
  --scenarios 20 \
  --seed 42 \
  --wind-direction 45 \
  --wind-strength 0.4 \
  --base-spread 0.25 \
  --output results/
```

Use `./build/ember --help` for every option. `--ignition X,Y` may be repeated; without it, the center cell is ignited. `--output` writes per-scenario and batch metrics to `summary.csv`. Add `--export csv`, `--export ppm` or `--export both` to write final grids.

Wind direction is where the wind blows toward: 0 degrees is east and 90 degrees is north. Coordinates use `(x,y)` with `(0,0)` in the upper-left grid cell.

## Metrics

Initialization and simulation are timed separately with `std::chrono::steady_clock`. Export and CLI work are excluded. One cell update means one cell visited during one executed time step:

```text
cell_updates = width * height * sum(executed_steps_per_scenario)
throughput = cell_updates / simulation_seconds
```

Early extinction therefore performs fewer updates than the configured maximum. Burned area counts final `Burning` and `Burned` cells and divides by the entire rectangular surface.

## Benchmarking

After a Release build:

```sh
python scripts/run_benchmarks.py --executable ./build/ember --output benchmarks/benchmark_results.csv
```

The script performs one warm-up and five measured repetitions for each defined workload. It never enables grid export. See [benchmarks/README.md](benchmarks/README.md) for the matrix and reporting rules.

## Documentation

- [Simplified propagation model](docs/model.md)
- [CUDA readiness notes](docs/cuda-readiness.md)

To generate and view the HTML API documentation locally using Doxygen:

```sh
doxygen Doxyfile
open docs/html/index.html
```

## License

EMBER is released under the [MIT License](LICENSE).
