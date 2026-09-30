/**
 * @file simulation_cuda.cu
 * @author Julian Hinojosa (@jhg45-ua)
 * @brief Implementation of CUDA accelerated kernels, on-device PRNG, and VRAM workspace.
 * @version 0.5
 * @date 29/7/2026
 * 
 * 
 */

#include "ember/cuda/simulation_cuda.hpp"
#include "ember/random.hpp"
#include "ember/nvtx.hpp"
#include "ember/simulation.hpp"

#include <cuda_runtime.h>
#include <iostream>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <vector>

namespace ember {

// Macro to check for CUDA errors
#define CUDA_CHECK(call)                                                      \
    do {                                                                      \
        cudaError_t err = call;                                               \
        if (err != cudaSuccess) {                                             \
            std::cerr << "[CUDA ERROR] " << cudaGetErrorString(err)           \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n";        \
            return false;                                                     \
        }                                                                     \
    } while(0)


    // ============================================================================
    // STATE-LESS DETERMINISTIC RANDOM NUMBER GENERATOR (GPU DEVICE INTRINSICS)
    // ============================================================================

    /**
     * @brief Evaluates the SplitMix64 bit mixer on the GPU.
     * @param value 64-bit integer state.
     * @return 64-bit pseudo-random mixed value.
     */
    __device__ __forceinline__ uint64_t cuda_mix64(uint64_t value) {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31U);
    }

    /**
     * @brief Combines two 64-bit hashes using Golden Ratio mixing.
     * @param seed Accumulated seed hash.
     * @param value Value to combine.
     * @return Combined hash value.
     */
    __device__ __forceinline__ uint64_t cuda_hash_combine(uint64_t seed, uint64_t value) {
        return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
    }

    /**
     * @brief Computes a stateless deterministic hash keyed by scenario, step, and cell.
     * @param seed Base scenario seed.
     * @param tag Domain separator tag (e.g., random_tag::spread).
     * @param step Current timestep index.
     * @param cell Cell index in the 1D grid.
     * @return 64-bit pseudo-random hash value.
     */
    __device__ __forceinline__ uint64_t cuda_keyed_hash(
        uint64_t seed, uint64_t tag, uint64_t step, uint64_t cell)
    {
        uint64_t h = seed;
        h = cuda_hash_combine(h, tag);
        h = cuda_hash_combine(h, step);
        h = cuda_hash_combine(h, cell);
        return cuda_mix64(h);
    }

    /**
     * @brief Normalizes a 64-bit random value to a double-precision float in [0, 1).
     * Matches bit-for-bit the CPU uniform01 implementation.
     * @param bits 64-bit unsigned integer.
     * @return Double-precision float in [0.0, 1.0).
     */
    __device__ __forceinline__ double cuda_uniform01(uint64_t bits) {
        return static_cast<double>(bits >> 11U) * 0x1.0p-53;
    }

    // Synthetic input must use the exact CPU hash structure, ensuring identical random fields.
    __device__ __forceinline__ uint64_t input_mix64(uint64_t value) {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31U);
    }

    __device__ __forceinline__ uint64_t input_hash_combine(uint64_t seed, uint64_t value) {
        return input_mix64(seed ^ input_mix64(value + 0x517cc1b727220a95ULL));
    }

    __device__ __forceinline__ uint64_t input_keyed_hash(uint64_t seed, uint64_t tag, uint64_t cell) {
        return input_hash_combine(input_hash_combine(input_hash_combine(seed, tag), cell), 0);
    }

    /**
     * @brief Maps random bits to a uniform floating-point range using hardware rounding intrinsics.
     * Uses `__fadd_rn`, `__fmul_rn`, and `__fsub_rn` to match IEEE-754 round-to-nearest-even on host.
     */
    __device__ __forceinline__ float input_uniform_range(uint64_t bits, float minimum, float maximum) {
        const float unit = static_cast<float>(static_cast<double>(bits >> 11U) * 0x1.0p-53);
        return __fadd_rn(minimum, __fmul_rn(unit, __fsub_rn(maximum, minimum)));
    }

