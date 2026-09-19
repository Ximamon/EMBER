/**
 * @file runner.hpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Runner wrapper for executing the Ember simulation.
 * @version 0.1
 * @date 29/7/2026
 * 
 * 
 */

#pragma once

#include "ember/config.hpp"
#include "ember/statistics.hpp"

namespace ember {

/**
 * @brief Runs a batch of simulation scenarios based on the provided configuration.
 * 
 * @param config The simulation configuration parameters.
 * @return BatchStatistics The aggregated statistics for all scenarios in the batch.
 */
BatchStatistics run_batch(const SimulationConfig& config);

} // namespace ember
