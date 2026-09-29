/**
 * @file runner.cpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Implementation of the simulation runner.
 * @version 0.5
 * @date 29/7/2026
 * 
 * 
 */

#include "ember/runner.hpp"
#include "ember/nvtx.hpp"
#include "ember/export.hpp"
#include "ember/simulation.hpp"
#include "ember/random.hpp"
#include "ember/terrain.hpp"

#include <iostream>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <vector>

#if EMBER_ENABLE_MPI
#include <mpi.h>
#endif

#if EMBER_ENABLE_CUDA
#include "ember/cuda/simulation_cuda.hpp"
#endif

namespace ember {
namespace {

/**
 * @brief Generates a zero-padded filename stem for a given scenario.
 * 
 * Used for exporting CSV and PPM files with consistent naming (e.g., "scenario_000001_final").
 * 
 * @param scenario_id The unique ID of the scenario.
 * @return A string containing the formatted file stem.
 */
std::string scenario_stem(std::uint64_t scenario_id) {
    std::ostringstream stream;
    stream << "scenario_" << std::setw(6) << std::setfill('0') << scenario_id << "_final";
    return stream.str();
}

} // namespace

/**
 * @brief Executes a batch of simulation scenarios sequentially or distributed across MPI ranks.
 */
BatchStatistics run_batch(const SimulationConfig& input_config) {
    const nvtx::ScopedRange batch_range("batch.run", nvtx::purple, 0U);

    SimulationConfig config;
    double load_seconds = 0.0;
    {
        const nvtx::ScopedRange terrain_range("batch.resolve_terrain", nvtx::teal, 1U);
        const auto load_start = std::chrono::steady_clock::now();

        config = resolve_terrain_config(input_config);
        
        // If the terrain was loaded from a file, measure the time taken to load it.
        load_seconds = ((!input_config.terrain && config.terrain) || (!input_config.elevation && config.elevation)) ?
            std::chrono::duration<double>(std::chrono::steady_clock::now() - load_start).count() : 0.0;
    }

    // Ensure configuration integrity before allocating any large grid buffers or creating directories.
    {
        const nvtx::ScopedRange validation_range("config.validate", nvtx::teal, 1U);
        validate_config(config);
    }

    using clock = std::chrono::steady_clock;
    const auto batch_wall_start = clock::now();

    int rank = 0;           // Current MPI rank (process ID)
    int world_size = 1;     // Total number of MPI ranks (processes), 1 if MPI is not enabled.

#if EMBER_ENABLE_MPI
    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);
    if (mpi_initialized) {
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    }
#endif

    // Initialize the batch statistics structure to accumulate results across all scenarios.
    BatchStatistics batch;
    batch.width = config.width;
    batch.height = config.height;
    batch.terrain_load_seconds = load_seconds;
    
    // If real terrain is used, print its properties and any specified ignition points to the console.
    if (config.terrain && rank == 0) {
        std::cout << "Terrain: EPSG:" << config.terrain->epsg << ", " << config.terrain->cell_size_m << " m cells; load "
                  << load_seconds << " s\nUniform fuel: " << config.terrain_fuel
                  << "; moisture: " << config.terrain_moisture
                  << (config.elevation ? "; metric elevation\n" : "; flat elevation\n");
        for (const auto& point : config.ignitions)
            std::cout << "Ignition: " << point.x << ',' << point.y << '\n';
        if (!config.output_directory.empty()) export_terrain_run(config);
    }

    // Reserve space for scenario results to avoid reallocations during the batch run.
    // Each scenario will append its statistics to this vector after completion.
    batch.scenario_results.reserve(config.scenarios);
    // Convert the output directory string to a filesystem path for easier manipulation and validation.
    const std::filesystem::path output_directory(config.output_directory);

    // If using CUDA, allocate a workspace for GPU memory management. 
    // This workspace will be reused across all scenarios to minimize allocation overhead.
#if EMBER_ENABLE_CUDA
    CudaWorkspace cuda_workspace(checked_cell_count(config));
    if (!initialize_cuda_context(batch.cuda_startup_seconds))
        throw std::runtime_error("CUDA context initialization failed");