    /**
     * @brief CUDA kernel for on-device synthetic terrain initialization.
     * 
     * Generates fuel, moisture, vegetation, elevation, and initial burn states directly
     * in VRAM without requiring intermediate PCIe host-to-device transfers.
     * 
     * @param count Total number of cells in the grid.
     * @param center Linear index of the center cell (used for default ignition).
     * @param seed Base scenario seed.
     * @param min_fuel Minimum fuel value.
     * @param max_fuel Maximum fuel value.
     * @param min_moisture Minimum moisture value.
     * @param max_moisture Maximum moisture value.
     * @param min_vegetation Minimum vegetation density.
     * @param max_vegetation Maximum vegetation density.
     * @param min_elevation Minimum elevation in meters.
     * @param max_elevation Maximum elevation in meters.
     * @param non_combustible_fraction Fraction of cells designated non-combustible.
     * @param burn_rate Fuel consumption rate per burning step.
     * @param ignitions Array of linear cell indices for ignition points.
     * @param ignition_count Number of ignition points in the array.
     * @param[out] state_curr Current state buffer.
     * @param[out] state_next Next state buffer.
     * @param[out] fuel_curr Current fuel buffer.
     * @param[out] fuel_next Next fuel buffer.
     * @param[out] moisture Moisture buffer.
     * @param[out] vegetation Vegetation buffer.
     * @param[out] elevation Elevation buffer.
     */
    __global__ void initialize_synthetic_kernel(
        std::size_t count, std::size_t center, uint64_t seed,
        float min_fuel, float max_fuel, float min_moisture, float max_moisture,
        float min_vegetation, float max_vegetation, float min_elevation, float max_elevation,
        float non_combustible_fraction, float burn_rate,
        const uint64_t* ignitions, std::size_t ignition_count,
        CellState* state_curr, CellState* state_next, float* fuel_curr, float* fuel_next,
        float* moisture, float* vegetation, float* elevation)
    {
        const std::size_t index = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
        if (index >= count) return;
        const uint64_t key = static_cast<uint64_t>(index);
        float fuel = input_uniform_range(input_keyed_hash(seed, random_tag::fuel, key), min_fuel, max_fuel);
        moisture[index] = input_uniform_range(input_keyed_hash(seed, random_tag::moisture, key), min_moisture, max_moisture);
        vegetation[index] = input_uniform_range(input_keyed_hash(seed, random_tag::vegetation, key), min_vegetation, max_vegetation);
        elevation[index] = input_uniform_range(input_keyed_hash(seed, random_tag::elevation, key), min_elevation, max_elevation);
        const double non_combustible_draw = static_cast<double>(input_keyed_hash(seed, random_tag::non_combustible, key) >> 11U) * 0x1.0p-53;
        CellState state = non_combustible_draw < non_combustible_fraction ? CellState::NonCombustible : CellState::Unburned;
        bool ignite = ignition_count == 0 && index == center;
        for (std::size_t i = 0; i < ignition_count; ++i) ignite |= ignitions[i] == index;
        if (ignite) {
            state = CellState::Burning;
            fuel = fmaxf(fuel, burn_rate);
        }
        state_curr[index] = state_next[index] = state;
        fuel_curr[index] = fuel_next[index] = fuel;
    }

    /**
     * @brief Clamps a float value to the [0.0, 1.0] range on the device.
     */
    __device__ __forceinline__ float cuda_clamp01(float value) {
        return fminf(fmaxf(value, 0.0f), 1.0f);
    }

    // ============================================================================
    // IGNITION PROBABILITY CALCULATION FOR NEIGHBORING CELLS
    // ============================================================================

