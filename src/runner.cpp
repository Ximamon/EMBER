#include "ember/runner.hpp"

#include "ember/export.hpp"
#include "ember/simulation.hpp"

#include <filesystem>
#include <iomanip>
#include <sstream>

namespace ember {
namespace {

std::string scenario_stem(std::uint64_t scenario_id) {
    std::ostringstream stream;
    stream << "scenario_" << std::setw(6) << std::setfill('0') << scenario_id << "_final";
    return stream.str();
}

} // namespace

BatchStatistics run_batch(const SimulationConfig& config) {
    validate_config(config);
    BatchStatistics batch;
    batch.scenario_results.reserve(config.scenarios);
    const std::filesystem::path output_directory(config.output_directory);

    for (std::size_t scenario_index = 0; scenario_index < config.scenarios; ++scenario_index) {
        WildfireSimulation simulation(config, static_cast<std::uint64_t>(scenario_index));
        auto scenario_statistics = simulation.run();

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
