/**
 * @file simulation_cuda.hpp
 * @author Julian Hinojosa (@jhg45-ua)
 * @brief CUDA accelerated simulation backend with double-buffered VRAM slots and multi-stream overlap.
 * @version 0.6
 * @date 29/7/2026
 */

#pragma once

#include "ember/config.hpp"
#include "ember/grid.hpp"

#include <cuda_runtime.h>
#include <cstddef>
#include <chrono>

namespace ember {

/**
 * @struct CudaScenarioTimings
 * @brief High-precision performance timers for the discrete phases of a GPU simulation scenario.
 */
struct CudaScenarioTimings {
    double allocation_seconds{};
    double host_to_device_seconds{};
    double device_initialization_seconds{};
    double kernel_seconds{};
    double device_to_host_seconds{};
};

/**
 * @class CudaWorkspace
 * @brief RAII persistent VRAM memory pool managing 2 ping-pong slots for zero-bubble overlap.
 */
class CudaWorkspace {
public:
    explicit CudaWorkspace(std::size_t cell_count) noexcept : cell_count_(cell_count) {}
    ~CudaWorkspace() noexcept;

    CudaWorkspace(const CudaWorkspace&) = delete;
    CudaWorkspace& operator=(const CudaWorkspace&) = delete;

    /**
     * @brief Allocates persistent VRAM buffers (both slots) and streams if not already present.
     */
    bool allocate(double& seconds);

    /**
     * @brief Releases all allocated VRAM buffers, streams, and resets member pointers to nullptr.
     */
    void release(double& seconds) noexcept;

private:
    // Funciones del backend en simulation_cuda.cu con acceso a los buffers y streams internos
    friend bool upload_scenario_async(const SimulationConfig&, std::size_t, GridBuffers&,
                                      CudaWorkspace&, int, CudaScenarioTimings&);
    friend bool launch_scenario_kernel(const SimulationConfig&, std::size_t,
                                       CudaWorkspace&, int);
    friend bool sync_and_download_slot(const SimulationConfig&, GridBuffers&,
                                       CudaWorkspace&, int, std::size_t&, CudaScenarioTimings&);
    friend bool sync_transfer_stream(CudaWorkspace&);
    friend bool run_scenario_cuda(const SimulationConfig&, std::size_t, GridBuffers&,
                                  CudaWorkspace&, std::size_t&, CudaScenarioTimings&);

    /// @brief Estructura de buffers de VRAM asignada a cada slot de simulación.
    struct SlotBuffers {
        CellState *state_curr{}, *state_next{};
        float *fuel_curr{}, *fuel_next{};
        float *elevation{}, *moisture{}, *vegetation{};
        CellState *result_state_curr{};
        float *result_fuel_curr{};
    };

    /// @brief Stream dedicado exclusivamente a la ejecución del kernel de cómputo (stencil).
    cudaStream_t stream_compute_{};
    /// @brief Stream dedicado a transferencias asíncronas Host-to-Device (DMA).
    cudaStream_t stream_transfer_{};

    /// @brief Dos ranuras en VRAM (Slot 0 y Slot 1) para permitir doble búfer entre escenarios.
    SlotBuffers slots_[2]{};

    std::size_t cell_count_{};
    std::uint64_t* ignition_indices_{};
    std::size_t ignition_capacity_{};
    std::chrono::steady_clock::time_point kernel_start_time_{};
};

bool initialize_cuda_context(double& seconds);
bool check_cuda_device();

bool upload_scenario_async(
    const SimulationConfig& config,
    std::size_t scenario_id,
    GridBuffers& buffers,
    CudaWorkspace& workspace,
    int slot,
    CudaScenarioTimings& timings
);

bool unregister_scenario_host(const SimulationConfig& config, GridBuffers& buffers);

bool launch_scenario_kernel(
    const SimulationConfig& config,
    std::size_t scenario_id,
    CudaWorkspace& workspace,
    int slot
);

bool sync_and_download_slot(
    const SimulationConfig& config,
    GridBuffers& buffers,
    CudaWorkspace& workspace,
    int slot,
    std::size_t& completed_steps,
    CudaScenarioTimings& timings
);

bool sync_transfer_stream(CudaWorkspace& workspace);

bool run_scenario_cuda(
    const SimulationConfig& config,
    std::size_t scenario_id,
    GridBuffers& buffers,
    CudaWorkspace& workspace,
    std::size_t& completed_steps,
    CudaScenarioTimings& timings
);

} // namespace ember