    /**
     * @brief Calculates the fire spread probability from a burning neighbor to a target cell.
     * 
     * Incorporates distance attenuation (1.0 orthogonal, 1/sqrt(2) diagonal), wind alignment
     * projection, topographic slope factor, moisture dampening, and fuel/vegetation density.
     */
    __device__ __forceinline__ float cuda_neighbor_probability(
        float target_fuel,
        float target_moisture,
        float target_vegetation,
        float target_elevation,
        float neighbor_elevation,
        int delta_x,
        int delta_y,
        float wind_x,
        float wind_y,
        float wind_strength,
        float slope_scale,
        float base_spread)
    {
        constexpr float inverse_sqrt_two = 0.70710678118654752440f;
        const bool diagonal = (delta_x != 0 && delta_y != 0);
        const float distance_factor = diagonal ? inverse_sqrt_two : 1.0f;
        
        // Spatial projection of fire spread
        const float direction_x = static_cast<float>(delta_x) * distance_factor;
        const float direction_y = static_cast<float>(-delta_y) * distance_factor;

        // Scalar product with the pre-calculated wind vector
        const float alignment = direction_x * wind_x + direction_y * wind_y;
        const float wind_factor = fminf(fmaxf(1.0f + wind_strength * alignment, 0.25f), 2.0f);

        // Topographic slope factor: slope = (target_elevation - neighbor_elevation) / slope_scale
        const float slope = fminf(fmaxf((target_elevation - neighbor_elevation) / slope_scale, -1.0f), 1.0f);
        const float slope_factor = fminf(fmaxf(1.0f + 0.5f * slope, 0.5f), 1.5f);

        // Moisture factor: 1.0 (dry) to 0.2 (wet)
        const float moisture_factor = 1.0f - 0.8f * cuda_clamp01(target_moisture);

        return cuda_clamp01(
            base_spread * cuda_clamp01(target_fuel) * target_vegetation *
            moisture_factor * wind_factor * slope_factor * distance_factor
        );
    }

    // ============================================================================
    // 2D PHYSICAL PROPAGATION KERNEL (MOORE STENCIL)
    // ============================================================================

    /**
     * @brief 2D physical wildfire propagation kernel evaluating an 8-neighbor Moore stencil.
     * 
     * Executed with 16x16 thread blocks. Evaluates state transitions:
     * - NonCombustible / Burned: Remains unchanged.
     * - Burning: Consumes fuel at `burn_rate`; transitions to Burned when fuel <= 0.
     * - Unburned: Evaluates fire transmission probability across the 8 Moore neighbors.
     *   Uses short-circuit pruning when no neighbor is burning. If burning neighbors exist,
     *   draws a pseudo-random number from `cuda_keyed_hash` to decide ignition.
     */
    __global__ void step_stencil_kernel(
        int width,
        int height,
        uint64_t scenario_seed,
        uint64_t step_index,
        float base_spread,
        float burn_rate,
        float wind_strength,
        float wind_x,
        float wind_y,
        float slope_scale,
        const float* __restrict__ fuel_in,
        float* __restrict__ fuel_out,
        const float* __restrict__ elevation,
        const float* __restrict__ moisture,
        const float* __restrict__ vegetation,
        const CellState* __restrict__ state_in,
        CellState* __restrict__ state_out)
    {
        const int x = blockIdx.x * blockDim.x + threadIdx.x;
        const int y = blockIdx.y * blockDim.y + threadIdx.y;

        if (x >= width || y >= height) return;

        const int idx = y * width + x;
        const CellState current_state = state_in[idx];

        // Non-combustible or already burned cells undergo no state transitions
        if (current_state == CellState::NonCombustible || current_state == CellState::Burned) {
            state_out[idx] = current_state;
            fuel_out[idx] = fuel_in[idx];
            return;
        }

        // Burning cells consume fuel according to the configured burn rate
        if (current_state == CellState::Burning) {
            const float remaining_fuel = fuel_in[idx] - burn_rate;
            const float clamped_fuel = remaining_fuel > 0.0f ? remaining_fuel : 0.0f;
            fuel_out[idx] = clamped_fuel;
            state_out[idx] = (clamped_fuel <= 0.0f) ? CellState::Burned : CellState::Burning;
            return;
        }

        // Unburned cell: inspect the 8-cell Moore neighborhood
        double probability_not_ignited = 1.0;

        const float target_fuel = fuel_in[idx];
        const float target_moisture = moisture[idx];
        const float target_vegetation = vegetation[idx];
        const float target_elevation = elevation[idx];

        #pragma unroll
        for (int row_offset = -1; row_offset <= 1; ++row_offset) {
            const int ny = y + row_offset;
            if (ny < 0 || ny >= height) continue;

            #pragma unroll
            for (int col_offset = -1; col_offset <= 1; ++col_offset) {
                if (row_offset == 0 && col_offset == 0) continue;
                const int nx = x + col_offset;
                if (nx < 0 || nx >= width) continue;

                const int n_idx = ny * width + nx;
                if (state_in[n_idx] == CellState::Burning) {
                    const float p_cell = cuda_neighbor_probability(
                        target_fuel,
                        target_moisture,
                        target_vegetation,
                        target_elevation,
                        elevation[n_idx],
                        -col_offset,
                        -row_offset,
                        wind_x,
                        wind_y,
                        wind_strength,
                        slope_scale,
                        base_spread
                    );
                    probability_not_ignited *= (1.0 - static_cast<double>(p_cell));
                }
            }
        }

        fuel_out[idx] = target_fuel;

        // Stochastic short-circuit: if no neighbor is burning, cell remains Unburned
        if (probability_not_ignited >= 1.0) {
            state_out[idx] = CellState::Unburned;
            return;
        }

        // Deterministic stochastic evaluation congruent with the CPU baseline
        const double ignition_probability = 1.0 - probability_not_ignited;
        const double draw = cuda_uniform01(cuda_keyed_hash(
            scenario_seed,
            static_cast<uint64_t>(random_tag::spread),
            step_index,
            static_cast<uint64_t>(idx)
        ));

        state_out[idx] = (draw < ignition_probability) ? CellState::Burning : CellState::Unburned;
    }


