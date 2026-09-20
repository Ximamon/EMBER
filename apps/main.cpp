/**
 * @file main.cpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Main entry point for the EMBER fire simulation.
 * @version 0.1
 * @date 29/7/2026
 * 
 * 
 */

#include "ember/cli.hpp"
#include "ember/runner.hpp"
#include "ember/random.hpp"

#include <exception>
#include <iomanip>
#include <iostream>

int main(int argc, const char* argv[]) {

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--benchmark-rng") == 0) {
            std::size_t iters = 100'000'000;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                iters = std::stoull(argv[i + 1]);
            }
            ember::run_rng_benchmark(iters);
            return 0;
        }
    }

    // Parse arguments first; if the user requests help or provides invalid input, the program terminates early without allocating resources.
    try {
        const auto options = ember::parse_cli(argc, argv);
        if (options.show_help) {
            ember::print_help(std::cout);
            return 0;
        }

        // Execute the full simulation batch. This is a blocking operation that processes all scenarios sequentially.
        const auto statistics = ember::run_batch(options.config);
        std::cout << "EMBER\n"
                  << "Grid: " << options.config.width << " x " << options.config.height << '\n'
                  << "Maximum steps: " << options.config.max_steps << '\n'
                  << "Scenarios: " << options.config.scenarios << '\n'
                  << "Seed: " << options.config.seed << '\n'
                  << std::fixed << std::setprecision(6)
                  << "Initialization time: " << statistics.total_initialization_seconds << " s\n"
                  << "Simulation time: " << statistics.total_simulation_seconds << " s\n";

        // Step profiling
        if (statistics.total_simulation_seconds > 0.0) {
            const double compute_pct = 100.0 * statistics.total_step_compute_seconds / statistics.total_simulation_seconds;
            const double swap_pct = 100.0 * statistics.total_swap_seconds / statistics.total_simulation_seconds;
            std::cout << "  ├── Stencil compute: " << statistics.total_step_compute_seconds
                      << " s (" << std::setprecision(2) << compute_pct << " %)\n"
                      << "  └── Buffer swap:     " << statistics.total_swap_seconds
                      << " s (" << std::setprecision(2) << swap_pct << " %)\n"
                      << std::setprecision(6);
        }

        std::cout << "Execution time: " << statistics.total_core_seconds << " s\n"
                  << "Mean scenario time: " << statistics.mean_scenario_seconds << " s\n"
                  << std::setprecision(3)
                  << "Mean step time: " << (statistics.mean_step_seconds * 1000.0) << " ms\n"
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
            std::cout << "Summary: " << options.config.output_directory << "summary.csv\n";
        }
        return 0;
    } catch (const std::exception& error) {
        // Catch all standard exceptions to ensure graceful termination and provide meaningful error messages instead of a raw crash.
        std::cerr << "EMBER error: " << error.what() << "\nUse --help for usage.\n";
        return 2;
    }
}
