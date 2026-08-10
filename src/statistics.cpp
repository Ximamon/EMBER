/**
 * @file statistics.cpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Implementation of the simulation statistics.
 * @version 0.1
 * @date 29/7/2026
 * 
 * 
 */

#include "ember/statistics.hpp"

namespace ember {

const char* to_string(TerminationReason reason) noexcept {
    return reason == TerminationReason::Extinguished ? "extinguished" : "max_steps";
}

void finalize_batch_statistics(BatchStatistics& statistics) {
    statistics.total_cell_updates = 0;
    statistics.completed_scenarios = statistics.scenario_results.size();
    statistics.extinguished_scenarios = 0;
    statistics.max_steps_scenarios = 0;
    statistics.mean_burned_percent = 0.0;
    statistics.total_initialization_seconds = 0.0;
    statistics.total_simulation_seconds = 0.0;
    statistics.total_core_seconds = 0.0;

    for (const auto& scenario : statistics.scenario_results) {
        statistics.total_cell_updates += scenario.cell_updates;
        statistics.mean_burned_percent += scenario.burned_percent;
        statistics.total_initialization_seconds += scenario.initialization_seconds;
        statistics.total_simulation_seconds += scenario.simulation_seconds;
        statistics.total_core_seconds += scenario.total_core_seconds;
        if (scenario.termination == TerminationReason::Extinguished) {
            ++statistics.extinguished_scenarios;
        } else {
            ++statistics.max_steps_scenarios;
        }
    }

    if (statistics.completed_scenarios != 0) {
        const auto count = static_cast<double>(statistics.completed_scenarios);
        statistics.mean_burned_percent /= count;
        statistics.mean_scenario_seconds = statistics.total_core_seconds / count;
    } else {
        statistics.mean_scenario_seconds = 0.0;
    }
    statistics.throughput_cell_updates_per_second =
        statistics.total_simulation_seconds > 0.0
            ? static_cast<double>(statistics.total_cell_updates) / statistics.total_simulation_seconds
            : 0.0;
}

} // namespace ember