    // 1. Filter scenarios assigned to this MPI rank
    std::vector<std::size_t> my_scenarios;
    for (std::size_t scenario_index = 0; scenario_index < config.scenarios; ++scenario_index) {
        if (scenario_index % static_cast<std::size_t>(world_size) == static_cast<std::size_t>(rank)) {
            my_scenarios.push_back(scenario_index);
        }
    }

    if (!my_scenarios.empty()) {
        std::unique_ptr<WildfireSimulation> current_sim = nullptr;
        double current_host_init_seconds = 0.0;

        // Pipelined execution loop
        for (std::size_t i = 0; i < my_scenarios.size(); ++i) {
            const std::size_t scenario_index = my_scenarios[i];
            const std::string scenario_range_name = "batch.scenario." + std::to_string(scenario_index);
            const nvtx::ScopedRange scenario_range(scenario_range_name.c_str(), nvtx::blue, 1U);
            const auto scenario_wall_start = clock::now();

            // A. Cold start: allocate and initialize host buffers for the initial scenario
            if (i == 0) {
                current_sim = std::make_unique<WildfireSimulation>(config, static_cast<std::uint64_t>(scenario_index));
                const nvtx::ScopedRange host_init_range("scenario.host_initialize", nvtx::teal, 2U);
                const auto host_init_start = clock::now();
                if (config.synthetic_init_backend == SyntheticInitBackend::Cuda)
                    current_sim->initialize_empty();
                else
                    current_sim->initialize();
                current_host_init_seconds = std::chrono::duration<double>(clock::now() - host_init_start).count();
            }

            CudaScenarioTimings cuda_timings;
            std::size_t completed_steps = 0;

            // B. Enqueue simulation kernels on the GPU (asynchronous launch returns immediately to CPU)
            if (!launch_scenario_cuda(config, scenario_index, current_sim->grid(), cuda_workspace, cuda_timings)) {
                throw std::runtime_error("CUDA scenario launch failed on rank " + std::to_string(rank) +
                                         ", scenario " + std::to_string(scenario_index));
            }

            // C. CONCURRENT OVERLAP: initialize the next scenario (i + 1) in host RAM while the GPU computes scenario i
            std::unique_ptr<WildfireSimulation> next_sim = nullptr;
            double next_host_init_seconds = 0.0;
            if (i + 1 < my_scenarios.size()) {
                const std::size_t next_scenario_index = my_scenarios[i + 1];
                next_sim = std::make_unique<WildfireSimulation>(config, static_cast<std::uint64_t>(next_scenario_index));
                const nvtx::ScopedRange host_init_range("scenario.host_initialize", nvtx::teal, 2U);
                const auto host_init_start = clock::now();
                if (config.synthetic_init_backend == SyntheticInitBackend::Cuda)
                    next_sim->initialize_empty();
                else
                    next_sim->initialize();
                next_host_init_seconds = std::chrono::duration<double>(clock::now() - host_init_start).count();
            }

            // D. Block host CPU until GPU execution completes and download results to RAM
            if (!sync_and_download_scenario_cuda(config, current_sim->grid(), cuda_workspace, completed_steps, cuda_timings)) {
                throw std::runtime_error("CUDA scenario sync failed on rank " + std::to_string(rank) +
                                         ", scenario " + std::to_string(scenario_index));
            }

            // E. Collect scenario-level statistics and timing breakdown
            ScenarioStatistics scenario_statistics;
            scenario_statistics.scenario_id = scenario_index;
            scenario_statistics.scenario_seed = ember::scenario_seed(config.seed, scenario_index);
            scenario_statistics.steps_executed = completed_steps;
            scenario_statistics.termination = TerminationReason::MaxSteps;
            scenario_statistics.host_initialization_seconds = current_host_init_seconds;
            scenario_statistics.device_allocation_seconds = cuda_timings.allocation_seconds;
            scenario_statistics.host_to_device_seconds = cuda_timings.host_to_device_seconds;
            scenario_statistics.device_initialization_seconds = cuda_timings.device_initialization_seconds;
            scenario_statistics.device_to_host_seconds = cuda_timings.device_to_host_seconds;
            scenario_statistics.initialization_seconds = scenario_statistics.host_initialization_seconds +
                cuda_timings.allocation_seconds + cuda_timings.host_to_device_seconds +
                cuda_timings.device_initialization_seconds;
            scenario_statistics.simulation_seconds = cuda_timings.kernel_seconds;
            scenario_statistics.step_compute_seconds = cuda_timings.kernel_seconds;
            scenario_statistics.swap_seconds = 0.0;
            scenario_statistics.cell_updates = completed_steps * config.width * config.height;
            scenario_statistics.mean_step_seconds = completed_steps > 0 ?
                (cuda_timings.kernel_seconds / static_cast<double>(completed_steps)) : 0.0;
            scenario_statistics.throughput_cell_updates_per_second =
                cuda_timings.kernel_seconds > 0.0 ?
                    (static_cast<double>(scenario_statistics.cell_updates) / cuda_timings.kernel_seconds) : 0.0;

            const auto view = static_cast<const GridBuffers&>(current_sim->grid()).current_view();
            std::size_t burned_count = 0;
            const std::size_t total_cells = config.width * config.height;

            for (std::size_t c = 0; c < total_cells; ++c) {
                if (view.state[c] == CellState::Burned || view.state[c] == CellState::Burning) {
                    ++burned_count;
                }
            }

            scenario_statistics.burned_cells = burned_count;
            scenario_statistics.burned_percent =
                (static_cast<double>(burned_count) / static_cast<double>(total_cells)) * 100.0;

            // F. File export (CSV / PPM) if requested
            if (config.export_format != ExportFormat::None) {
                const nvtx::ScopedRange export_range("scenario.export_grid", nvtx::yellow, 2U);
                const auto stem = scenario_stem(static_cast<std::uint64_t>(scenario_index));
                if (config.export_format == ExportFormat::Csv || config.export_format == ExportFormat::Both) {
                    export_grid_csv(output_directory / (stem + ".csv"), view, config.terrain.get());
                }
                if (config.export_format == ExportFormat::Ppm || config.export_format == ExportFormat::Both) {
                    export_grid_ppm(output_directory / (stem + ".ppm"), view, config.terrain.get());
                }
            }

            scenario_statistics.scenario_wall_seconds =
                std::chrono::duration<double>(clock::now() - scenario_wall_start).count();
            scenario_statistics.total_core_seconds = scenario_statistics.scenario_wall_seconds;

            batch.scenario_results.push_back(scenario_statistics);

            // G. Advance next scenario to current for the subsequent iteration
            if (next_sim) {
                current_sim = std::move(next_sim);
                current_host_init_seconds = next_host_init_seconds;
            }
        }
    }