    // ============================================================================
    // CONTROLLER AND VRAM MANAGER (HOST RUNNER)
    // ============================================================================

    bool check_cuda_device() {
        int device_count = 0;
        cudaError_t err = cudaGetDeviceCount(&device_count);
        if (err != cudaSuccess || device_count == 0) {
            std::cerr << "No GPUs available\n";
            return false;
        }

        cudaDeviceProp prop;
        cudaGetDeviceProperties(&prop, 0);
        std::cout << "[CUDA] Device: " << prop.name 
                  << " | Compute Capability: " << prop.major << "." << prop.minor
                  << " | VRAM Total: " << prop.totalGlobalMem / (1024 * 1024 * 1024) << " GB\n";
        return true;
    }

    bool initialize_cuda_context(double& seconds) {
        const nvtx::ScopedRange range("cuda.startup", nvtx::purple, 1U);
        const auto start = std::chrono::steady_clock::now();
        
        int device_count = 0;
        cudaGetDeviceCount(&device_count);

        if (device_count > 0) {
            int local_rank = 0;
            const char* ompi_rank_env = std::getenv("OMPI_COMM_WORLD_LOCAL_RANK");
            const char* slurm_rank_env = std::getenv("SLURM_LOCALID");

            if (ompi_rank_env) local_rank = std::atoi(ompi_rank_env);
            else if (slurm_rank_env) local_rank = std::atoi(slurm_rank_env);
            
            const int target_gpu = local_rank % device_count;
            cudaSetDevice(target_gpu);

            cudaDeviceProp prop;
            cudaGetDeviceProperties(&prop, target_gpu);
            std::cout << "[MPI Rank " << local_rank << "] Vinculado a GPU #" 
                      << target_gpu << ": " << prop.name << '\n';
        }

        const auto status = cudaFree(nullptr);
        seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (status != cudaSuccess) {
            std::cerr << "[CUDA ERROR] CUDA startup: " << cudaGetErrorString(status) << '\n';
            return false;
        }
        return true;
    }

    CudaWorkspace::~CudaWorkspace() noexcept {
        double ignored = 0.0;
        release(ignored);
    }

