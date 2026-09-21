#pragma once

#include "ember/config.hpp"
#include <filesystem>
#include <memory>
#include <vector>

namespace ember {
// Shared as const: geography never changes between scenarios. Rows run north to south.
struct TerrainData {
    std::size_t width{}, height{};
    double xllcorner{}, yllcorner{}, cell_size_m{};
    int epsg{32631};
    std::vector<std::uint16_t> codes;
    std::vector<std::uint8_t> valid;
    std::size_t valid_cells{}, combustible_cells{};
};
bool known_fuel_code(int code) noexcept;
bool combustible_code(int code) noexcept;
std::shared_ptr<const TerrainData> load_terrain(const std::filesystem::path& path);
// Load once if needed, resolve dimensions and ignition, and validate before allocation.
SimulationConfig resolve_terrain_config(SimulationConfig config);
} // namespace ember
