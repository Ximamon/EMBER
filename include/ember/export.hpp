/**
 * @file export.hpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Export functionality for the Ember simulation.
 * @version 0.1
 * @date 29/7/2026
 * 
 * 
 */

#pragma once

#include "ember/grid.hpp"
#include "ember/statistics.hpp"

#include <filesystem>

namespace ember {

/**
 * @brief Exports the grid data to a CSV file.
 * 
 * @param path The path to the output CSV file.
 * @param grid The grid data to export.
 */
void export_grid_csv(const std::filesystem::path& path, ConstGridView grid);
/**
 * @brief Exports the grid data to a PPM file.
 * 
 * @param path The path to the output PPM file.
 * @param grid The grid data to export.
 */
void export_grid_ppm(const std::filesystem::path& path, ConstGridView grid);
/**
 * @brief Exports the batch statistics to a CSV file.
 * 
 * @param path The path to the output CSV file.
 * @param statistics The batch statistics to export.
 */
void export_summary_csv(const std::filesystem::path& path, const BatchStatistics& statistics);

} // namespace ember