    bool CudaWorkspace::allocate(double& seconds) {
        seconds = 0.0;
        if (slots_[0].state_curr) return true;
        const nvtx::ScopedRange range("cuda.allocate", nvtx::purple, 2U);
        const auto start = std::chrono::steady_clock::now();

        // 1. Crear los dos streams no bloqueantes independientes
        if (!stream_compute_) {
            CUDA_CHECK(cudaStreamCreateWithFlags(&stream_compute_, cudaStreamNonBlocking));
        }
        if (!stream_transfer_) {
            CUDA_CHECK(cudaStreamCreateWithFlags(&stream_transfer_, cudaStreamNonBlocking));
        }

        const auto bytes_state = cell_count_ * sizeof(CellState);
        const auto bytes_float = cell_count_ * sizeof(float);

        const auto allocate_slot = [&](SlotBuffers& s) {
            const auto allocate_one = [](auto** pointer, std::size_t bytes) {
                const auto status = cudaMalloc(pointer, bytes);
                if (status != cudaSuccess)
                    std::cerr << "[CUDA ERROR] allocation: " << cudaGetErrorString(status) << '\n';
                return status == cudaSuccess;
            };
            return allocate_one(&s.state_curr, bytes_state) &&
                   allocate_one(&s.state_next, bytes_state) &&
                   allocate_one(&s.fuel_curr, bytes_float) &&
                   allocate_one(&s.fuel_next, bytes_float) &&
                   allocate_one(&s.elevation, bytes_float) &&
                   allocate_one(&s.moisture, bytes_float) &&
                   allocate_one(&s.vegetation, bytes_float);
        };

        // Reservar VRAM para Slot 0 y Slot 1 (~44 MB totales)
        const bool ok = allocate_slot(slots_[0]) && allocate_slot(slots_[1]);
        seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (!ok) {
            double ignored = 0.0;
            release(ignored);
        }
        return ok;
    }

    void CudaWorkspace::release(double& seconds) noexcept {
        const nvtx::ScopedRange range("cuda.free", nvtx::purple, 1U);
        const auto start = std::chrono::steady_clock::now();

        if (stream_compute_) { cudaStreamDestroy(stream_compute_); stream_compute_ = nullptr; }
        if (stream_transfer_) { cudaStreamDestroy(stream_transfer_); stream_transfer_ = nullptr; }

        const auto free_slot = [](SlotBuffers& s) {
            cudaFree(s.state_curr); cudaFree(s.state_next);
            cudaFree(s.fuel_curr); cudaFree(s.fuel_next);
            cudaFree(s.elevation); cudaFree(s.moisture); cudaFree(s.vegetation);
            s = {};
        };
        free_slot(slots_[0]);
        free_slot(slots_[1]);

        cudaFree(ignition_indices_);
        ignition_indices_ = nullptr;
        ignition_capacity_ = 0;

        seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }

    // ============================================================================
    // PIPELINED ASYNCHRONOUS LAUNCH & MULTI-STREAM SYNCHRONIZATION
    // ============================================================================

