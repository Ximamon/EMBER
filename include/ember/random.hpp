#pragma once

#include <cstdint>

namespace ember {

constexpr std::uint64_t mix64(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

constexpr std::uint64_t hash_combine(std::uint64_t seed, std::uint64_t value) noexcept {
    return mix64(seed ^ mix64(value + 0x517cc1b727220a95ULL));
}

constexpr std::uint64_t scenario_seed(std::uint64_t global_seed, std::uint64_t scenario_id) noexcept {
    return hash_combine(global_seed, scenario_id);
}

constexpr std::uint64_t keyed_hash(
    std::uint64_t seed,
    std::uint64_t tag,
    std::uint64_t first,
    std::uint64_t second = 0) noexcept {
    return hash_combine(hash_combine(hash_combine(seed, tag), first), second);
}

inline double uniform01(std::uint64_t bits) noexcept {
    return static_cast<double>(bits >> 11U) * 0x1.0p-53;
}

inline float uniform_range(std::uint64_t bits, float minimum, float maximum) noexcept {
    return minimum + static_cast<float>(uniform01(bits)) * (maximum - minimum);
}

namespace random_tag {
constexpr std::uint64_t fuel = 0x4655454cULL;
constexpr std::uint64_t moisture = 0x4d4f4953ULL;
constexpr std::uint64_t vegetation = 0x56454745ULL;
constexpr std::uint64_t elevation = 0x454c4556ULL;
constexpr std::uint64_t non_combustible = 0x4e4f4e43ULL;
constexpr std::uint64_t spread = 0x53505244ULL;
} // namespace random_tag

} // namespace ember
