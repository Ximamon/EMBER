/**
 * @file simulation_cuda.hpp
 * @author Julian Hinojosa (@jhg45-ua)
 * @brief CUDA accelerated simulation backend and VRAM workspace management.
 * @version 0.5
 * @date 29/7/2026
 * 
 * 
 */

#pragma once

#include "ember/config.hpp"
#include "ember/grid.hpp"

#include <cstddef>
#include <chrono>

namespace ember {

/**
 * @struct CudaScenarioTimings
 * @brief High-precision performance timers for the discrete phases of a GPU simulation scenario.
 */
struct CudaScenarioTimings {
    /// @brief Time in seconds spent on device memory allocation (amortized to ~0s when reusing CudaWorkspace).
    double allocation_seconds{};
    /// @brief Time in seconds spent copying initial grid data from host to device (H2D).
    double host_to_device_seconds{};
    /// @brief Time in seconds executing the on-device synthetic initialization kernel.
    double device_initialization_seconds{};
    /// @brief Wall-clock time in seconds executing simulation stencil kernels on the GPU.
    double kernel_seconds{};
    /// @brief Time in seconds copying final scenario state from device to host (D2H).
    double device_to_host_seconds{};
};

/**
 * @class CudaWorkspace
 * @brief RAII persistent VRAM memory pool for amortized GPU batch simulations.
 * 
 * Allocates 7 contiguous GPU buffers (SoA format) once per batch to avoid repetitive,
 * synchronous `cudaMalloc` / `cudaFree` driver calls between scenarios:
 * - State current & next (`CellState`)
 * - Fuel current & next (`float`)
 * - Elevation, moisture, and vegetation (`float`)
 * - Dynamic ignition index array (`uint64_t`)
 * 
 * Pointer swapping during simulation timesteps is strictly performed on local host pointers
 * in `run_scenario_cuda`, preserving the workspace's member pointers across scenario iterations.
 */
class CudaWorkspace {
public:
    /**
     * @brief Constructs an unallocated CUDA workspace for a given cell count.
     * @param cell_count Total number of cells in the simulation grid (width * height).
     */
    explicit CudaWorkspace(std::size_t cell_count) noexcept : cell_count_(cell_count) {}

    /**
     * @brief Destructor. Releases all allocated VRAM buffers.
     */
    ~CudaWorkspace() noexcept;

    CudaWorkspace(const CudaWorkspace&) = delete;
    CudaWorkspace& operator=(const CudaWorkspace&) = delete;

    /**
     * @brief Allocates VRAM buffers if not already allocated.
     * @param[out] seconds Time spent in allocation calls.
     * @return true if allocation succeeded or was already present, false on failure.
     */
    bool allocate(double& seconds);

    /**
     * @brief Frees all allocated device buffers and resets member pointers to nullptr.
     * @param[out] seconds Time spent in deallocation calls.
     */
    void release(double& seconds) noexcept;

private:
    friend bool run_scenario_cuda(const SimulationConfig&, std::size_t, GridBuffers&,
                                  CudaWorkspace&, std::size_t&, CudaScenarioTimings&);
    friend bool launch_scenario_cuda(const SimulationConfig&, std::size_t, GridBuffers&,
                                     CudaWorkspace&, CudaScenarioTimings&);
    friend bool sync_and_download_scenario_cuda(const SimulationConfig&, GridBuffers&,
                                                CudaWorkspace&, std::size_t&, CudaScenarioTimings&);

    std::size_t cell_count_{};
    CellState *state_curr_{}, *state_next_{};
    float *fuel_curr_{}, *fuel_next_{};
    float *elevation_{}, *moisture_{}, *vegetation_{};
    std::uint64_t* ignition_indices_{};
    std::size_t ignition_capacity_{};

    // Tracking active buffers after ping-pong swaps & kernel execution start time
    CellState* result_state_curr_{};
    float* result_fuel_curr_{};
    std::chrono::steady_clock::time_point kernel_start_time_{};
};

/**
 * @brief Warms up the CUDA runtime driver and measures context initialization latency.
 * @param[out] seconds Wall-clock time taken by the initial runtime driver call.
 * @return true if CUDA runtime initialized successfully, false otherwise.
 */
bool initialize_cuda_context(double& seconds);

/**
 * @brief Queries and logs properties of available CUDA GPU devices.
 * @return true if at least one compatible CUDA-capable device is detected, false otherwise.
 */
bool check_cuda_device();

/**
 * @brief Asynchronously enqueues inputs (H2D) and dispatches all simulation kernels to GPU.
 * Returns immediately to allow CPU to perform host initialization for the next scenario.
 */
bool launch_scenario_cuda(
    const SimulationConfig& config,
    std::size_t scenario_id,
    GridBuffers& buffers,
    CudaWorkspace& workspace,
    CudaScenarioTimings& timings
);

/**
 * @brief Blocks host CPU until GPU execution completes (cudaDeviceSynchronize) and downloads results (D2H).
 */
bool sync_and_download_scenario_cuda(
    const SimulationConfig& config,
    GridBuffers& buffers,
    CudaWorkspace& workspace,
    std::size_t& completed_steps,
    CudaScenarioTimings& timings
);

/**
 * @brief Monolithic wrapper preserving backward compatibility.
 */
bool run_scenario_cuda(
    const SimulationConfig& config,
    std::size_t scenario_id,
    GridBuffers& buffers,
    CudaWorkspace& workspace,
    std::size_t& completed_steps,
    CudaScenarioTimings& timings
);

} // namespace ember