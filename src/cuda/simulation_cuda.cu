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

    }

}