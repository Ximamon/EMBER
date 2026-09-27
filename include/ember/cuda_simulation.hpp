#pragma once

#include "ember/config.hpp"
#include "ember/grid.hpp"

#include <cstddef>

namespace ember {
    struct CudaScenarioTimings {
        double allocation_seconds{};
        double host_to_device_seconds{};
        double device_initialization_seconds{};
        double kernel_seconds{};
        double device_to_host_seconds{};
    };

    class CudaWorkspace {
    public:
        explicit CudaWorkspace(std::size_t cell_count) noexcept : cell_count_(cell_count) {}
        ~CudaWorkspace() noexcept;
        CudaWorkspace(const CudaWorkspace&) = delete;
        CudaWorkspace& operator=(const CudaWorkspace&) = delete;
        bool allocate(double& seconds);
        void release(double& seconds) noexcept;

    private:
        friend bool run_scenario_cuda(const SimulationConfig&, std::size_t, GridBuffers&,
                                      CudaWorkspace&, std::size_t&, CudaScenarioTimings&);
        std::size_t cell_count_{};
        CellState *state_curr_{}, *state_next_{};
        float *fuel_curr_{}, *fuel_next_{};
        float *elevation_{}, *moisture_{}, *vegetation_{};
        std::uint64_t* ignition_indices_{};
        std::size_t ignition_capacity_{};
    };

    bool initialize_cuda_context(double& seconds);
    // Checks if CUDA device is avalible and show propierties
    bool check_cuda_device();

    // Exec scenario on GPU
    bool run_scenario_cuda(
        const SimulationConfig& config,
        std::size_t scenario_id,
        GridBuffers& buffers,
        CudaWorkspace& workspace,
        std::size_t& completed_steps,
        CudaScenarioTimings& timings
    );
} // namespace ember
