/**
 * @file cli.hpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Command-line interface for the Ember simulation.
 * @version 0.1
 * @date 29/7/2026
 * 
 * 
 */

#pragma once

#include "ember/config.hpp"

#include <iosfwd>

namespace ember {


/**
 * @struct CliOptions
 * @brief Structure to hold the command-line options for the Ember simulation.
 * This structure contains the simulation configuration parameters and a flag to indicate whether help information should be displayed.
 */
struct CliOptions {
    /// @brief Simulation configuration parameters parsed from the command line.
    SimulationConfig config;
    /// @brief Flag indicating whether help information should be displayed.
    bool show_help{};
};

/**
 * @brief Parses command-line arguments and returns the corresponding CliOptions structure.
 * 
 * @param argc The number of command-line arguments.
 * @param argv The array of command-line argument strings.
 * @throws std::invalid_argument If an unknown option is encountered or if a required value is missing.
 * @throws std::out_of_range If a value is out of the expected range for a given option.
 * @throws std::overflow_error If a value exceeds the maximum representable value for its type.
 * @throws std::runtime_error If an error occurs while parsing the command-line arguments.
 * @return CliOptions 
 */
CliOptions parse_cli(int argc, const char* const argv[]);

/**
 * @brief Prints help information for the Ember simulation to the specified output stream.
 * 
 * @param output The output stream to which the help information should be printed.
 */
void print_help(std::ostream& output);

} // namespace ember
