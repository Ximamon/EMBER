#include "ember/cuda_simulation.hpp"
#include "ember/random.hpp"

#include <cuda_runtime.h>
#include <iostream>
#include <chrono>
#include <cstdint>
#include <algorithm>

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
    } while(0)                                                                \


    // ============================================================================
    // STATE-LESS DETERMINISTIC RANDOM NUMBER GENERATOR (GPU DEVICE INTRINSICS)
    // ============================================================================

    __device__ __forceinline__ uint64_t cuda_mix64(uint64_t value) {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31U);
    }

    __device__ __forceinline__ uint64_t cuda_hash_combine(uint64_t seed, uint64_t value) {
        return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
    }

    __device__ __forceinline__ uint64_t cuda_keyed_hash(
        uint64_t seed, uint64_t tag, uint64_t step, uint64_t cell)
    {
        uint64_t h = seed;
        h = cuda_hash_combine(h, tag);
        h = cuda_hash_combine(h, step);
        h = cuda_hash_combine(h, cell);
        return cuda_mix64(h);
    }

    __device__ __forceinline__ double cuda_uniform01(uint64_t bits) {
        // Escala exactamente a [0, 1) en doble precisión idéntico a la CPU
        return static_cast<double>(bits >> 11U) * 0x1.0p-53;
    }

    // ============================================================================
    // 2D PHYSICAL PROPAGATION KERNEL (MOORE STENCIL)
    // ============================================================================

    __global__ void step_stencil_kernel(
        int width,
        int height,
        uint64_t scenario_seed,
        uint64_t step_index,
        float base_spread,
        float burn_rate,
        const float* __restrict__ fuel_in,
        float* __restrict__ fuel_out,
        const float* __restrict__ elevation,
        const uint8_t* __restrict__ state_in,
        uint8_t* __restrict__ state_out
    ) {
        // Cells to GPUs Threads
        const int x = blockIdx.x * blockDim.x + threadIdx.x;
        const int y = blockIdx.y * blockDim.y + threadIdx.y;

        if (x >= width || y >= height) return;

        const int idx = y * width + x;
        const auto current_state = static_cast<CellState>(state_in[idx]);

        // Cells without fuel or already burned
        if (current_state == CellState::NonCombustible || current_state == CellState::Burned) {
            state_out[idx] = static_cast<uint8_t>(current_state);
            fuel_out[idx] = fuel_in[idx];
            return;
        }

        // Cells burning
        if (current_state == CellState::Burning) {
            const float remaining_fuel = fuel_in[idx] - burn_rate;
            const float clamped_fuel = remaining_fuel > 0.0f ? remaining_fuel : 0.0f;
            fuel_out[idx] = clamped_fuel;
            state_out[idx] = static_cast<uint8_t>(
                clamped_fuel <= 0.0f ? CellState::Burned : CellState::Burning
            );
            return;
        }

        // Cells unburned -> Explore Moore neighborhood (3x3)
        double probability_not_ignited = 1.0;

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
                if (static_cast<CellState>(state_in[n_idx]) == CellState::Burning) {
                    // Distancia euclidiana: ortogonal = 1.0, diagonal = sqrt(2)
                    const float dist = (row_offset == 0 || col_offset == 0) ? 1.0f : 1.41421356f;
                    const float p_cell = base_spread / dist;
                    probability_not_ignited *= (1.0 - static_cast<double>(p_cell));
                }
            }
        }

        fuel_out[idx] = fuel_in[idx];

        // Shortpath: if no neighbour is burning, cell stays unburned
        if (probability_not_ignited >= 1.0) {
            state_out[idx] = static_cast<uint8_t>(CellState::Unburned);
            return;
        }

        //
        const double ignition_probability = 1.0 - probability_not_ignited;
        const double draw = cuda_uniform01(cuda_keyed_hash(
            scenario_seed,
            static_cast<uint64_t>(random_tag::spread),
            step_index,
            static_cast<uint64_t>(idx)
        ));
        
        state_out[idx] = static_cast<uint8_t>(
            draw < ignition_probability ? CellState::Burning : CellState::Unburned
        );
    }


    // ============================================================================
    // CONTROLLER AND VRAM MANAGER (HOST RUNNER)
    // ============================================================================
    bool check_cuda_device() {
        int device_count = 0;
        cudaError_t err = cudaGetDeviceCount(&device_count);
        if (err != cudaSuccess || device_count == 0) {
            std::cerr << "No GPUs avalible\n";
            return false;
        }

        cudaDeviceProp prop;
        cudaGetDeviceProperties(&prop, 0);
        std::cout << "[CUDA] Device: " << prop.name 
                << " | Compute Capability: " << prop.major << "." << prop.minor
                << " | VRAM Total: " << prop.totalGlobalMem / (1024 * 1024 * 1024) << " GB\n";
        return true;
    }

    bool run_scenario_cuda(
        const SimulationConfig& config,
        std::size_t scenario_id,
        GridBuffers& buffers,
        std::size_t& completed_steps,
        double& kernel_time_seconds
    ) {
        const int width = static_cast<int>(config.width);
        const int height = static_cast<int>(config.height);
        const std::size_t num_cells = config.width * config.height;

        const std::size_t bytes_state = num_cells * sizeof(uint8_t);
        const std::size_t bytes_float = num_cells * sizeof(float);

        // Pointers to device memory
        uint8_t *d_state_curr = nullptr;
        uint8_t *d_state_next = nullptr;
        float *d_fuel_curr = nullptr;
        float *d_fuel_next = nullptr;
        float *d_elevation = nullptr;

        // Allocate device memory
        CUDA_CHECK(cudaMalloc(&d_state_curr, bytes_state));
        CUDA_CHECK(cudaMalloc(&d_state_next, bytes_state));
        CUDA_CHECK(cudaMalloc(&d_fuel_curr, bytes_float));
        CUDA_CHECK(cudaMalloc(&d_fuel_next, bytes_float));
        CUDA_CHECK(cudaMalloc(&d_elevation, bytes_float));

        // Inital transfer Host -> Device (only once per scenario)
        auto view = buffers.current_view();
        CUDA_CHECK(cudaMemcpy(d_state_curr, view.state, bytes_state, cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d_fuel_curr, view.fuel, bytes_float, cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d_elevation, view.elevation, bytes_float, cudaMemcpyHostToDevice));

        // Grid config: 2D 16x16 threads per block
        const dim3 block_dim(16, 16);
        const dim3 grid_dim((width + 15) / 16, (height + 15) / 16);

        const auto t_start = std::chrono::steady_clock::now();

        for (std::size_t step = 0; step < config.max_steps; ++step) {
            step_stencil_kernel<<<grid_dim, block_dim>>>(
                width, height,
                config.seed + scenario_id,
                step,
                static_cast<float>(config.base_spread),
                static_cast<float>(config.burn_rate),
                d_fuel_curr, d_fuel_next,
                d_elevation,
                d_state_curr, d_state_next
            );

            // Swap buffers for next iteration
            std::swap(d_state_curr, d_state_next);
            std::swap(d_fuel_curr, d_fuel_next);
        }

        // Synchronize and measure kernel execution time
        CUDA_CHECK(cudaDeviceSynchronize());
        const auto t_end = std::chrono::steady_clock::now();
        kernel_time_seconds = std::chrono::duration<double>(t_end - t_start).count();
        completed_steps = config.max_steps;

        // Transfer results back to host
        CUDA_CHECK(cudaMemcpy(view.state, d_state_curr, bytes_state, cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaMemcpy(view.fuel, d_fuel_curr, bytes_float, cudaMemcpyDeviceToHost));

        // Free device memory
        cudaFree(d_state_curr);
        cudaFree(d_state_next);
        cudaFree(d_fuel_curr);
        cudaFree(d_fuel_next);
        cudaFree(d_elevation);

        return true;
    }

}