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

BatchStatistics run_batch(const SimulationConfig& config);

} // namespace ember
