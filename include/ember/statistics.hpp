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
    /// @brief Total number of cells that were non-combustible during the simulation.
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
    std::vector<ScenarioStatistics> scenario_results;
    std::uint64_t total_cell_updates{};
    std::size_t completed_scenarios{};
    std::size_t extinguished_scenarios{};
    std::size_t max_steps_scenarios{};
    double mean_burned_percent{};
    double total_initialization_seconds{};
    double total_simulation_seconds{};
    double total_core_seconds{};
    double mean_scenario_seconds{};
    double throughput_cell_updates_per_second{};
};

const char* to_string(TerminationReason reason) noexcept;
void finalize_batch_statistics(BatchStatistics& statistics);

} // namespace ember
