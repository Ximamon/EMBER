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

    /**
     * @brief Queries and logs properties of available CUDA GPU devices.
     */
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

    /**
     * @brief Initializes the CUDA driver runtime context and binds the process to a device.
     */
    bool initialize_cuda_context(double& seconds) {
        const nvtx::ScopedRange range("cuda.startup", nvtx::purple, 1U);
        const auto start = std::chrono::steady_clock::now();
        
        // Assign GPU based on local rank to avoid contention in multi-GPU nodes
        int device_count = 0;
        cudaGetDeviceCount(&device_count);
        if (device_count > 0) {
            int local_rank = 0;
            const char* ompi_rank_env = std::getenv("OMPI_COMM_WORLD_LOCAL_RANK");
            const char* slurm_rank_env = std::getenv("SLURM_LOCALID");
            if (ompi_rank_env) local_rank = std::atoi(ompi_rank_env);
            else if (slurm_rank_env) local_rank = std::atoi(slurm_rank_env);
            cudaSetDevice(local_rank % device_count);
        }

        const auto status = cudaFree(nullptr);
        seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (status != cudaSuccess) {
            std::cerr << "[CUDA ERROR] CUDA startup: " << cudaGetErrorString(status) << '\n';
            return false;
        }
        return true;
    }

    /**
     * @brief Destructor. Automatically deallocates all device memory buffers.
     */
    CudaWorkspace::~CudaWorkspace() noexcept {
        double ignored = 0.0;
        release(ignored);
    }

    /**
     * @brief Allocates persistent VRAM buffers if not already allocated.
     */
    bool CudaWorkspace::allocate(double& seconds) {
        seconds = 0.0;
        if (state_curr_) return true;
        const nvtx::ScopedRange range("cuda.allocate", nvtx::purple, 2U);
        const auto start = std::chrono::steady_clock::now();
        const auto bytes_state = cell_count_ * sizeof(CellState);
        const auto bytes_float = cell_count_ * sizeof(float);
        const auto allocate_one = [](auto** pointer, std::size_t bytes) {
            const auto status = cudaMalloc(pointer, bytes);
            if (status != cudaSuccess)
                std::cerr << "[CUDA ERROR] allocation: " << cudaGetErrorString(status) << '\n';
            return status == cudaSuccess;
        };
        const bool ok = allocate_one(&state_curr_, bytes_state) &&
                        allocate_one(&state_next_, bytes_state) &&
                        allocate_one(&fuel_curr_, bytes_float) &&
                        allocate_one(&fuel_next_, bytes_float) &&
                        allocate_one(&elevation_, bytes_float) &&
                        allocate_one(&moisture_, bytes_float) &&
                        allocate_one(&vegetation_, bytes_float);
        seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (!ok) {
            double ignored = 0.0;
            release(ignored);
        }
        return ok;
    }

    /**
     * @brief Releases all allocated VRAM buffers and resets pointers to nullptr.
     */
    void CudaWorkspace::release(double& seconds) noexcept {
        const nvtx::ScopedRange range("cuda.free", nvtx::purple, 1U);
        const auto start = std::chrono::steady_clock::now();
        cudaFree(state_curr_); cudaFree(state_next_);
        cudaFree(fuel_curr_); cudaFree(fuel_next_);
        cudaFree(elevation_); cudaFree(moisture_); cudaFree(vegetation_);
        cudaFree(ignition_indices_);
        state_curr_ = state_next_ = nullptr;
        fuel_curr_ = fuel_next_ = elevation_ = moisture_ = vegetation_ = nullptr;
        ignition_indices_ = nullptr;
        ignition_capacity_ = 0;
        seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }

    // ============================================================================
    // PIPELINED ASYNCHRONOUS LAUNCH & SYNCHRONIZATION
    // ============================================================================

    /**
     * @brief Asynchronously enqueues inputs and launches stencil kernels on the GPU.
     */
    bool launch_scenario_cuda(
        const SimulationConfig& config,
        std::size_t scenario_id,
        GridBuffers& buffers,
        CudaWorkspace& workspace,
        CudaScenarioTimings& timings)
    {
        const int width = static_cast<int>(config.width);
        const int height = static_cast<int>(config.height);
        const std::size_t num_cells = config.width * config.height;

        const std::size_t bytes_state = num_cells * sizeof(CellState);
        const std::size_t bytes_float = num_cells * sizeof(float);

        if (!workspace.allocate(timings.allocation_seconds)) return false;

        CellState* d_state_curr = workspace.state_curr_;
        CellState* d_state_next = workspace.state_next_;
        float* d_fuel_curr = workspace.fuel_curr_;
        float* d_fuel_next = workspace.fuel_next_;
        float* d_elevation = workspace.elevation_;
        float* d_moisture = workspace.moisture_;
        float* d_vegetation = workspace.vegetation_;

        auto view = buffers.current_view();
        if (view.state == nullptr || view.fuel == nullptr || view.elevation == nullptr ||
            view.moisture == nullptr || view.vegetation == nullptr) {
            std::cerr << "[CUDA ERROR] host output buffers are not initialized\n";
            return false;
        }

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
                CUDA_CHECK(cudaMemcpy(workspace.ignition_indices_, indices.data(), count * sizeof(uint64_t), cudaMemcpyHostToDevice));
                timings.host_to_device_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            }
            const nvtx::ScopedRange init_range("cuda.initialize_synthetic", nvtx::teal, 2U);
            const auto start = std::chrono::steady_clock::now();
            const uint64_t seed = ember::scenario_seed(config.seed, static_cast<uint64_t>(scenario_id));
            const unsigned int threads = 256;
            const unsigned int blocks = static_cast<unsigned int>((num_cells + threads - 1) / threads);
            initialize_synthetic_kernel<<<blocks, threads>>>(
                num_cells, (config.height / 2) * config.width + config.width / 2, seed,
                config.min_fuel, config.max_fuel, config.min_moisture, config.max_moisture,
                config.min_vegetation, config.max_vegetation, config.min_elevation, config.max_elevation,
                config.non_combustible_fraction, config.burn_rate,
                workspace.ignition_indices_, count,
                d_state_curr, d_state_next, d_fuel_curr, d_fuel_next,
                d_moisture, d_vegetation, d_elevation);
            CUDA_CHECK(cudaGetLastError());
            CUDA_CHECK(cudaDeviceSynchronize());
            timings.device_initialization_seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (config.verify_cuda_initialization) {
                const nvtx::ScopedRange verify_range("cuda.verify_initial_input", nvtx::green, 2U);
                SimulationConfig cpu_config = config;
                cpu_config.synthetic_init_backend = SyntheticInitBackend::Cpu;
                cpu_config.verify_cuda_initialization = false;
                WildfireSimulation reference(cpu_config, scenario_id);
                reference.initialize();
                const auto expected = static_cast<const GridBuffers&>(reference.grid()).current_view();
                std::vector<CellState> states(num_cells);
                std::vector<float> values(num_cells);
                CUDA_CHECK(cudaMemcpy(states.data(), d_state_curr, bytes_state, cudaMemcpyDeviceToHost));
                if (std::memcmp(states.data(), expected.state, bytes_state) != 0) {
                    std::cerr << "[CUDA ERROR] initialized state differs from CPU\n";
                    return false;
                }
                const auto compare_field = [&](const char* name, const float* device, const float* host) {
                    const auto status = cudaMemcpy(values.data(), device, bytes_float, cudaMemcpyDeviceToHost);
                    if (status != cudaSuccess) {
                        std::cerr << "[CUDA ERROR] verifying " << name << ": " << cudaGetErrorString(status) << '\n';
                        return false;
                    }
                    if (std::memcmp(values.data(), host, bytes_float) != 0) {
                        std::cerr << "[CUDA ERROR] initialized " << name << " differs from CPU\n";
                        return false;
                    }
                    return true;
                };
                if (!compare_field("fuel", d_fuel_curr, expected.fuel) ||
                    !compare_field("moisture", d_moisture, expected.moisture) ||
                    !compare_field("vegetation", d_vegetation, expected.vegetation) ||
                    !compare_field("elevation", d_elevation, expected.elevation)) return false;
            }
        } else {
            const nvtx::ScopedRange upload_range("cuda.host_to_device", nvtx::teal, 2U);
            const auto start = std::chrono::steady_clock::now();
            CUDA_CHECK(cudaMemcpy(d_state_curr, view.state, bytes_state, cudaMemcpyHostToDevice));
            CUDA_CHECK(cudaMemcpy(d_fuel_curr, view.fuel, bytes_float, cudaMemcpyHostToDevice));
            CUDA_CHECK(cudaMemcpy(d_elevation, view.elevation, bytes_float, cudaMemcpyHostToDevice));
            CUDA_CHECK(cudaMemcpy(d_moisture, view.moisture, bytes_float, cudaMemcpyHostToDevice));
            CUDA_CHECK(cudaMemcpy(d_vegetation, view.vegetation, bytes_float, cudaMemcpyHostToDevice));
            timings.host_to_device_seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        }

        constexpr float pi = 3.14159265358979323846f;
        const float radians = config.wind_direction_degrees * pi / 180.0f;
        const float wind_x = std::cos(radians);
        const float wind_y = std::sin(radians);
        const std::uint64_t scenario_seed = ember::scenario_seed(config.seed, static_cast<std::uint64_t>(scenario_id));

        const dim3 block_dim(16, 16);
        const dim3 grid_dim((width + 15) / 16, (height + 15) / 16);

        workspace.kernel_start_time_ = std::chrono::steady_clock::now();

        {
            const nvtx::ScopedRange timesteps_range("cuda.timesteps", nvtx::orange, 2U);
            for (std::size_t step = 0; step < config.max_steps; ++step) {
                step_stencil_kernel<<<grid_dim, block_dim>>>(
                    width, height, scenario_seed, static_cast<uint64_t>(step),
                    static_cast<float>(config.base_spread), static_cast<float>(config.burn_rate),
                    static_cast<float>(config.wind_strength), wind_x, wind_y,
                    static_cast<float>(config.slope_scale),
                    d_fuel_curr, d_fuel_next, d_elevation, d_moisture, d_vegetation,
                    d_state_curr, d_state_next
                );

                std::swap(d_state_curr, d_state_next);
                std::swap(d_fuel_curr, d_fuel_next);
            }
        }

        // Save final device pointers holding the converged or final simulation state
        workspace.result_state_curr_ = d_state_curr;
        workspace.result_fuel_curr_ = d_fuel_curr;

        return true;
    }

    /**
     * @brief Synchronizes GPU execution, measures kernel duration, and downloads results to host memory.
     */
    bool sync_and_download_scenario_cuda(
        const SimulationConfig& config,
        GridBuffers& buffers,
        CudaWorkspace& workspace,
        std::size_t& completed_steps,
        CudaScenarioTimings& timings)
    {
        const std::size_t num_cells = config.width * config.height;
        const std::size_t bytes_state = num_cells * sizeof(CellState);
        const std::size_t bytes_float = num_cells * sizeof(float);

        // Block host CPU until all enqueued GPU kernels finish
        {
            const nvtx::ScopedRange synchronize_range("cuda.synchronize", nvtx::red, 2U);
            CUDA_CHECK(cudaDeviceSynchronize());
        }

        const auto t_end = std::chrono::steady_clock::now();
        timings.kernel_seconds = std::chrono::duration<double>(t_end - workspace.kernel_start_time_).count();
        completed_steps = config.max_steps;

        auto view = buffers.current_view();
        {
            const nvtx::ScopedRange download_range("cuda.device_to_host", nvtx::teal, 2U);
            const auto start = std::chrono::steady_clock::now();
            CUDA_CHECK(cudaMemcpy(view.state, workspace.result_state_curr_, bytes_state, cudaMemcpyDeviceToHost));
            CUDA_CHECK(cudaMemcpy(view.fuel, workspace.result_fuel_curr_, bytes_float, cudaMemcpyDeviceToHost));
            if (config.synthetic_init_backend == SyntheticInitBackend::Cuda) {
                CUDA_CHECK(cudaMemcpy(view.moisture, workspace.moisture_, bytes_float, cudaMemcpyDeviceToHost));
                CUDA_CHECK(cudaMemcpy(view.vegetation, workspace.vegetation_, bytes_float, cudaMemcpyDeviceToHost));
                CUDA_CHECK(cudaMemcpy(view.elevation, workspace.elevation_, bytes_float, cudaMemcpyDeviceToHost));
            }
            timings.device_to_host_seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        }

        return true;
    }

    /**
     * @brief Synchronous composite wrapper combining asynchronous launch, device synchronization, and D2H download.
     */
    bool run_scenario_cuda(
        const SimulationConfig& config,
        std::size_t scenario_id,
        GridBuffers& buffers,
        CudaWorkspace& workspace,
        std::size_t& completed_steps,
        CudaScenarioTimings& timings)
    {
        const nvtx::ScopedRange scenario_range("cuda.scenario", nvtx::blue, 1U);
        if (!launch_scenario_cuda(config, scenario_id, buffers, workspace, timings)) return false;
        return sync_and_download_scenario_cuda(config, buffers, workspace, completed_steps, timings);
    }
} // namespace ember
