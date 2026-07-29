#pragma once

#include "ember/grid.hpp"
#include "ember/statistics.hpp"

#include <filesystem>

namespace ember {

void export_grid_csv(const std::filesystem::path& path, ConstGridView grid);
void export_grid_ppm(const std::filesystem::path& path, ConstGridView grid);
void export_summary_csv(const std::filesystem::path& path, const BatchStatistics& statistics);

} // namespace ember
