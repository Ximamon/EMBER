/**
 * @file cli.cpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Implementation of the command-line interface.
 * @version 0.5
 * @date 29/7/2026
 * 
 * 
 */

#include "ember/cli.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace ember {
namespace {

/**
 * @brief Ensures that an option has a corresponding value and returns it.
 * @param index The current argument index, which will be incremented.
 * @param argc The total number of arguments.
 * @param argv The argument array.
 * @param option The name of the option being parsed (for error reporting).
 * @return The value associated with the option.
 * @throws std::invalid_argument If the value is missing.
 */
std::string require_value(int& index, int argc, const char* const argv[], const std::string& option) {
    if (index + 1 >= argc) {
        throw std::invalid_argument("missing value for " + option);
    }
    ++index;
    return argv[index];
}

/**
 * @brief Parses a 64-bit unsigned integer from a string.
 * @param text The string to parse.
 * @param option The name of the option being parsed (for error reporting).
 * @return The parsed unsigned integer.
 * @throws std::invalid_argument If the string is empty, negative, or invalid.
 */
std::uint64_t parse_u64(const std::string& text, const std::string& option) {
    // std::stoull silently wraps negative numbers due to two's complement conversion, 
    // so we must explicitly check for a leading minus sign to reject invalid inputs.
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

/**
 * @brief Parses a size_t value from a string.
 * @param text The string to parse.
 * @param option The name of the option being parsed.
 * @return The parsed size_t value.
 */
std::size_t parse_size(const std::string& text, const std::string& option) {
    const auto value = parse_u64(text, option);
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument("value is too large for " + option);
    }
    return static_cast<std::size_t>(value);
}

/**
 * @brief Parses a floating-point value from a string.
 * @param text The string to parse.
 * @param option The name of the option being parsed.
 * @return The parsed float value.
 */
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

/**
 * @brief Parses an ignition point from a comma-separated string (e.g., "128,256").
 * @param text The string to parse.
 * @return The parsed IgnitionPoint struct.
 */
IgnitionPoint parse_ignition(const std::string& text) {
    const auto comma = text.find(',');
    if (comma == std::string::npos || text.find(',', comma + 1) != std::string::npos) {
        throw std::invalid_argument("ignition must use X,Y format");
    }
    return {parse_size(text.substr(0, comma), "--ignition"),
            parse_size(text.substr(comma + 1), "--ignition")};
}

/**
 * @brief Parses the export format from a string.
 * @param text The string to parse (e.g., "csv", "ppm").
 * @return The parsed ExportFormat enum.
 */
ExportFormat parse_export_format(const std::string& text) {
    if (text == "none") return ExportFormat::None;
    if (text == "csv") return ExportFormat::Csv;
    if (text == "ppm") return ExportFormat::Ppm;
    if (text == "both") return ExportFormat::Both;
    throw std::invalid_argument("--export must be one of: none, csv, ppm, both");
}

/**
 * @brief Parses the synthetic terrain initialization backend from a string.
 * @param text Backend name ("cpu", "openmp", or "gpu").
 * @return The corresponding SyntheticInitBackend enum value.
 * @throws std::invalid_argument If the text does not match a recognized backend.
 */
SyntheticInitBackend parse_init_backend(const std::string& text) {
    if (text == "cpu") return SyntheticInitBackend::Cpu;
    if (text == "openmp") return SyntheticInitBackend::OpenMp;
    if (text == "gpu") return SyntheticInitBackend::Cuda;
    throw std::invalid_argument("--cuda-init must be one of: cpu, openmp, gpu");
}

/**
 * @brief Parses the spread model from a string.
 * @param text The string to parse (e.g., "empirical", "rothermel").
 * @return The parsed SpreadModel enum.
 */
SpreadModel parse_spread_model(const std::string& text) {
    if (text == "empirical") return SpreadModel::Empirical;
    if (text == "rothermel") return SpreadModel::Rothermel;
    throw std::invalid_argument("--spread-model must be one of: empirical, rothermel");
}

/**
 * @brief Parses a Rothermel fuel family from a string.
 * @param text The string to parse (e.g., "grass", "shrub").
 * @return The parsed FuelClass enum.
 */
FuelClass parse_fuel_class(const std::string& text) {
    if (text == "grass") return FuelClass::Grass;
    if (text == "shrub") return FuelClass::Shrub;
    if (text == "timber-understory") return FuelClass::TimberUnderstory;
    if (text == "timber-litter") return FuelClass::TimberLitter;
    throw std::invalid_argument(
        "--synthetic-fuel-class must be one of: grass, shrub, timber-understory, timber-litter");
}

} // namespace

/**
 * @brief Parses command-line arguments and populates CliOptions.
 */
