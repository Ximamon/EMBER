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
    /// @brief Time in seconds spent on device memory allocation (amortized when reusing CudaWorkspace).
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
 * @brief RAII persistent VRAM memory pool managing double-buffered slots and streams for zero-bubble overlap.
 * 
 * Manages two independent VRAM slots (Slot 0 and Slot 1) and two dedicated CUDA streams
 * (`stream_compute_` and `stream_transfer_`) to achieve full concurrency between PCIe DMA transfers
 * and GPU SM compute execution:
 * - Eliminates PCIe starvation bubbles (~3-4 ms per scenario).
 * - Avoids repetitive synchronous `cudaMalloc` / `cudaFree` driver calls between scenarios.
 * - Dynamically locks host memory pages (`cudaHostRegister`) during DMA transfers to ensure non-blocking copies.
 */
class CudaWorkspace {
public:
    /**
     * @brief Constructs an unallocated CUDA workspace for a given cell count.
     * @param cell_count Total number of cells in the simulation grid (width * height).
     */
    explicit CudaWorkspace(std::size_t cell_count) noexcept : cell_count_(cell_count) {}

    /**
     * @brief Destructor. Releases all allocated VRAM buffers and destroys CUDA streams.
     */
    ~CudaWorkspace() noexcept;

    CudaWorkspace(const CudaWorkspace&) = delete;
    CudaWorkspace& operator=(const CudaWorkspace&) = delete;

    /**
     * @brief Allocates persistent VRAM buffers (both slots) and non-blocking streams if not already present.
     * @param[out] seconds Time in seconds spent in allocation and stream creation calls.
     * @return true if allocation succeeded or was already present, false on failure.
     */
    bool allocate(double& seconds);

    /**
     * @brief Releases all allocated VRAM buffers, destroys streams, and resets member pointers to nullptr.
     * @param[out] seconds Time in seconds spent in deallocation calls.
     */
    void release(double& seconds) noexcept;

private:
    // Backend functions in simulation_cuda.cu with privileged access to internal buffers and streams
    friend bool upload_scenario_async(const SimulationConfig&, std::size_t, GridBuffers&,
                                      CudaWorkspace&, int, CudaScenarioTimings&);
    friend bool launch_scenario_kernel(const SimulationConfig&, std::size_t,
                                       CudaWorkspace&, int);
    friend bool sync_and_download_slot(const SimulationConfig&, GridBuffers&,
                                       CudaWorkspace&, int, std::size_t&, CudaScenarioTimings&);
    friend bool sync_transfer_stream(CudaWorkspace&);
    
    friend bool run_scenario_cuda(const SimulationConfig&, std::size_t, GridBuffers&,
                                  CudaWorkspace&, std::size_t&, CudaScenarioTimings&);

    /**
     * @struct SlotBuffers
     * @brief Encapsulates a complete set of simulation device buffers for a single VRAM slot.
     */
    struct SlotBuffers {
        /// @brief Device pointers for double-buffered cell states.
        CellState *state_curr{}, *state_next{};
        /// @brief Device pointers for double-buffered fuel values.
        float *fuel_curr{}, *fuel_next{};
        /// @brief Device pointers for static terrain elevation, moisture, and vegetation layers.
        float *elevation{}, *moisture{}, *vegetation{};
        /// @brief Tracks the active state buffer pointer where final simulation results reside after ping-pong swaps.
        CellState *result_state_curr{};
        /// @brief Tracks the active fuel buffer pointer where final simulation results reside after ping-pong swaps.
        float *result_fuel_curr{};
    };

    /// @brief Dedicated non-blocking CUDA stream for stencil compute kernels.
    cudaStream_t stream_compute_{};
    /// @brief Dedicated non-blocking CUDA stream for asynchronous host-to-device (H2D) DMA transfers.
    cudaStream_t stream_transfer_{};

    /// @brief Double-buffered VRAM slots (Slot 0 and Slot 1) enabling ping-pong concurrent execution.
    SlotBuffers slots_[2]{};

