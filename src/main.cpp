#include "ember/cli.hpp"
#include "ember/runner.hpp"

#include <exception>
#include <iomanip>
#include <iostream>

int main(int argc, const char* argv[]) {
    try {
        const auto options = ember::parse_cli(argc, argv);
        if (options.show_help) {
            ember::print_help(std::cout);
            return 0;
        }

        const auto statistics = ember::run_batch(options.config);
        std::cout << "EMBER CPU baseline\n"
                  << "Grid: " << options.config.width << " x " << options.config.height << '\n'
                  << "Maximum steps: " << options.config.max_steps << '\n'
                  << "Scenarios: " << options.config.scenarios << '\n'
                  << "Seed: " << options.config.seed << '\n'
                  << std::fixed << std::setprecision(6)
                  << "Initialization time: " << statistics.total_initialization_seconds << " s\n"
                  << "Simulation time: " << statistics.total_simulation_seconds << " s\n"
                  << "Execution time: " << statistics.total_core_seconds << " s\n"
                  << "Mean scenario time: " << statistics.mean_scenario_seconds << " s\n"
                  << "Cell updates: " << statistics.total_cell_updates << '\n'
                  << std::setprecision(0)
                  << "Throughput: " << statistics.throughput_cell_updates_per_second
                  << " cell updates/s\n"
                  << std::setprecision(3)
                  << "Mean burned area: " << statistics.mean_burned_percent << " %\n"
                  << "Completed scenarios: " << statistics.completed_scenarios << '\n'
                  << "Extinguished scenarios: " << statistics.extinguished_scenarios << '\n'
                  << "Max-step scenarios: " << statistics.max_steps_scenarios << '\n';
        if (!options.config.output_directory.empty()) {
            std::cout << "Summary: " << options.config.output_directory << "/summary.csv\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "EMBER error: " << error.what() << "\nUse --help for usage.\n";
        return 2;
    }
}
