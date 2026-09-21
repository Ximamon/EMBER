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

/**
 * @brief Opens an output file stream, creating parent directories if necessary.
 * @param path The path to the file.
 * @param mode The IO mode (default is out).
 * @return The opened output file stream.
 * @throws std::runtime_error If the file could not be opened.
 */
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

void export_grid_csv(const std::filesystem::path& path, ConstGridView grid, const TerrainData* terrain) {
    auto output = open_output(path);
    output << "x,y,state,fuel,moisture,vegetation,elevation";
    if (terrain) output << ",fuel_code,valid";
    output << '\n';
    output << std::setprecision(9);
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t column = 0; column < grid.width; ++column) {
            const auto index = row * grid.width + column;
            output << column << ',' << row << ',' << to_string(grid.state[index]) << ','
                   << grid.fuel[index] << ',' << grid.moisture[index] << ','
                   << grid.vegetation[index] << ',' << grid.elevation[index];
            if (terrain) output << ',' << terrain->codes[index] << ',' << static_cast<int>(terrain->valid[index]);
            output << '\n';
        }
    }
    if (!output) {
        throw std::runtime_error("failed while writing grid CSV: " + path.string());
    }
}

void export_grid_ppm(const std::filesystem::path& path, ConstGridView grid, const TerrainData* terrain) {
    auto output = open_output(path, std::ios::out | std::ios::binary);
    output << "P6\n" << grid.width << ' ' << grid.height << "\n255\n";
    // Generate the PPM pixel data. We map CellState enum values to specific RGB 
    // colors for visual differentiation: Unburned (Green), Burning (Orange), etc.
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
        if (terrain && !terrain->valid[index]) color[0] = color[1] = color[2] = 220;
        output.write(reinterpret_cast<const char*>(color), 3);
    }
    if (!output) {
        throw std::runtime_error("failed while writing grid PPM: " + path.string());
    }
}

void export_terrain_run(const SimulationConfig& config) {
    const auto& t = *config.terrain;
    const auto directory = std::filesystem::path(config.output_directory);
    auto output = open_output(directory / "run.json");
    output << std::setprecision(17)
           << "{\n  \"schema_version\": 1,\n  \"epsg\": " << t.epsg
           << ",\n  \"width\": " << t.width << ", \"height\": " << t.height
           << ",\n  \"xllcorner\": " << t.xllcorner << ", \"yllcorner\": " << t.yllcorner
           << ",\n  \"cell_size_m\": " << t.cell_size_m
           << ",\n  \"fuel\": " << config.terrain_fuel << ", \"moisture\": " << config.terrain_moisture
           << ",\n  \"vegetation\": 1, \"elevation\": 0,\n  \"seed\": " << config.seed
           << ",\n  \"base_spread\": " << config.base_spread << ", \"burn_rate\": " << config.burn_rate
           << ",\n  \"wind_direction_degrees\": " << config.wind_direction_degrees
           << ", \"wind_strength\": " << config.wind_strength
           << ",\n  \"max_steps\": " << config.max_steps << ", \"scenarios\": " << config.scenarios
           << ",\n  \"ignitions\": [";
    for (std::size_t i = 0; i < config.ignitions.size(); ++i) {
        if (i) output << ',';
        output << '[' << config.ignitions[i].x << ',' << config.ignitions[i].y << ']';
    }
    output << "]\n}\n";
    if (!output) throw std::runtime_error("failed writing run metadata");
    if (!config.terrain_path.empty()) {
        auto source = std::filesystem::path(config.terrain_path);
        source.replace_extension(".json");
        if (std::filesystem::exists(source)) {
            const auto target = directory / "terrain-source.json";
            if (!std::filesystem::exists(target) || !std::filesystem::equivalent(source, target))
                std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing);
        }
    }
}

void export_summary_csv(const std::filesystem::path& path, const BatchStatistics& statistics) {
    auto output = open_output(path);
    
    output << "record_type,scenario_id,scenario_seed,steps_executed,termination,extinguished_at_step,"
              "burned_cells,burned_percent,cell_updates,initialization_seconds,simulation_seconds,"
              "step_compute_seconds,swap_seconds,mean_step_seconds,min_step_seconds,max_step_seconds,"
              "total_core_seconds,throughput_cell_updates_per_second,mean_scenario_seconds,"
              "completed_scenarios,extinguished_scenarios,max_steps_scenarios,"
              "valid_cells,nodata_cells,non_combustible_cells,initially_combustible_cells,"
              "burned_hectares,combustible_burned_percent,terrain_load_seconds\n";

    output << std::setprecision(30);

    for (const auto& scenario : statistics.scenario_results) {
        output << "scenario,"
               << scenario.scenario_id << ','
               << scenario.scenario_seed << ','
               << scenario.steps_executed << ','
               << to_string(scenario.termination) << ','
               << scenario.extinguished_at_step << ','
               << scenario.burned_cells << ','
               << scenario.burned_percent << ','
               << scenario.cell_updates << ','
               << scenario.initialization_seconds << ','
               << scenario.simulation_seconds << ','
               << scenario.step_compute_seconds << ','
               << scenario.swap_seconds << ','
               << scenario.mean_step_seconds << ','
               << scenario.min_step_seconds << ','
               << scenario.max_step_seconds << ','
               << scenario.total_core_seconds << ','
               << scenario.throughput_cell_updates_per_second << ",,,,,"
               << scenario.valid_cells << ',' << scenario.nodata_cells << ','
               << scenario.non_combustible_cells << ',' << scenario.initially_combustible_cells << ',';
        if (scenario.burned_hectares >= 0) output << scenario.burned_hectares;
        output << ',' << scenario.combustible_burned_percent << ",\n";
    }

    output << "batch,,,,,,,"
           << statistics.mean_burned_percent << ','
           << statistics.total_cell_updates << ','
           << statistics.total_initialization_seconds << ','
           << statistics.total_simulation_seconds << ','
           << statistics.total_step_compute_seconds << ','
           << statistics.total_swap_seconds << ','
           << statistics.mean_step_seconds << ",,,"
           << statistics.total_core_seconds << ','
           << statistics.throughput_cell_updates_per_second << ','
           << statistics.mean_scenario_seconds << ','
           << statistics.completed_scenarios << ','
           << statistics.extinguished_scenarios << ','
           << statistics.max_steps_scenarios << ",,,,,,," << statistics.terrain_load_seconds << '\n';

    if (!output) {
        throw std::runtime_error("failed while writing summary CSV: " + path.string());
    }
}

} // namespace ember
