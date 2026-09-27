/**
 * @file runner.cpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Implementation of the simulation runner.
 * @version 0.1
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
#include <filesystem>
#include <iomanip>
#include <stdexcept>
#include <sstream>
#include <type_traits>
#include <vector>

#if EMBER_ENABLE_MPI
#include <mpi.h>
#endif

#if EMBER_ENABLE_CUDA
#include "ember/cuda_simulation.hpp"
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

BatchStatistics run_batch(const SimulationConfig& input_config) {
    const nvtx::ScopedRange batch_range("batch.run", nvtx::purple, 0U);

    SimulationConfig config;
    double load_seconds = 0.0;
    {
        const nvtx::ScopedRange terrain_range("batch.resolve_terrain", nvtx::teal, 1U);
        const auto load_start = std::chrono::steady_clock::now();
        config = resolve_terrain_config(input_config);
        load_seconds = (!input_config.terrain && config.terrain) ?
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
    int world_size = 1;     // Total number of MPI ranks (processes), 1 if MPI is not enabled, menas that the simulation is running in a single process.

#if EMBER_ENABLE_MPI
    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);
    if (mpi_initialized) {
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    }
#endif

#if EMBER_ENABLE_CUDA
    // Check for CUDA device availability only on the master node (Rank 0) to avoid redundant checks across all MPI ranks.
    // if (rank == 0) {
    //     const nvtx::ScopedRange cuda_dev_range("cuda.check_device", nvtx::orange, 1U);
    //     check_cuda_device();
    // }
#endif

    BatchStatistics batch;
    batch.width = config.width;
    batch.height = config.height;
    batch.terrain_load_seconds = load_seconds;
    if (config.terrain && rank == 0) {
        std::cout << "Terrain: EPSG:32631, " << config.terrain->cell_size_m << " m cells; load "
                  << load_seconds << " s\nUniform fuel: " << config.terrain_fuel
                  << "; moisture: " << config.terrain_moisture << "; flat elevation\n";
        for (const auto& point : config.ignitions)
            std::cout << "Ignition: " << point.x << ',' << point.y << '\n';
        if (!config.output_directory.empty()) export_terrain_run(config);
    }
    batch.scenario_results.reserve(config.scenarios);
    const std::filesystem::path output_directory(config.output_directory);
#if EMBER_ENABLE_CUDA
    CudaWorkspace cuda_workspace(checked_cell_count(config));
    if (!initialize_cuda_context(batch.cuda_startup_seconds))
        throw std::runtime_error("CUDA context initialization failed");
#endif

    // Process each scenario sequentially. 
    // Each simulation instance generates its own random seed based on its scenario index.
    for (std::size_t scenario_index = 0; scenario_index < config.scenarios; ++scenario_index) {
        if (scenario_index % static_cast<std::size_t>(world_size) != static_cast<std::size_t>(rank)) {
            continue; // This scenario is assigned to a different MPI rank; skip it.
        }

        const std::string scenario_range_name = "batch.scenario." + std::to_string(scenario_index);
        const nvtx::ScopedRange scenario_range(scenario_range_name.c_str(), nvtx::blue, 1U);
#if EMBER_ENABLE_CUDA
        const auto scenario_wall_start = clock::now();
#endif

        // Initialize a new WildfireSimulation instance for the current scenario and run it to completion.
        WildfireSimulation simulation(config, static_cast<std::uint64_t>(scenario_index));
        ScenarioStatistics scenario_statistics;

#if EMBER_ENABLE_CUDA
        {
            const nvtx::ScopedRange host_init_range("scenario.host_initialize", nvtx::teal, 2U);
            const auto host_init_start = clock::now();
            if (config.synthetic_init_backend == SyntheticInitBackend::Cuda)
                simulation.initialize_empty();
            else
                simulation.initialize();
            scenario_statistics.host_initialization_seconds =
                std::chrono::duration<double>(clock::now() - host_init_start).count();
        }

        auto& buffers = simulation.grid();
        std::size_t completed_steps = 0;
        CudaScenarioTimings cuda_timings;

        // 2. Ejecutar kernel en GPU y verificar éxito
        const bool success = run_scenario_cuda(
            config,
            scenario_index,
            buffers,
            cuda_workspace,
            completed_steps,
            cuda_timings
        );

        if (!success) {
            throw std::runtime_error("CUDA scenario failed on rank " + std::to_string(rank) +
                                     ", scenario " + std::to_string(scenario_index));
        }

        // 3. Registrar métricas de la simulación
        scenario_statistics.scenario_id = scenario_index;
        scenario_statistics.scenario_seed = ember::scenario_seed(config.seed, scenario_index);
        scenario_statistics.steps_executed = completed_steps;
        scenario_statistics.termination = TerminationReason::MaxSteps;
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
        scenario_statistics.mean_step_seconds = cuda_timings.kernel_seconds / completed_steps;
        scenario_statistics.throughput_cell_updates_per_second =
            cuda_timings.kernel_seconds > 0.0 ?
                scenario_statistics.cell_updates / cuda_timings.kernel_seconds : 0.0;

        // Conteo de celdas quemadas
        const auto view = buffers.current_view();
        std::size_t burned_count = 0;
        const std::size_t total_cells = config.width * config.height;
        for (std::size_t i = 0; i < total_cells; ++i) {
            if (view.state[i] == CellState::Burned || view.state[i] == CellState::Burning) {
                ++burned_count;
            }
        }
        scenario_statistics.burned_cells = burned_count;
        scenario_statistics.burned_percent =
            (static_cast<double>(burned_count) / static_cast<double>(total_cells)) * 100.0;

#else
        scenario_statistics = simulation.run();
#endif

        // If the export format is not None, export the final grid state to the specified format(s).
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
#if EMBER_ENABLE_CUDA
        scenario_statistics.scenario_wall_seconds =
            std::chrono::duration<double>(clock::now() - scenario_wall_start).count();
        scenario_statistics.total_core_seconds = scenario_statistics.scenario_wall_seconds;
#endif
        
        batch.scenario_results.push_back(scenario_statistics);
    }

#if EMBER_ENABLE_CUDA
    cuda_workspace.release(batch.cuda_release_seconds);
#endif

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

const auto batch_wall_end = clock::now(); // <-- Fin cronómetro de pared
    const double wall_clock_seconds =
        std::chrono::duration<double>(batch_wall_end - batch_wall_start).count();

    if (rank == 0) {
        {
            const nvtx::ScopedRange statistics_range("batch.finalize_statistics", nvtx::green, 1U);
            finalize_batch_statistics(batch);
        }

#if EMBER_ENABLE_CUDA
        // Include one-time context startup and workspace release in the batch wall time.
        batch.total_core_seconds = wall_clock_seconds;
        batch.mean_scenario_seconds = batch.completed_scenarios > 0 ?
            wall_clock_seconds / static_cast<double>(batch.completed_scenarios) : 0.0;
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
