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

#include "ember/export.hpp"
#include "ember/simulation.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <vector>

#if EMBER_ENABLE_MPI
#include <mpi.h>
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

BatchStatistics run_batch(const SimulationConfig& config) {

    // Ensure configuration integrity before allocating any large grid buffers or creating directories.
    validate_config(config);

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

    BatchStatistics batch;
    batch.scenario_results.reserve(config.scenarios);
    const std::filesystem::path output_directory(config.output_directory);

    // Process each scenario sequentially. 
    // Each simulation instance generates its own random seed based on its scenario index.
    for (std::size_t scenario_index = 0; scenario_index < config.scenarios; ++scenario_index) {
        if (scenario_index % static_cast<std::size_t>(world_size) != static_cast<std::size_t>(rank)) {
            continue; // This scenario is assigned to a different MPI rank; skip it.
        }

        // Initialize a new WildfireSimulation instance for the current scenario and run it to completion.
        WildfireSimulation simulation(config, static_cast<std::uint64_t>(scenario_index));
        auto scenario_statistics = simulation.run();

        // If the export format is not None, export the final grid state to the specified format(s).
        if (config.export_format != ExportFormat::None) {
            const auto stem = scenario_stem(static_cast<std::uint64_t>(scenario_index));
            const auto grid = static_cast<const GridBuffers&>(simulation.grid()).current_view();

            if (config.export_format == ExportFormat::Csv || config.export_format == ExportFormat::Both) {
                export_grid_csv(output_directory / (stem + ".csv"), grid);
            }

            if (config.export_format == ExportFormat::Ppm || config.export_format == ExportFormat::Both) {
                export_grid_ppm(output_directory / (stem + ".ppm"), grid);
            }
        }
        
        batch.scenario_results.push_back(scenario_statistics);
    }

#if EMBER_ENABLE_MPI
    // Gather all scenario results from other ranks to the master node (Rank 0) for final aggregation and reporting.
    if (world_size > 1) {
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
        finalize_batch_statistics(batch);

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
            export_summary_csv(output_directory / "summary.csv", batch);
        }
    }
    return batch;
}

} // namespace ember
