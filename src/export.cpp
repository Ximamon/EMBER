/**
 * @file export.cpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Implementation of the export functionality.
 * @version 0.1
 * @date 29/7/2026
 * 
 * 
 */

#include "ember/export.hpp"

#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>

namespace ember {
namespace {

std::ofstream open_output(const std::filesystem::path& path, std::ios::openmode mode = std::ios::out) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream stream(path, mode);
    if (!stream) {
        throw std::runtime_error("could not open output file: " + path.string());
    }
    return stream;
}

} // namespace

void export_grid_csv(const std::filesystem::path& path, ConstGridView grid) {
    auto output = open_output(path);
    output << "x,y,state,fuel,moisture,vegetation,elevation\n";
    output << std::setprecision(9);
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t column = 0; column < grid.width; ++column) {
            const auto index = row * grid.width + column;
            output << column << ',' << row << ',' << to_string(grid.state[index]) << ','
                   << grid.fuel[index] << ',' << grid.moisture[index] << ','
                   << grid.vegetation[index] << ',' << grid.elevation[index] << '\n';
        }
    }
    if (!output) {
        throw std::runtime_error("failed while writing grid CSV: " + path.string());
    }
}

void export_grid_ppm(const std::filesystem::path& path, ConstGridView grid) {
    auto output = open_output(path, std::ios::out | std::ios::binary);
    output << "P6\n" << grid.width << ' ' << grid.height << "\n255\n";
    for (std::size_t index = 0; index < grid.width * grid.height; ++index) {
        unsigned char color[3]{};
        switch (grid.state[index]) {
        case CellState::Unburned:
            color[0] = 35; color[1] = 135; color[2] = 55;
            break;
        case CellState::Burning:
            color[0] = 255; color[1] = 90; color[2] = 15;
            break;
        case CellState::Burned:
            color[0] = 35; color[1] = 35; color[2] = 35;
            break;
        case CellState::NonCombustible:
            color[0] = 80; color[1] = 105; color[2] = 135;
            break;
        }
        output.write(reinterpret_cast<const char*>(color), 3);
    }
    if (!output) {
        throw std::runtime_error("failed while writing grid PPM: " + path.string());
    }
}

void export_summary_csv(const std::filesystem::path& path, const BatchStatistics& statistics) {
    auto output = open_output(path);
    output << "record_type,scenario_id,scenario_seed,steps_executed,termination,extinguished_at_step,"
              "burned_cells,burned_percent,cell_updates,initialization_seconds,simulation_seconds,"
              "total_core_seconds,throughput_cell_updates_per_second,mean_scenario_seconds,"
              "completed_scenarios,extinguished_scenarios,max_steps_scenarios\n";
    output << std::setprecision(12);
    for (const auto& scenario : statistics.scenario_results) {
        output << "scenario," << scenario.scenario_id << ',' << scenario.scenario_seed << ','
               << scenario.steps_executed << ',' << to_string(scenario.termination) << ','
               << scenario.extinguished_at_step << ',' << scenario.burned_cells << ','
               << scenario.burned_percent << ',' << scenario.cell_updates << ','
               << scenario.initialization_seconds << ',' << scenario.simulation_seconds << ','
               << scenario.total_core_seconds << ','
               << scenario.throughput_cell_updates_per_second << ",,,,\n";
    }
    output << "batch,,,,,,," << statistics.mean_burned_percent << ','
           << statistics.total_cell_updates << ',' << statistics.total_initialization_seconds << ','
           << statistics.total_simulation_seconds << ',' << statistics.total_core_seconds << ','
           << statistics.throughput_cell_updates_per_second << ','
           << statistics.mean_scenario_seconds << ',' << statistics.completed_scenarios << ','
           << statistics.extinguished_scenarios << ',' << statistics.max_steps_scenarios << '\n';
    if (!output) {
        throw std::runtime_error("failed while writing summary CSV: " + path.string());
    }
}

} // namespace ember
