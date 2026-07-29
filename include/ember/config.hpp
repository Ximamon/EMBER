#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ember {

struct IgnitionPoint {
    std::size_t x{};
    std::size_t y{};
};

enum class ExportFormat {
    None,
    Csv,
    Ppm,
    Both
};

struct SimulationConfig {
    std::size_t width{512};
    std::size_t height{512};
    std::size_t max_steps{500};
    std::size_t scenarios{1};
    std::uint64_t seed{42};

    float wind_direction_degrees{45.0F};
    float wind_strength{0.4F};
    float base_spread{0.25F};
    float burn_rate{0.20F};
    float slope_scale{0.25F};

    float min_fuel{0.40F};
    float max_fuel{1.00F};
    float min_moisture{0.00F};
    float max_moisture{1.00F};
    float min_vegetation{0.75F};
    float max_vegetation{1.25F};
    float min_elevation{0.00F};
    float max_elevation{1.00F};
    float non_combustible_fraction{0.05F};

    std::vector<IgnitionPoint> ignitions;
    std::string output_directory;
    ExportFormat export_format{ExportFormat::None};
};

std::size_t checked_cell_count(const SimulationConfig& config);
void validate_config(const SimulationConfig& config);

} // namespace ember
