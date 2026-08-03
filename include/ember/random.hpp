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

#include <cstdint>

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

} // namespace ember