    bool upload_scenario_async(
        const SimulationConfig& config,
        std::size_t scenario_id,
        GridBuffers& buffers,
        CudaWorkspace& workspace,
        int slot,
        CudaScenarioTimings& timings)
    {
        const int width = static_cast<int>(config.width);
        const int height = static_cast<int>(config.height);
        const std::size_t num_cells = config.width * config.height;
        const std::size_t bytes_state = num_cells * sizeof(CellState);
        const std::size_t bytes_float = num_cells * sizeof(float);

        auto& s = workspace.slots_[slot];

        if (config.synthetic_init_backend == SyntheticInitBackend::Cuda) {
            const auto count = config.ignitions.size();
            if (count > workspace.ignition_capacity_) {
                const nvtx::ScopedRange allocation_range("cuda.allocate_ignitions", nvtx::purple, 2U);
                const auto start = std::chrono::steady_clock::now();
                cudaFree(workspace.ignition_indices_);
                workspace.ignition_indices_ = nullptr;
                workspace.ignition_capacity_ = 0;
                CUDA_CHECK(cudaMalloc(&workspace.ignition_indices_, count * sizeof(uint64_t)));
                workspace.ignition_capacity_ = count;
                timings.allocation_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            }
            if (count != 0) {
                std::vector<uint64_t> indices;
                indices.reserve(count);
                for (const auto& point : config.ignitions)
                    indices.push_back(static_cast<uint64_t>(point.y * config.width + point.x));
                const nvtx::ScopedRange upload_range("cuda.host_to_device_ignitions", nvtx::teal, 2U);
                const auto start = std::chrono::steady_clock::now();
                CUDA_CHECK(cudaMemcpyAsync(workspace.ignition_indices_, indices.data(), count * sizeof(uint64_t),
                                           cudaMemcpyHostToDevice, workspace.stream_transfer_));
                timings.host_to_device_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            }
            const nvtx::ScopedRange init_range("cuda.initialize_synthetic", nvtx::teal, 2U);
            const auto start = std::chrono::steady_clock::now();
            const uint64_t seed = ember::scenario_seed(config.seed, static_cast<uint64_t>(scenario_id));
            const unsigned int threads = 256;
            const unsigned int blocks = static_cast<unsigned int>((num_cells + threads - 1) / threads);
            
            initialize_synthetic_kernel<<<blocks, threads, 0, workspace.stream_transfer_>>>(
                num_cells, (config.height / 2) * config.width + config.width / 2, seed,
                config.min_fuel, config.max_fuel, config.min_moisture, config.max_moisture,
                config.min_vegetation, config.max_vegetation, config.min_elevation, config.max_elevation,
                config.non_combustible_fraction, config.burn_rate,
                workspace.ignition_indices_, count,
                s.state_curr, s.state_next, s.fuel_curr, s.fuel_next,
                s.moisture, s.vegetation, s.elevation);
            CUDA_CHECK(cudaGetLastError());
            timings.device_initialization_seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        } else {
            auto view = buffers.current_view();
            if (view.state == nullptr || view.fuel == nullptr || view.elevation == nullptr ||
                view.moisture == nullptr || view.vegetation == nullptr) {
                std::cerr << "[CUDA ERROR] host output buffers are not initialized\n";
                return false;
            }

            const nvtx::ScopedRange upload_range("cuda.host_to_device_async", nvtx::teal, 2U);
            const auto start = std::chrono::steady_clock::now();

            // Bloquear páginas en RAM para transferir por DMA asíncrono real
            cudaHostRegister(view.state, bytes_state, cudaHostRegisterDefault);
            cudaHostRegister(view.fuel, bytes_float, cudaHostRegisterDefault);
            cudaHostRegister(view.elevation, bytes_float, cudaHostRegisterDefault);
            cudaHostRegister(view.moisture, bytes_float, cudaHostRegisterDefault);
            cudaHostRegister(view.vegetation, bytes_float, cudaHostRegisterDefault);

            CUDA_CHECK(cudaMemcpyAsync(s.state_curr, view.state, bytes_state, cudaMemcpyHostToDevice, workspace.stream_transfer_));
            CUDA_CHECK(cudaMemcpyAsync(s.fuel_curr, view.fuel, bytes_float, cudaMemcpyHostToDevice, workspace.stream_transfer_));
            CUDA_CHECK(cudaMemcpyAsync(s.elevation, view.elevation, bytes_float, cudaMemcpyHostToDevice, workspace.stream_transfer_));
            CUDA_CHECK(cudaMemcpyAsync(s.moisture, view.moisture, bytes_float, cudaMemcpyHostToDevice, workspace.stream_transfer_));
            CUDA_CHECK(cudaMemcpyAsync(s.vegetation, view.vegetation, bytes_float, cudaMemcpyHostToDevice, workspace.stream_transfer_));

            timings.host_to_device_seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        }

        return true;
    }

    bool unregister_scenario_host(const SimulationConfig& config, GridBuffers& buffers) {
        if (config.synthetic_init_backend == SyntheticInitBackend::Cuda) return true;
        auto view = buffers.current_view();
        cudaHostUnregister(view.state);
        cudaHostUnregister(view.fuel);
        cudaHostUnregister(view.elevation);
        cudaHostUnregister(view.moisture);
        cudaHostUnregister(view.vegetation);
        return true;
    }

