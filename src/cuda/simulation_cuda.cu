#include "ember/cuda_simulation.hpp"
#include "ember/random.hpp"
#include "ember/nvtx.hpp"

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

    __device__ __forceinline__ float cuda_clamp01(float value) {
        return fminf(fmaxf(value, 0.0f), 1.0f);
    }

    // ============================================================================
    // IGNITION PROBABILITY CALCULATION FOR NEIGHBORING CELLS
    // ============================================================================

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

        // Celdas calcinadas o no combustibles
        if (current_state == CellState::NonCombustible || current_state == CellState::Burned) {
            state_out[idx] = current_state;
            fuel_out[idx] = fuel_in[idx];
            return;
        }

        // Celdas ardiendo: consumen combustible según burn_rate
        if (current_state == CellState::Burning) {
            const float remaining_fuel = fuel_in[idx] - burn_rate;
            const float clamped_fuel = remaining_fuel > 0.0f ? remaining_fuel : 0.0f;
            fuel_out[idx] = clamped_fuel;
            state_out[idx] = (clamped_fuel <= 0.0f) ? CellState::Burned : CellState::Burning;
            return;
        }

        // Celda combustible no quemada (Unburned): explorar vecindario de Moore de 8 celdas
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

        // Cortocircuito estocástico: si ningún vecino arde, la celda permanece Unburned
        if (probability_not_ignited >= 1.0) {
            state_out[idx] = CellState::Unburned;
            return;
        }

        // Evaluación determinista mediante el hash congruente con la CPU
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
        double& kernel_time_seconds)
    {
        const nvtx::ScopedRange scenario_range("cuda.scenario", nvtx::blue, 1U);
        const int width = static_cast<int>(config.width);
        const int height = static_cast<int>(config.height);
        const std::size_t num_cells = config.width * config.height;

        const std::size_t bytes_state = num_cells * sizeof(CellState);
        const std::size_t bytes_float = num_cells * sizeof(float);

        // Punteros a memoria en la tarjeta NVIDIA A100
        CellState *d_state_curr = nullptr;
        CellState *d_state_next = nullptr;
        float *d_fuel_curr = nullptr;
        float *d_fuel_next = nullptr;
        float *d_elevation = nullptr;
        float *d_moisture = nullptr;
        float *d_vegetation = nullptr;

        {
            const nvtx::ScopedRange allocation_range("cuda.allocate", nvtx::purple, 2U);
            CUDA_CHECK(cudaMalloc(&d_state_curr, bytes_state));
            CUDA_CHECK(cudaMalloc(&d_state_next, bytes_state));
            CUDA_CHECK(cudaMalloc(&d_fuel_curr, bytes_float));
            CUDA_CHECK(cudaMalloc(&d_fuel_next, bytes_float));
            CUDA_CHECK(cudaMalloc(&d_elevation, bytes_float));
            CUDA_CHECK(cudaMalloc(&d_moisture, bytes_float));
            CUDA_CHECK(cudaMalloc(&d_vegetation, bytes_float));
        }

        auto view = buffers.current_view();
        if (view.state == nullptr || view.fuel == nullptr || view.elevation == nullptr ||
            view.moisture == nullptr || view.vegetation == nullptr) {
            std::cerr << "[CUDA Error] Los buffers de entrada son nulos. Requiere simulation.initialize().\n";
            return false;
        }

        // Transferencia inicial Host -> Device (única por escenario)

        {
            const nvtx::ScopedRange upload_range("cuda.host_to_device", nvtx::teal, 2U);
            CUDA_CHECK(cudaMemcpy(d_state_curr, view.state, bytes_state, cudaMemcpyHostToDevice));
            CUDA_CHECK(cudaMemcpy(d_fuel_curr, view.fuel, bytes_float, cudaMemcpyHostToDevice));
            CUDA_CHECK(cudaMemcpy(d_elevation, view.elevation, bytes_float, cudaMemcpyHostToDevice));
            CUDA_CHECK(cudaMemcpy(d_moisture, view.moisture, bytes_float, cudaMemcpyHostToDevice));
            CUDA_CHECK(cudaMemcpy(d_vegetation, view.vegetation, bytes_float, cudaMemcpyHostToDevice));
        }

        // Precalcular vectores directores de viento para evitar funciones trigonométricas en la GPU
        constexpr float pi = 3.14159265358979323846f;
        const float radians = config.wind_direction_degrees * pi / 180.0f;
        const float wind_x = std::cos(radians);
        const float wind_y = std::sin(radians);

        // Derivar la semilla canónica del escenario exactamente igual que la CPU
        const std::uint64_t scenario_seed = ember::scenario_seed(
            config.seed, static_cast<std::uint64_t>(scenario_id)
        );

        const dim3 block_dim(16, 16);
        const dim3 grid_dim((width + 15) / 16, (height + 15) / 16);

        const auto t_start = std::chrono::steady_clock::now();

        {
            const nvtx::ScopedRange timesteps_range("cuda.timesteps", nvtx::orange, 2U);
            for (std::size_t step = 0; step < config.max_steps; ++step) {
                step_stencil_kernel<<<grid_dim, block_dim>>>(
                    width,
                    height,
                    scenario_seed,
                    static_cast<uint64_t>(step),
                    static_cast<float>(config.base_spread),
                    static_cast<float>(config.burn_rate),
                    static_cast<float>(config.wind_strength),
                    wind_x,
                    wind_y,
                    static_cast<float>(config.slope_scale),
                    d_fuel_curr,
                    d_fuel_next,
                    d_elevation,
                    d_moisture,
                    d_vegetation,
                    d_state_curr,
                    d_state_next
                );

                // Rotación de doble buffer en registros VRAM (coste 0.00 ms)
                std::swap(d_state_curr, d_state_next);
                std::swap(d_fuel_curr, d_fuel_next);
            }

            // Maintain synchronization within `cuda.timesteps` to measure
            // actual asynchronous execution on the GPU, not just kernel invocation
            const nvtx::ScopedRange synchronize_range("cuda.synchronize", nvtx::red, 2U);
            CUDA_CHECK(cudaDeviceSynchronize());
        }

        const auto t_end = std::chrono::steady_clock::now();
        kernel_time_seconds = std::chrono::duration<double>(t_end - t_start).count();
        completed_steps = config.max_steps;

        // Recuperar el resultado final de Device a Host
        {
            const nvtx::ScopedRange download_range("cuda.device_to_host", nvtx::teal, 2U);
            CUDA_CHECK(cudaMemcpy(view.state, d_state_curr, bytes_state, cudaMemcpyDeviceToHost));
            CUDA_CHECK(cudaMemcpy(view.fuel, d_fuel_curr, bytes_float, cudaMemcpyDeviceToHost));
        }

        // Liberar memoria VRAM
        {
            const nvtx::ScopedRange free_range("cuda.free", nvtx::purple, 2U);
            cudaFree(d_state_curr);
            cudaFree(d_state_next);
            cudaFree(d_fuel_curr);
            cudaFree(d_fuel_next);
            cudaFree(d_elevation);
            cudaFree(d_moisture);
            cudaFree(d_vegetation);
        }

        return true;
    }

}