CliOptions parse_cli(int argc, const char* const argv[]) {
    // We implement a custom, lightweight CLI parser to keep EMBER dependency-free 
    // (avoiding heavy libraries like Boost.Program_options or CLI11).
    CliOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string option(argv[index]);
        if (option == "--help" || option == "-h") {
            options.show_help = true;
        } else if (option == "--width") {
            options.config.width_explicit = true;
            options.config.width = parse_size(require_value(index, argc, argv, option), option);
        } else if (option == "--height") {
            options.config.height_explicit = true;
            options.config.height = parse_size(require_value(index, argc, argv, option), option);
        } else if (option == "--terrain") {
            options.config.terrain_path = require_value(index, argc, argv, option);
            if (options.config.terrain_path.empty()) throw std::invalid_argument("empty terrain path");
        } else if (option == "--elevation") {
            options.config.elevation_path = require_value(index, argc, argv, option);
            if (options.config.elevation_path.empty()) throw std::invalid_argument("empty elevation path");
        } else if (option == "--terrain-fuel") {
            options.config.terrain_fuel = parse_float(require_value(index, argc, argv, option), option);
        } else if (option == "--terrain-moisture") {
            options.config.terrain_moisture = parse_float(require_value(index, argc, argv, option), option);
        } else if (option == "--steps") {
            options.config.max_steps = parse_size(require_value(index, argc, argv, option), option);
        } else if (option == "--scenarios") {
            options.config.scenarios = parse_size(require_value(index, argc, argv, option), option);
        } else if (option == "--seed") {
            options.config.seed = parse_u64(require_value(index, argc, argv, option), option);
        } else if (option == "--cuda-init") {
            options.config.synthetic_init_backend = parse_init_backend(require_value(index, argc, argv, option));
        } else if (option == "--verify-cuda-init") {
            options.config.verify_cuda_initialization = true;
        } else if (option == "--wind-direction") {
            options.config.wind_direction_degrees = parse_float(require_value(index, argc, argv, option), option);
        } else if (option == "--wind-strength") {
            options.config.wind_strength = parse_float(require_value(index, argc, argv, option), option);
        } else if (option == "--base-spread") {
            options.config.base_spread = parse_float(require_value(index, argc, argv, option), option);
        } else if (option == "--burn-rate") {
            options.config.burn_rate = parse_float(require_value(index, argc, argv, option), option);
        } else if (option == "--spread-model") {
            options.config.spread_model = parse_spread_model(require_value(index, argc, argv, option));
        } else if (option == "--wind-speed") {
            options.config.wind_speed_m_s = parse_float(require_value(index, argc, argv, option), option);
        } else if (option == "--time-step") {
            options.config.rothermel_time_step_s = parse_float(require_value(index, argc, argv, option), option);
        } else if (option == "--min-spread-rate") {
            options.config.min_spread_rate_m_s = parse_float(require_value(index, argc, argv, option), option);
        } else if (option == "--rothermel-cell-size") {
            options.config.rothermel_cell_size_m = parse_float(require_value(index, argc, argv, option), option);
        } else if (option == "--synthetic-fuel-class") {
            options.config.synthetic_fuel_class = parse_fuel_class(require_value(index, argc, argv, option));
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

/**
 * @brief Prints formatted CLI usage and available options to the output stream.
 */
void print_help(std::ostream& output) {
    output <<
        "EMBER - stochastic wildfire simulator\n\n"
        "Usage: ember [options]\n\n"
        "Options:\n"
        "  --width N                 Grid width (default: 512)\n"
        "  --height N                Grid height (default: 512)\n"
        "  --terrain FILE            ZAFM ASCII grid with .prj (WGS84 UTM 29N/30N/31N); CPU only\n"
        "  --elevation FILE          Aligned metric ASCII heights with .prj; scalar CPU only\n"
        "  --terrain-fuel X          Uniform fuel in (0,1] (default: 1)\n"
        "  --terrain-moisture X      Uniform moisture in [0,1] (default: 0.2)\n"
        "  --steps N                 Maximum steps (default: 500)\n"
        "  --scenarios N             Independent scenarios (default: 1)\n"
        "  --seed N                  Global seed (default: 42)\n"
        "  --cuda-init MODE          cpu, openmp, or gpu (CUDA default: gpu; CPU default: cpu)\n"
        "  --verify-cuda-init        Compare GPU-generated input with CPU before running\n"
        "  --wind-direction DEG      Direction wind blows toward; 0=east, 90=north\n"
        "  --wind-strength X         Wind strength in [0,1] (default: 0.4)\n"
        "  --base-spread X           Base spread probability in [0,1] (default: 0.25)\n"
        "  --burn-rate X             Fuel consumed per step in (0,1] (default: 0.2)\n"
        "  --spread-model MODEL      empirical or rothermel (default: empirical)\n"
        "  --wind-speed X            Real wind speed in m/s, Rothermel only (default: 0)\n"
        "  --time-step X             Simulated seconds per step, Rothermel only (default: 60)\n"
        "  --min-spread-rate X       Extinction threshold in m/s, Rothermel only (default: 0.0017)\n"
        "  --rothermel-cell-size X   Cell size in meters on synthetic grids, Rothermel only (default: 10)\n"
        "  --synthetic-fuel-class C  grass, shrub, timber-understory, or timber-litter (default: shrub)\n"
        "  --ignition X,Y            Ignition point; may be repeated (default: center)\n"
        "  --output DIRECTORY        Write summary.csv in this directory\n"
        "  --export FORMAT           none, csv, ppm, or both (default: none)\n"
        "  --benchmark-rng N         Benchmark RNG performance with N iterations (default: 100.000.000)\n"
        "  -h, --help                Show this help\n";
}

} // namespace ember