    bool launch_scenario_kernel(
        const SimulationConfig& config,
        std::size_t scenario_id,
        CudaWorkspace& workspace,
        int slot)
    {
        const int width = static_cast<int>(config.width);
        const int height = static_cast<int>(config.height);
        auto& s = workspace.slots_[slot];

        constexpr float pi = 3.14159265358979323846f;
        const float radians = config.wind_direction_degrees * pi / 180.0f;
        const float wind_x = std::cos(radians);
        const float wind_y = std::sin(radians);
        const std::uint64_t scenario_seed = ember::scenario_seed(config.seed, static_cast<std::uint64_t>(scenario_id));

        const dim3 block_dim(16, 16);
        const dim3 grid_dim((width + 15) / 16, (height + 15) / 16);

        CellState* d_state_curr = s.state_curr;
        CellState* d_state_next = s.state_next;
        float* d_fuel_curr = s.fuel_curr;
        float* d_fuel_next = s.fuel_next;

        workspace.kernel_start_time_ = std::chrono::steady_clock::now();

        {
            const nvtx::ScopedRange timesteps_range("cuda.timesteps", nvtx::orange, 2U);
            for (std::size_t step = 0; step < config.max_steps; ++step) {
                step_stencil_kernel<<<grid_dim, block_dim, 0, workspace.stream_compute_>>>(
                    width, height, scenario_seed, static_cast<uint64_t>(step),
                    static_cast<float>(config.base_spread), static_cast<float>(config.burn_rate),
                    static_cast<float>(config.wind_strength), wind_x, wind_y,
                    static_cast<float>(config.slope_scale),
                    d_fuel_curr, d_fuel_next, s.elevation, s.moisture, s.vegetation,
                    d_state_curr, d_state_next
                );

                std::swap(d_state_curr, d_state_next);
                std::swap(d_fuel_curr, d_fuel_next);
            }
        }

        s.result_state_curr = d_state_curr;
        s.result_fuel_curr = d_fuel_curr;
        return true;
    }

    bool sync_and_download_slot(
        const SimulationConfig& config,
        GridBuffers& buffers,
        CudaWorkspace& workspace,
        int slot,
        std::size_t& completed_steps,
        CudaScenarioTimings& timings)
    {
        const std::size_t num_cells = config.width * config.height;
        const std::size_t bytes_state = num_cells * sizeof(CellState);
        const std::size_t bytes_float = num_cells * sizeof(float);
        auto& s = workspace.slots_[slot];

        // Sincronizar únicamente el cómputo de la GPU
        {
            const nvtx::ScopedRange synchronize_range("cuda.synchronize", nvtx::red, 2U);
            CUDA_CHECK(cudaStreamSynchronize(workspace.stream_compute_));
        }

        const auto t_end = std::chrono::steady_clock::now();
        timings.kernel_seconds = std::chrono::duration<double>(t_end - workspace.kernel_start_time_).count();
        completed_steps = config.max_steps;

        auto view = buffers.current_view();
        {
            const nvtx::ScopedRange download_range("cuda.device_to_host", nvtx::teal, 2U);
            const auto start = std::chrono::steady_clock::now();
            CUDA_CHECK(cudaMemcpy(view.state, s.result_state_curr, bytes_state, cudaMemcpyDeviceToHost));
            CUDA_CHECK(cudaMemcpy(view.fuel, s.result_fuel_curr, bytes_float, cudaMemcpyDeviceToHost));
            if (config.synthetic_init_backend == SyntheticInitBackend::Cuda) {
                CUDA_CHECK(cudaMemcpy(view.moisture, s.moisture, bytes_float, cudaMemcpyDeviceToHost));
                CUDA_CHECK(cudaMemcpy(view.vegetation, s.vegetation, bytes_float, cudaMemcpyDeviceToHost));
                CUDA_CHECK(cudaMemcpy(view.elevation, s.elevation, bytes_float, cudaMemcpyDeviceToHost));
            }
            timings.device_to_host_seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        }

        return true;
    }

    bool sync_transfer_stream(CudaWorkspace& workspace) {
        CUDA_CHECK(cudaStreamSynchronize(workspace.stream_transfer_));
        return true;
    }

    bool run_scenario_cuda(
        const SimulationConfig& config,
        std::size_t scenario_id,
        GridBuffers& buffers,
        CudaWorkspace& workspace,
        std::size_t& completed_steps,
        CudaScenarioTimings& timings)
    {
        const nvtx::ScopedRange scenario_range("cuda.scenario", nvtx::blue, 1U);
        if (!upload_scenario_async(config, scenario_id, buffers, workspace, 0, timings)) return false;
        if (!sync_transfer_stream(workspace)) return false;
        unregister_scenario_host(config, buffers);
        if (!launch_scenario_kernel(config, scenario_id, workspace, 0)) return false;
        return sync_and_download_slot(config, buffers, workspace, 0, completed_steps, timings);
    }
} // namespace ember