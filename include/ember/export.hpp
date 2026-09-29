/**
 * @file export.hpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Export functionality for the Ember simulation.
 * @version 0.5
 * @date 29/7/2026
 * 
 * 
 */

#pragma once

#include "ember/grid.hpp"
#include "ember/terrain.hpp"
#include "ember/statistics.hpp"

#include <filesystem>

namespace ember {

/**
 * @brief Exports the grid data to a CSV file.
 * 
 * @param path The path to the output CSV file.
 * @param grid The grid data to export.
 * @param terrain Optional pointer to terrain dataset providing GIS fuel codes and valid mask.
 */
void export_grid_csv(const std::filesystem::path& path, ConstGridView grid, const TerrainData* terrain = nullptr);

/**
 * @brief Exports the grid data to a PPM file.
 * 
 * @param path The path to the output PPM file.
 * @param grid The grid data to export.
 * @param terrain Optional pointer to terrain dataset providing GIS valid mask for NODATA tinting.
 */
void export_grid_ppm(const std::filesystem::path& path, ConstGridView grid, const TerrainData* terrain = nullptr);
/**
 * @brief Exports metadata and run configuration for terrain scenarios (run.json).
 * 
 * @param config Simulation configuration containing terrain metadata and output directory.
 */
void export_terrain_run(const SimulationConfig& config);

/**
 * @brief Exports comprehensive 37-column batch execution summary metrics to CSV.
 * 
 * @param path Path to the output summary.csv file.
 * @param statistics Consolidated batch statistics containing per-scenario metrics.
 */
void export_summary_csv(const std::filesystem::path& path, const BatchStatistics& statistics);

} // namespace ember
