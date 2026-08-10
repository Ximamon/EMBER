/**
 * @file config.cpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Implementation of the simulation configuration.
 * @version 0.1
 * @date 29/7/2026
 * 
 * 
 */

#include "ember/config.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace ember {
namespace {

bool in_unit_interval(float value) {
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

void validate_range(float minimum, float maximum, const char* name) {
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum) {
        throw std::invalid_argument(std::string(name) + " range is invalid");
    }
}

} // namespace

std::size_t checked_cell_count(const SimulationConfig& config) {
    if (config.width == 0 || config.height == 0) {
        throw std::invalid_argument("grid width and height must be greater than zero");
    }
    if (config.width > std::numeric_limits<std::size_t>::max() / config.height) {
        throw std::invalid_argument("grid dimensions overflow the addressable cell count");
    }
    return config.width * config.height;
}

void validate_config(const SimulationConfig& config) {
    const auto cell_count = checked_cell_count(config);
    if (config.max_steps == 0) {
        throw std::invalid_argument("steps must be greater than zero");
    }
    if (config.scenarios == 0) {
        throw std::invalid_argument("scenarios must be greater than zero");
    }
    if (cell_count > std::numeric_limits<std::uint64_t>::max() / config.max_steps) {
        throw std::invalid_argument("cell update count overflows 64-bit statistics");
    }
    const auto maximum_updates_per_scenario =
        static_cast<std::uint64_t>(cell_count) * static_cast<std::uint64_t>(config.max_steps);
    if (maximum_updates_per_scenario >
        std::numeric_limits<std::uint64_t>::max() / config.scenarios) {
        throw std::invalid_argument("batch cell update count overflows 64-bit statistics");
    }
    if (!std::isfinite(config.wind_direction_degrees)) {
        throw std::invalid_argument("wind direction must be finite");
    }
    if (!in_unit_interval(config.wind_strength) || !in_unit_interval(config.base_spread) ||
        !in_unit_interval(config.burn_rate) || !in_unit_interval(config.non_combustible_fraction)) {
        throw std::invalid_argument("wind strength, base spread, burn rate and non-combustible fraction must be in [0, 1]");
    }
    if (config.burn_rate <= 0.0F) {
        throw std::invalid_argument("burn rate must be greater than zero");
    }
    if (!std::isfinite(config.slope_scale) || config.slope_scale <= 0.0F) {
        throw std::invalid_argument("slope scale must be finite and greater than zero");
    }
    validate_range(config.min_fuel, config.max_fuel, "fuel");
    validate_range(config.min_moisture, config.max_moisture, "moisture");
    validate_range(config.min_vegetation, config.max_vegetation, "vegetation");
    validate_range(config.min_elevation, config.max_elevation, "elevation");
    if (config.min_fuel < 0.0F || config.max_fuel > 1.0F ||
        config.min_moisture < 0.0F || config.max_moisture > 1.0F ||
        config.min_elevation < 0.0F || config.max_elevation > 1.0F ||
        config.min_vegetation < 0.0F) {
        throw std::invalid_argument("terrain ranges are outside their supported bounds");
    }
    for (const auto& point : config.ignitions) {
        if (point.x >= config.width || point.y >= config.height) {
            throw std::invalid_argument("ignition point is outside the grid");
        }
    }
    if (config.export_format != ExportFormat::None && config.output_directory.empty()) {
        throw std::invalid_argument("--export requires --output");
    }
}

} // namespace ember
