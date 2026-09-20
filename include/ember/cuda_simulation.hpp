#pragma once

#include "ember/config.hpp"
#include "ember/grid.hpp"

#include <cstddef>

namespace ember {
    // Checks if CUDA device is avalible and show propierties
    bool check_cuda_device();

    // Exec scenario on GPU
    bool run_scenario_cuda(
        const SimulationConfig& config,
        std::size_t scenario_id,
        GridBuffers& buffers,
        std::size_t& completed_steps,
        double& kernel_time_seconds
    );
} // namespace ember