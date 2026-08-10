/**
 * @file config.hpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Configuration structure for the Ember simulation.
 * @version 0.1
 * @date 29/7/2026
 * 
 * 
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ember {

/**
 * @class IgnitionPoint
 * @brief Represents a point in the grid where the fire is ignited.
 * This structure holds the x and y coordinates of the ignition point within the simulation grid.
 * 
 */
struct IgnitionPoint {
    std::size_t x{};
    std::size_t y{};
};

/**
 * @enum ExportFormat
 * @brief Enumerates the available formats for exporting simulation data.
 * The available formats include:
 * - None: No export.
 * - Csv: Export data in CSV format.
 * - Ppm: Export data in PPM image format.
 * - Both: Export data in both CSV and PPM formats.
 */
enum class ExportFormat {
    None,
    Csv,
    Ppm,
    Both
};

/**
 * @class SimulationConfig
 * @brief Configuration structure for the Ember simulation.
 * This structure holds all the parameters needed to initialize and run the fire simulation.
 * The first group of parameters defines the grid dimensions, simulation steps, and scenarios. 
 * The second group defines environmental factors such as wind direction, wind strength, base spread, burn rate, and slope scale. 
 * The third group defines terrain properties including fuel, moisture, vegetation, and elevation ranges. 
 * The fourth group includes ignition points and output settings for exporting simulation data.
 */
struct SimulationConfig {
    /// @brief Width of the simulation grid. Must be greater than zero.
    std::size_t width{512};
    /// @brief Height of the simulation grid. Must be greater than zero.
    std::size_t height{512};
    /// @brief  Maximum number of simulation steps. Must be greater than zero.
    std::size_t max_steps{500};
    /// @brief Number of simulation scenarios. Must be greater than zero.
    std::size_t scenarios{1};
    /// @brief Random seed for the simulation. Must be a positive integer.
    std::uint64_t seed{42};

    /// @brief Wind direction in degrees. Must be a finite value.
    float wind_direction_degrees{45.0F};
    /// @brief Wind strength as a fraction of the maximum possible wind strength. Must be in the range [0, 1].
    float wind_strength{0.4F};
    /// @brief Base spread factor. Must be a positive value.
    float base_spread{0.25F};
    /// @brief Burn rate factor. Must be a positive value.
    float burn_rate{0.20F};
    /// @brief Slope scale factor. Must be a positive value.
    float slope_scale{0.25F};

    /// @brief Minimum fuel value for the terrain. Must be in the range [0, 1].
    float min_fuel{0.40F};
    /// @brief Maximum fuel value for the terrain. Must be in the range [0, 1].
    float max_fuel{1.00F};
    /// @brief Minimum moisture value for the terrain. Must be in the range [0, 1].
    float min_moisture{0.00F};
    /// @brief Maximum moisture value for the terrain. Must be in the range [0, 1].
    float max_moisture{1.00F};
    /// @brief Minimum vegetation value for the terrain. Must be in the range [0, 1].
    float min_vegetation{0.75F};
    /// @brief Maximum vegetation value for the terrain. Must be in the range [0, 1].
    float max_vegetation{1.25F};
    /// @brief Minimum elevation value for the terrain. Must be in the range [0, 1].
    float min_elevation{0.00F};
    /// @brief Maximum elevation value for the terrain. Must be in the range [0, 1].
    float max_elevation{1.00F};
    /// @brief Fraction of non-combustible material in the terrain. Must be in the range [0, 1].
    float non_combustible_fraction{0.05F};

    /// @brief List of ignition points where the fire will start. Each point must be within the grid dimensions.
    std::vector<IgnitionPoint> ignitions;
    /// @brief Directory where the simulation output will be saved. Must not be empty if export_format is not None.
    std::string output_directory;
    /// @brief Format for exporting simulation data. Must be one of the values defined in the ExportFormat enum.
    ExportFormat export_format{ExportFormat::None};
};

/**
 * @brief Calculates the total number of cells and checks for potential overflow.
 * 
 * @param config The simulation configuration containing width and height.
 * @return std::size_t The total number of cells (width * height).
 * @throws std::overflow_error If width * height exceeds the maximum value of std::size_t.
 */
std::size_t checked_cell_count(const SimulationConfig& config);

/**
 * @brief Validates the simulation configuration parameters.
 * 
 * Checks that all parameters (dimensions, probabilities, fractions) are within
 * their valid mathematical and logical bounds.
 * 
 * @param config The simulation configuration to validate.
 * @throws std::invalid_argument If any parameter is out of bounds.
 */
void validate_config(const SimulationConfig& config);

} // namespace ember
