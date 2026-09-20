/**
 * @file random.hpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Random number generation utilities for the Ember simulation.
 * @version 0.1
 * @date 29/7/2026
 * 
 * 
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>

namespace ember {

/**
 * @brief Mixes a 64-bit value to improve distribution.
 * Inline function that applies a series of bitwise operations and multiplications to a 64-bit integer value to produce a mixed output. 
 * This is useful for generating pseudo-random numbers with better distribution properties.
 * @param value The value to mix.
 * @return The mixed value.
 */
constexpr std::uint64_t mix64(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

/**
 * @brief Combines two 64-bit values using a hash function.
 * 
 * @param seed The seed value.
 * @param value The value to combine.
 * @return The combined hash value.
 */
constexpr std::uint64_t hash_combine(std::uint64_t seed, std::uint64_t value) noexcept {
    return mix64(seed ^ mix64(value + 0x517cc1b727220a95ULL));
}

/**
 * @brief Generates a scenario-specific seed.
 * 
 * @param global_seed The global seed value.
 * @param scenario_id The scenario ID.
 * @return The generated scenario seed.
 */
constexpr std::uint64_t scenario_seed(std::uint64_t global_seed, std::uint64_t scenario_id) noexcept {
    return hash_combine(global_seed, scenario_id);
}

/**
 * @brief Generates a keyed hash value.
 * 
 * @param seed The seed value.
 * @param tag The tag value.
 * @param first The first value to hash.
 * @param second The second value to hash.
 * @return The generated keyed hash.
 */
constexpr std::uint64_t keyed_hash(
    std::uint64_t seed,
    std::uint64_t tag,
    std::uint64_t first,
    std::uint64_t second = 0) noexcept {
    return hash_combine(hash_combine(hash_combine(seed, tag), first), second);
}

/**
 * @brief Generates a uniform random number in the range [0, 1).
 * 
 * @param bits The bits to use for generating the random number.
 * @return A double in the range [0, 1).
 */
inline double uniform01(std::uint64_t bits) noexcept {
    return static_cast<double>(bits >> 11U) * 0x1.0p-53;
}

/**
 * @brief Generates a uniform random number in the specified range.
 * 
 * @param bits The bits to use for generating the random number.
 * @param minimum The minimum value of the range.
 * @param maximum The maximum value of the range.
 * @return A float in the specified range.
 */
inline float uniform_range(std::uint64_t bits, float minimum, float maximum) noexcept {
    return minimum + static_cast<float>(uniform01(bits)) * (maximum - minimum);
}

/**
 * @namespace random_tag
 * @brief Contains tags used for keyed hashing to ensure unique random streams for different simulation aspects.
 */
namespace random_tag {
    /// @brief Tag for fuel random number generation.
    constexpr std::uint64_t fuel = 0x4655454cULL;
    /// @brief Tag for moisture random number generation.
    constexpr std::uint64_t moisture = 0x4d4f4953ULL;
    /// @brief Tag for vegetation random number generation.
    constexpr std::uint64_t vegetation = 0x56454745ULL;
    /// @brief Tag for elevation random number generation.
    constexpr std::uint64_t elevation = 0x454c4556ULL;
    /// @brief Tag for non-combustible cell random number generation.
    constexpr std::uint64_t non_combustible = 0x4e4f4e43ULL;
    /// @brief Tag for fire spread random number generation.
    constexpr std::uint64_t spread = 0x53505244ULL;
} // namespace random_tag

inline void run_rng_benchmark(std::size_t iters = 100'000'000) {
    using clock = std::chrono::high_resolution_clock;
    
    const std::uint64_t seed = 42ULL; // ULL (Unsigned Long Long) to ensure it's treated as a 64-bit unsigned integer
    const std::uint64_t step = 10ULL;

    std::cout << "========================================================\n"
              << "          EMBER STATELESS RNG MICRO-BENCHMARK           \n"
              << "========================================================\n"
              << "Iterations: " << iters << "\n\n";

    // ---------------------------------------------------------
    // TEST 1: 64 bits of keyed_hash only (mix64 + hash_combine)
    // ---------------------------------------------------------
    volatile std::uint64_t hash_sink = 0; // volatile evita que el compilador elimine el bucle (-O3)
    std::uint64_t accum_hash = 0;

    const auto start_hash = clock::now();
    for (std::size_t i = 0; i < iters; ++i) {
        accum_hash ^= keyed_hash(seed, random_tag::spread, step, static_cast<std::uint64_t>(i));
    }
    const auto end_hash = clock::now();
    hash_sink = accum_hash;

    const double sec_hash = std::chrono::duration<double>(end_hash - start_hash).count();
    const double mhash_per_sec = (static_cast<double>(iters) / sec_hash) / 1e6;
    const double ns_per_hash = (sec_hash / static_cast<double>(iters)) * 1e9;

    std::cout << "[1] Pure keyed_hash (mix64 + hash_combine):\n"
              << "  - Elapsed time: " << std::fixed << std::setprecision(6) << sec_hash << " s\n"
              << "  - Throughput:   " << std::setprecision(2) << mhash_per_sec << " MHash/s\n"
              << "  - Latency:      " << std::setprecision(3) << ns_per_hash << " ns / hash\n\n";

    // ---------------------------------------------------------
    // TEST 2: Complete RNG draw (keyed_hash + uniform01 float conversion)
    // ---------------------------------------------------------
    volatile double float_sink = 0.0;
    double accum_draw = 0.0;

    const auto start_draw = clock::now();
    for (std::size_t i = 0; i < iters; ++i) {
        const auto hash_val = keyed_hash(seed, random_tag::spread, step, static_cast<std::uint64_t>(i));
        accum_draw += uniform01(hash_val);
    }
    const auto end_draw = clock::now();
    float_sink = accum_draw;

    const double sec_draw = std::chrono::duration<double>(end_draw - start_draw).count();
    const double mdraw_per_sec = (static_cast<double>(iters) / sec_draw) / 1e6;
    const double ns_per_draw = (sec_draw / static_cast<double>(iters)) * 1e9;

    std::cout << "[2] Full RNG draw (keyed_hash + uniform01 float conversion):\n"
              << "  - Elapsed time: " << std::fixed << std::setprecision(6) << sec_draw << " s\n"
              << "  - Throughput:   " << std::setprecision(2) << mdraw_per_sec << " MDraw/s\n"
              << "  - Latency:      " << std::setprecision(3) << ns_per_draw << " ns / draw\n"
              << "========================================================\n"
              << "Sanity check (checksums): hash=" << hash_sink << ", draw=" << float_sink << "\n";
}

} // namespace ember
