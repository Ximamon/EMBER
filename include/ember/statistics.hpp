/**
 * @file statistics.hpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Statistics structure for the Ember simulation.
 * @version 0.1
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
    /// @brief Initialization time in seconds for the simulation.
    double initialization_seconds{};
    /// @brief Simulation time in seconds for the simulation.
    double simulation_seconds{};
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
