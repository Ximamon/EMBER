/**
 * @file runner.cpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Implementation of the simulation runner.
 * @version 0.1
 * @date 29/7/2026
 * 
 * 
 */

#include "ember/runner.hpp"

#include "ember/export.hpp"
#include "ember/simulation.hpp"

#include <filesystem>
#include <iomanip>
#include <sstream>

namespace ember {
namespace {

/**
 * @brief Generates a zero-padded filename stem for a given scenario.
 * 
 * Used for exporting CSV and PPM files with consistent naming (e.g., "scenario_000001_final").
 * 
 * @param scenario_id The unique ID of the scenario.
 * @return A string containing the formatted file stem.
 */
std::string scenario_stem(std::uint64_t scenario_id) {
    std::ostringstream stream;
    stream << "scenario_" << std::setw(6) << std::setfill('0') << scenario_id << "_final";
    return stream.str();
}

} // namespace

BatchStatistics run_batch(const SimulationConfig& config) {

    // Ensure configuration integrity before allocating any large grid buffers or creating directories.
    validate_config(config);
    BatchStatistics batch;
    batch.scenario_results.reserve(config.scenarios);
    const std::filesystem::path output_directory(config.output_directory);

    // Process each scenario sequentially. 
    // Each simulation instance generates its own random seed based on its scenario index.
    for (std::size_t scenario_index = 0; scenario_index < config.scenarios; ++scenario_index) {
        // Initialize a new WildfireSimulation instance for the current scenario and run it to completion.
        WildfireSimulation simulation(config, static_cast<std::uint64_t>(scenario_index));
        auto scenario_statistics = simulation.run();

        // If the export format is not None, export the final grid state to the specified format(s).
        if (config.export_format != ExportFormat::None) {
            const auto stem = scenario_stem(static_cast<std::uint64_t>(scenario_index));
            const auto grid = static_cast<const GridBuffers&>(simulation.grid()).current_view();

            if (config.export_format == ExportFormat::Csv || config.export_format == ExportFormat::Both) {
                export_grid_csv(output_directory / (stem + ".csv"), grid);
            }

            if (config.export_format == ExportFormat::Ppm || config.export_format == ExportFormat::Both) {
                export_grid_ppm(output_directory / (stem + ".ppm"), grid);
            }
        }
        
        batch.scenario_results.push_back(scenario_statistics);
    }

    finalize_batch_statistics(batch);
    if (!config.output_directory.empty()) {
        export_summary_csv(output_directory / "summary.csv", batch);
    }
    return batch;
}

} // namespace ember