    /// @brief Number of grid cells managed by this device workspace.
    std::size_t cell_count_{};
    /// @brief Dynamically allocated device array holding ignition cell indices.
    std::uint64_t* ignition_indices_{};
    /// @brief Allocated capacity of the ignition index device buffer.
    std::size_t ignition_capacity_{};
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
 * @brief Asynchronously enqueues inputs (H2D) to a designated VRAM slot via DMA transfer stream.
 * 
 * Dynamically pins host pages with `cudaHostRegister` (if using CPU synthetic init) to enable
 * true non-blocking hardware DMA copy over PCIe, preventing host CPU stall.
 * 
 * @param[in] config Simulation configuration parameters.
 * @param[in] scenario_id Unique identifier for the scenario.
 * @param[in,out] buffers Host grid buffers providing views for input data.
 * @param[in,out] workspace Persistent VRAM workspace for GPU memory reuse.
 * @param[in] slot Target slot index (0 or 1) in the double-buffered workspace.
 * @param[out] timings Timing statistics populated with allocation and upload durations.
 * @return true if asynchronous upload succeeded, false on any CUDA error.
 */
bool upload_scenario_async(
    const SimulationConfig& config,
    std::size_t scenario_id,
    GridBuffers& buffers,
    CudaWorkspace& workspace,
    int slot,
    CudaScenarioTimings& timings
);

/**
 * @brief Releases page-locked memory registration for host buffers.
 * 
 * Unregisters pages previously pinned with `cudaHostRegister` once DMA transfers finish.
 * 
 * @param[in] config Simulation configuration parameters.
 * @param[in,out] buffers Host grid buffers whose pages were locked.
 * @return true if unregistration succeeded, false on any error.
 */
bool unregister_scenario_host(const SimulationConfig& config, GridBuffers& buffers);

/**
 * @brief Dispatches simulation timesteps on the dedicated compute stream without blocking the CPU.
 * 
 * Enqueues `config.max_steps` evaluations of `step_stencil_kernel` into `stream_compute_` and returns
 * immediately, allowing concurrent CPU initialization and DMA transfer of the subsequent scenario.
 * 
 * @param[in] config Simulation configuration parameters.
 * @param[in] scenario_id Unique identifier for the scenario.
 * @param[in,out] workspace Persistent VRAM workspace containing the active slot.
 * @param[in] slot Active slot index (0 or 1) to execute.
 * @return true if kernel dispatch succeeded, false on any CUDA error.
 */
bool launch_scenario_kernel(
    const SimulationConfig& config,
    std::size_t scenario_id,
    CudaWorkspace& workspace,
    int slot
);

/**
 * @brief Synchronizes the compute stream for a slot and downloads simulation results to host memory (D2H).
 * 
 * Waits selectively on `stream_compute_` without stalling concurrent operations in other streams,
 * then transfers final cell states and fuel back to host memory.
 * 
 * @param[in] config Simulation configuration parameters.
 * @param[in,out] buffers Host grid buffers to receive downloaded simulation results.
 * @param[in,out] workspace Persistent VRAM workspace containing the computed slot.
 * @param[in] slot Active slot index (0 or 1) being synchronized and downloaded.
 * @param[out] completed_steps Number of simulation steps completed.
 * @param[out] timings Timing statistics populated with kernel and download durations.
 * @return true if synchronization and download succeeded, false on any CUDA error.
 */
bool sync_and_download_slot(
    const SimulationConfig& config,
    GridBuffers& buffers,
    CudaWorkspace& workspace,
    int slot,
    std::size_t& completed_steps,
    CudaScenarioTimings& timings
);

/**
 * @brief Blocks until all enqueued DMA transfers in the transfer stream have finished.
 * 
 * Ensures input data is fully present in VRAM before launching simulation kernels.
 * 
 * @param[in,out] workspace Persistent VRAM workspace containing the transfer stream.
 * @return true if stream synchronization succeeded, false on CUDA error.
 */
bool sync_transfer_stream(CudaWorkspace& workspace);

/**
 * @brief Monolithic synchronous wrapper preserving backward compatibility.
 * 
 * Combines `upload_scenario_async`, `sync_transfer_stream`, `unregister_scenario_host`,
 * `launch_scenario_kernel`, and `sync_and_download_slot` in a single blocking call.
 * 
 * @param[in] config Simulation configuration parameters.
 * @param[in] scenario_id Unique identifier for the scenario.
 * @param[in,out] buffers Host grid buffers providing views and receiving simulation results.
 * @param[in,out] workspace Persistent VRAM workspace for GPU memory reuse.
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