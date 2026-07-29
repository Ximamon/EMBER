#include "ember/cli.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace ember {
namespace {

std::string require_value(int& index, int argc, const char* const argv[], const std::string& option) {
    if (index + 1 >= argc) {
        throw std::invalid_argument("missing value for " + option);
    }
    ++index;
    return argv[index];
}

std::uint64_t parse_u64(const std::string& text, const std::string& option) {
    if (text.empty() || text.front() == '-') {
        throw std::invalid_argument("invalid non-negative integer for " + option + ": " + text);
    }
    std::size_t consumed = 0;
    try {
        const auto value = std::stoull(text, &consumed, 10);
        if (consumed != text.size()) {
            throw std::invalid_argument("");
        }
        return value;
    } catch (const std::exception&) {
        throw std::invalid_argument("invalid non-negative integer for " + option + ": " + text);
    }
}

std::size_t parse_size(const std::string& text, const std::string& option) {
    const auto value = parse_u64(text, option);
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument("value is too large for " + option);
    }
    return static_cast<std::size_t>(value);
}

float parse_float(const std::string& text, const std::string& option) {
    std::size_t consumed = 0;
    try {
        const float value = std::stof(text, &consumed);
        if (consumed != text.size()) {
            throw std::invalid_argument("");
        }
        return value;
    } catch (const std::exception&) {
        throw std::invalid_argument("invalid number for " + option + ": " + text);
    }
}

IgnitionPoint parse_ignition(const std::string& text) {
    const auto comma = text.find(',');
    if (comma == std::string::npos || text.find(',', comma + 1) != std::string::npos) {
        throw std::invalid_argument("ignition must use X,Y format");
    }
    return {parse_size(text.substr(0, comma), "--ignition"),
            parse_size(text.substr(comma + 1), "--ignition")};
}

ExportFormat parse_export_format(const std::string& text) {
    if (text == "none") return ExportFormat::None;
    if (text == "csv") return ExportFormat::Csv;
    if (text == "ppm") return ExportFormat::Ppm;
    if (text == "both") return ExportFormat::Both;
    throw std::invalid_argument("--export must be one of: none, csv, ppm, both");
}

} // namespace

CliOptions parse_cli(int argc, const char* const argv[]) {
    CliOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string option(argv[index]);
        if (option == "--help" || option == "-h") {
            options.show_help = true;
        } else if (option == "--width") {
            options.config.width = parse_size(require_value(index, argc, argv, option), option);
        } else if (option == "--height") {
            options.config.height = parse_size(require_value(index, argc, argv, option), option);
        } else if (option == "--steps") {
            options.config.max_steps = parse_size(require_value(index, argc, argv, option), option);
        } else if (option == "--scenarios") {
            options.config.scenarios = parse_size(require_value(index, argc, argv, option), option);
        } else if (option == "--seed") {
            options.config.seed = parse_u64(require_value(index, argc, argv, option), option);
        } else if (option == "--wind-direction") {
            options.config.wind_direction_degrees = parse_float(require_value(index, argc, argv, option), option);
        } else if (option == "--wind-strength") {
            options.config.wind_strength = parse_float(require_value(index, argc, argv, option), option);
        } else if (option == "--base-spread") {
            options.config.base_spread = parse_float(require_value(index, argc, argv, option), option);
        } else if (option == "--burn-rate") {
            options.config.burn_rate = parse_float(require_value(index, argc, argv, option), option);
        } else if (option == "--ignition") {
            options.config.ignitions.push_back(parse_ignition(require_value(index, argc, argv, option)));
        } else if (option == "--output") {
            options.config.output_directory = require_value(index, argc, argv, option);
        } else if (option == "--export") {
            options.config.export_format = parse_export_format(require_value(index, argc, argv, option));
        } else {
            throw std::invalid_argument("unknown option: " + option);
        }
    }
    if (!options.show_help) {
        validate_config(options.config);
    }
    return options;
}

void print_help(std::ostream& output) {
    output <<
        "EMBER CPU baseline - stochastic wildfire simulator\n\n"
        "Usage: ember [options]\n\n"
        "Options:\n"
        "  --width N                 Grid width (default: 512)\n"
        "  --height N                Grid height (default: 512)\n"
        "  --steps N                 Maximum steps (default: 500)\n"
        "  --scenarios N             Independent scenarios (default: 1)\n"
        "  --seed N                  Global seed (default: 42)\n"
        "  --wind-direction DEG      Direction wind blows toward; 0=east, 90=north\n"
        "  --wind-strength X         Wind strength in [0,1] (default: 0.4)\n"
        "  --base-spread X           Base spread probability in [0,1] (default: 0.25)\n"
        "  --burn-rate X             Fuel consumed per step in (0,1] (default: 0.2)\n"
        "  --ignition X,Y            Ignition point; may be repeated (default: center)\n"
        "  --output DIRECTORY        Write summary.csv in this directory\n"
        "  --export FORMAT           none, csv, ppm, or both (default: none)\n"
        "  -h, --help                Show this help\n";
}

} // namespace ember
