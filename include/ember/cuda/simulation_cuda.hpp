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

    /// @brief Number of grid cells managed by this device workspace.
    std::size_t cell_count_{};
    /// @brief Device pointers for double-buffered cell states.
    CellState *state_curr_{}, *state_next_{};
    /// @brief Device pointers for double-buffered fuel values.
    float *fuel_curr_{}, *fuel_next_{};
    /// @brief Device pointers for static terrain elevation, moisture, and vegetation layers.
    float *elevation_{}, *moisture_{}, *vegetation_{};
    /// @brief Dynamically allocated device array holding ignition cell indices.
    std::uint64_t* ignition_indices_{};
    /// @brief Allocated capacity of the ignition index device buffer.
    std::size_t ignition_capacity_{};

    /// @brief Tracks the active state buffer pointer where final simulation results reside after ping-pong swaps.
    CellState* result_state_curr_{};
    /// @brief Tracks the active fuel buffer pointer where final simulation results reside after ping-pong swaps.
    float* result_fuel_curr_{};
    /// @brief Timestamp recorded immediately before enqueuing simulation stencil timesteps on the GPU.
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
 * @brief Asynchronously enqueues inputs (H2D) and dispatches all simulation kernels to the GPU.
 * 
 * Returns immediately without blocking the host CPU, enabling concurrent CPU initialization
 * of the next scenario while the GPU executes simulation timesteps.
 * 
 * @param config Simulation configuration parameters.
 * @param scenario_id Unique identifier for the scenario.
 * @param buffers Host grid buffers providing views for input data.
 * @param workspace Persistent VRAM workspace for GPU memory reuse.
 * @param[out] timings Timing statistics populated with allocation and upload durations.
 * @return true if asynchronous launch succeeded, false on any CUDA error.
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
 * 
 * Computes elapsed kernel execution time and transfers final cell states and fuel back to host memory.
 * 
 * @param config Simulation configuration parameters.
 * @param buffers Host grid buffers to receive downloaded simulation results.
 * @param workspace Persistent VRAM workspace containing the final device buffers.
 * @param[out] completed_steps Number of simulation steps completed.
 * @param[out] timings Timing statistics populated with kernel and download durations.
 * @return true if synchronization and download succeeded, false on any CUDA error.
 */
bool sync_and_download_scenario_cuda(
    const SimulationConfig& config,
    GridBuffers& buffers,
    CudaWorkspace& workspace,
    std::size_t& completed_steps,
    CudaScenarioTimings& timings
);

/**
 * @brief Monolithic synchronous wrapper preserving backward compatibility.
 * 
 * Combines `launch_scenario_cuda` and `sync_and_download_scenario_cuda` in a single blocking call.
 * 
 * @param config Simulation configuration parameters.
 * @param scenario_id Unique identifier for the scenario.
 * @param buffers Host grid buffers providing views and receiving simulation results.
 * @param workspace Persistent VRAM workspace for GPU memory reuse.
 * @param[out] completed_steps Number of simulation steps completed.
 * @param[out] timings Deserialized performance phase timings.
 * @return true if scenario executed successfully, false on any CUDA error.
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