    cuda_workspace.release(batch.cuda_release_seconds);

#else
    // Standard CPU execution path (without CUDA)
    for (std::size_t scenario_index = 0; scenario_index < config.scenarios; ++scenario_index) {
        if (scenario_index % static_cast<std::size_t>(world_size) != static_cast<std::size_t>(rank)) {
            continue;
        }

        const std::string scenario_range_name = "batch.scenario." + std::to_string(scenario_index);
        const nvtx::ScopedRange scenario_range(scenario_range_name.c_str(), nvtx::blue, 1U);

        WildfireSimulation simulation(config, static_cast<std::uint64_t>(scenario_index));
        ScenarioStatistics scenario_statistics = simulation.run();

        if (config.export_format != ExportFormat::None) {
            const nvtx::ScopedRange export_range("scenario.export_grid", nvtx::yellow, 2U);
            const auto stem = scenario_stem(static_cast<std::uint64_t>(scenario_index));
            const auto grid = static_cast<const GridBuffers&>(simulation.grid()).current_view();

            if (config.export_format == ExportFormat::Csv || config.export_format == ExportFormat::Both) {
                export_grid_csv(output_directory / (stem + ".csv"), grid, config.terrain.get());
            }
            if (config.export_format == ExportFormat::Ppm || config.export_format == ExportFormat::Both) {
                export_grid_ppm(output_directory / (stem + ".ppm"), grid, config.terrain.get());
            }
        }

        batch.scenario_results.push_back(scenario_statistics);
    }
#endif

// Gather scenario results from all MPI ranks to the master node (Rank 0) for final aggregation and reporting
#if EMBER_ENABLE_MPI
    // Gather all scenario results from other ranks to the master node (Rank 0) for final aggregation and reporting.
    if (world_size > 1) {
        const nvtx::ScopedRange gather_range("mpi.gather_results", nvtx::red, 1U);
        using ResultType = decltype(batch.scenario_results)::value_type;
        static_assert(std::is_trivially_copyable_v<ResultType>,
                      "ScenarioResult must be trivially copyable for MPI communication.");

        if (rank != 0) {
            // Secondary nodes send their list of results to Rank 0
            const std::uint64_t count = batch.scenario_results.size();
            MPI_Send(&count, 1, MPI_UINT64_T, 0, 0, MPI_COMM_WORLD);
            if (count > 0) {
                MPI_Send(batch.scenario_results.data(),
                         static_cast<int>(count * sizeof(ResultType)),
                         MPI_BYTE, 0, 1, MPI_COMM_WORLD);
            }
        } else {
            // The master node (Rank 0) receives the results from each of the other nodes
            for (int src_rank = 1; src_rank < world_size; ++src_rank) {
                std::uint64_t incoming_count = 0;
                MPI_Recv(&incoming_count, 1, MPI_UINT64_T, src_rank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                if (incoming_count > 0) {
                    const std::size_t prev_size = batch.scenario_results.size();
                    batch.scenario_results.resize(prev_size + incoming_count);
                    MPI_Recv(&batch.scenario_results[prev_size],
                             static_cast<int>(incoming_count * sizeof(ResultType)),
                             MPI_BYTE, src_rank, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                }
            }

            // Sort by scenario ID to ensure a canonical order
            std::sort(batch.scenario_results.begin(), batch.scenario_results.end(),
                      [](const auto& a, const auto& b) {
                          return a.scenario_id < b.scenario_id;
                      });
        }
    }
#endif

    // End of batch wall-clock timing
    const auto batch_wall_end = clock::now();
    const double wall_clock_seconds =
        std::chrono::duration<double>(batch_wall_end - batch_wall_start).count();

    if (rank == 0) {
        {
            const nvtx::ScopedRange statistics_range("batch.finalize_statistics", nvtx::green, 1U);
            finalize_batch_statistics(batch);
        }

#if EMBER_ENABLE_CUDA
        // On single-node execution, the total core time is equal to the wall-clock time.
        if (world_size == 1) {
        batch.total_core_seconds = wall_clock_seconds;
        batch.mean_scenario_seconds = batch.completed_scenarios > 0 ?
            wall_clock_seconds / static_cast<double>(batch.completed_scenarios) : 0.0;
    }
#endif

        // On multi-node parallel execution, replace with the actual elapsed wall-clock time
        if (world_size > 1) {
            batch.total_simulation_seconds = wall_clock_seconds;
            batch.total_core_seconds = wall_clock_seconds;
            batch.total_step_compute_seconds /= static_cast<double>(world_size);
            batch.total_swap_seconds /= static_cast<double>(world_size);
            batch.throughput_cell_updates_per_second =
                wall_clock_seconds > 0.0
                    ? static_cast<double>(batch.total_cell_updates) / wall_clock_seconds
                    : 0.0;
        }

        if (!config.output_directory.empty()) {
            const nvtx::ScopedRange summary_range("batch.export_summary", nvtx::yellow, 1U);
            export_summary_csv(output_directory / "summary.csv", batch);
        }
    }
    return batch;
}

} // namespace ember