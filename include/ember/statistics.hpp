/**
 * @file statistics.hpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Statistics structure for the Ember simulation.
 * @version 0.5
 * @date 29/7/2026
 * 
 * 
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ember {

/**
 * @enum TerminationReason
 * @brief Enumerates the reasons for simulation termination.
 * The available reasons include:
 * - Extinguished: The fire was extinguished before reaching the maximum number of steps.
 * - MaxSteps: The simulation reached the maximum number of steps without extinguishing the fire.
 */
enum class TerminationReason {
    Extinguished,
    MaxSteps
};

/**
 * @struct ScenarioStatistics
 * @brief Structure containing statistics for a single simulation scenario.
 */
struct ScenarioStatistics {
    /// @brief Unique identifier for the simulation scenario.
    std::uint64_t scenario_id{};
    /// @brief Seed used for random number generation in the scenario.
    std::uint64_t scenario_seed{};
    /// @brief Number of simulation steps executed in the scenario.
    std::size_t steps_executed{};
    /// @brief Step index at which the fire was extinguished. -1 if not extinguished.
    std::int64_t extinguished_at_step{-1};
    /// @brief Reason for simulation termination.
    TerminationReason termination{TerminationReason::MaxSteps};
    /// @brief Total number of cell state updates during the simulation.
    std::uint64_t cell_updates{};
    /// @brief Total number of cells that burned during the simulation.
    std::size_t burned_cells{};
    /// @brief Total number of cells currently burning.
    std::size_t burning_cells{};
    /// @brief Total number of cells that were non-combustible during the simulation.
    std::size_t non_combustible_cells{};

    /// @brief Percentage of the grid that burned during the simulation.
    double burned_percent{};
    /// @brief Total count of valid (non-NODATA) cells within the terrain bounding box.
    std::size_t valid_cells{};
    /// @brief Count of NODATA cells located outside the terrain boundaries.
    std::size_t nodata_cells{};
    /// @brief Total number of cells that were initially combustible before ignition.
    std::size_t initially_combustible_cells{};
    /// @brief Total burned area in hectares (-1.0 for synthetic non-georeferenced grids).
    double burned_hectares{-1.0};
    /// @brief Percentage of initially combustible cells that were consumed by fire.
    double combustible_burned_percent{};
    /// @brief Initialization time in seconds for the simulation.
    double initialization_seconds{};
    /// @brief Time spent in host-side memory and state initialization.
    double host_initialization_seconds{};
    /// @brief Time spent in device memory allocation (amortized across batch).
    double device_allocation_seconds{};
    /// @brief Time spent copying input grid arrays from host to device (H2D).
    double host_to_device_seconds{};
    /// @brief Time spent executing on-device synthetic terrain generation.
    double device_initialization_seconds{};
    /// @brief Time spent copying final state arrays from device to host (D2H).
    double device_to_host_seconds{};
    /// @brief Total scenario elapsed wall-clock time in seconds.
    double scenario_wall_seconds{};
    /// @brief Simulation time in seconds for the simulation.
    double simulation_seconds{};

    /// @brief Cumulative time in seconds spent evaluating cellular stencil updates.
    double step_compute_seconds{};
    /// @brief Cumulative time in seconds spent swapping ping-pong buffers.
    double swap_seconds{};
    /// @brief Minimum time observed for a single simulation step in seconds.
    double min_step_seconds{};
    /// @brief Maximum time observed for a single simulation step in seconds.
    double max_step_seconds{};
    /// @brief Mean time per simulation step in seconds.
    double mean_step_seconds{};

    /// @brief Total core time in seconds for the simulation.
    double total_core_seconds{};
    /// @brief Throughput of cell updates per second.
    double throughput_cell_updates_per_second{};
};

/**
 * @struct BatchStatistics
 * @brief Structure containing aggregated statistics for a batch of simulation scenarios.
 */
struct BatchStatistics {
    /// @brief Grid width in cells.
    std::size_t width{};
    /// @brief Grid height in cells.
    std::size_t height{};
    /// @brief Time in seconds spent reading and parsing real terrain raster files.
    double terrain_load_seconds{};
    /// @brief Time in seconds spent initializing the CUDA driver and runtime at batch startup.
    double cuda_startup_seconds{};
    /// @brief Time in seconds spent releasing persistent CUDA VRAM buffers at batch termination.
    double cuda_release_seconds{};
    /// @brief Vector containing statistics for each individual scenario in the batch.
    std::vector<ScenarioStatistics> scenario_results;
    /// @brief Total number of cell state updates across all scenarios.
    std::uint64_t total_cell_updates{};
    /// @brief Total number of completed scenarios.
    std::size_t completed_scenarios{};
    /// @brief Total number of scenarios that terminated because the fire was extinguished.
    std::size_t extinguished_scenarios{};
    /// @brief Total number of scenarios that terminated because they reached the maximum number of steps.
    std::size_t max_steps_scenarios{};
    /// @brief Mean percentage of the grid burned across all scenarios.
    double mean_burned_percent{};
    /// @brief Total initialization time in seconds across all scenarios.
    double total_initialization_seconds{};
    /// @brief Total simulation time in seconds across all scenarios.
    double total_simulation_seconds{};
    
    /// @brief Cumulative stencil calculation time in seconds summed across all scenarios.
    double total_step_compute_seconds{};
    /// @brief Cumulative double-buffering pointer swap time in seconds across all scenarios.
    double total_swap_seconds{};
    /// @brief Mean execution time per simulation step across all scenarios in seconds.
    double mean_step_seconds{};
    
    /// @brief Total core execution time in seconds across all scenarios.
    double total_core_seconds{};
    /// @brief Mean execution time per scenario in seconds.
    double mean_scenario_seconds{};
    /// @brief Average throughput of cell updates per second across the batch.
    double throughput_cell_updates_per_second{};
};

/**
 * @brief Converts a TerminationReason enum value to its corresponding string representation.
 * @param reason The termination reason to convert.
 * @return A string representation of the reason.
 */
const char* to_string(TerminationReason reason) noexcept;

/**
 * @brief Finalizes the batch statistics, calculating means and totals based on the collected scenario results.
 * @param statistics The batch statistics to finalize.
 */
void finalize_batch_statistics(BatchStatistics& statistics);

} // namespace ember
