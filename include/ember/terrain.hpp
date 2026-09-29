/**
 * @file terrain.hpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Data structures and loaders for geographic terrain datasets (ESRI ASCII / ZAFM).
 * @version 0.5
 * @date 29/7/2026
 * 
 * 
 */

#pragma once

#include "ember/config.hpp"
#include <filesystem>
#include <memory>
#include <vector>

namespace ember {

/**
 * @struct TerrainData
 * @brief Immutable geographic terrain dataset shared across simulation scenarios.
 * 
 * Stores raster fuel models and geographic metadata imported from an ESRI ASCII Grid (.asc)
 * accompanied by an EPSG:32631 (WGS 84 / UTM Zone 31N) projection sidecar (.prj).
 * 
 * The data is stored in row-major order with rows running from North to South (top-to-bottom),
 * matching standard GIS raster specifications. The instance is wrapped in a `std::shared_ptr<const TerrainData>`
 * so that all scenarios in a batch can share the same underlying geography without copying.
 */
struct TerrainData {
    /// @brief Number of grid columns (grid width in cells).
    std::size_t width{};

    /// @brief Number of grid rows (grid height in cells).
    std::size_t height{};

    /// @brief Westernmost X coordinate (UTM Easting in meters) of the lower-left corner.
    double xllcorner{};

    /// @brief Southernmost Y coordinate (UTM Northing in meters) of the lower-left corner.
    double yllcorner{};

    /// @brief Spatial resolution of each cell in meters (isotropic square cells).
    double cell_size_m{};

    /// @brief EPSG identifier for the spatial reference system (defaults to 32631: UTM Zone 31N).
    int epsg{32631};

    /// @brief Flattened row-major 1D array of categorical fuel model codes (size = width * height).
    std::vector<std::uint16_t> codes;

    /// @brief Flattened row-major 1D mask (1 = valid cell, 0 = NODATA/exterior cell).
    std::vector<std::uint8_t> valid;

    /// @brief Total count of valid (non-NODATA) cells within the grid.
    std::size_t valid_cells{};

    /// @brief Total count of combustible vegetation cells (fuel code >= 100).
    std::size_t combustible_cells{};
};

/**
 * @brief Checks if a given integer code corresponds to a known ZAFM European fuel model.
 * 
 * Supported fuel model codes include:
 * - Non-combustible / special: 0 (NODATA/unburnable), 91 (urban/built), 92 (agricultural),
 *   93 (bare soil/rock), 98 (water bodies).
 * - Combustible fuel types (ZAFM models >= 100):
 *   - Grasslands: 102, 104, 106, 107, 108, 109
 *   - Shrublands: 142, 143, 145, 147, 148, 149
 *   - Forest / Slash: 161, 162, 163, 165, 183
 * 
 * @param code The categorical fuel code from the raster grid.
 * @return true if the code is a recognized ZAFM fuel classification code, false otherwise.
 */
bool known_fuel_code(int code) noexcept;

/**
 * @brief Determines whether a given fuel code represents combustible vegetative matter.
 * 
 * A cell is combustible if and only if its fuel model code is known and corresponds to a
 * vegetative fuel model (code >= 100). Non-combustible surfaces (code < 100, such as water,
 * urban, bare soil, or NODATA) cannot ignite or propagate fire.
 * 
 * @param code The categorical fuel code.
 * @return true if the cell is combustible (code >= 100), false if non-combustible.
 */
bool combustible_code(int code) noexcept;

/**
 * @brief Parses and validates an ESRI ASCII raster grid (.asc) and its projection sidecar (.prj).
 * 
 * Reads the 6 standard ASCII grid header entries (`ncols`, `nrows`, `xllcorner`, `yllcorner`,
 * `cellsize`, `nodata_value`), verifies UTM Zone 31N coordinates, validates the EPSG:32631 projection
 * sidecar, and loads the grid cells into memory while computing combustible and valid cell counts.
 * 
 * Memory allocation and dimensions are guarded against overflow and capped at a maximum of 16 million cells.
 * 
 * @param path Filesystem path to the .asc terrain raster file.
 * @return std::shared_ptr<const TerrainData> Shared pointer to the immutable parsed terrain dataset.
 * @throws std::invalid_argument If the file is inaccessible, the header is malformed or incomplete,
 *                               dimensions exceed limits, coordinates are outside valid UTM bounds,
 *                               the .prj sidecar is missing/invalid, fuel codes are unrecognized,
 *                               or the dataset contains no combustible cells.
 */
std::shared_ptr<const TerrainData> load_terrain(const std::filesystem::path& path);
// Read metric heights after checking exact grid alignment and coverage of valid terrain.
std::shared_ptr<const std::vector<float>> load_elevation(
    const std::filesystem::path& path, const TerrainData& terrain);
// Load once if needed, resolve dimensions and ignition, and validate before allocation.

/**
 * @brief Resolves, loads, and harmonizes terrain specifications with the simulation configuration.
 * 
 * If a terrain path is specified in `config.terrain_path`, this function loads the dataset (if not already loaded),
 * reconciles explicit vs terrain dimensions, assigns default ignition points to the closest combustible cell
 * to the grid center if none were configured, and ensures that all ignitions land strictly on combustible cells.
 * 
 * In the current version, real terrain execution is guarded to CPU backends and will reject execution if
 * compiled with CUDA support (`EMBER_ENABLE_CUDA=ON`).
 * 
 * @param config Input simulation configuration.
 * @return SimulationConfig Fully validated and resolved configuration with attached terrain data.
 * @throws std::invalid_argument If real terrain is invoked on a CUDA build, if dimensions conflict,
 *                               or if any ignition point falls on a non-combustible or out-of-bounds cell.
 */
SimulationConfig resolve_terrain_config(SimulationConfig config);

} // namespace ember
