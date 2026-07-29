#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ember {

enum class TerminationReason {
    Extinguished,
    MaxSteps
};

struct ScenarioStatistics {
    std::uint64_t scenario_id{};
    std::uint64_t scenario_seed{};
    std::size_t steps_executed{};
    std::int64_t extinguished_at_step{-1};
    TerminationReason termination{TerminationReason::MaxSteps};
    std::uint64_t cell_updates{};
    std::size_t burned_cells{};
    std::size_t burning_cells{};
    std::size_t non_combustible_cells{};
    double burned_percent{};
    double initialization_seconds{};
    double simulation_seconds{};
    double total_core_seconds{};
    double throughput_cell_updates_per_second{};
};

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
