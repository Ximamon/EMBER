/**
 * @file ember_tests.cpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Core unit and regression tests for the EMBER wildfire simulation engine.
 * @version 0.5
 * @date 29/7/2026
 * 
 * 
 */

#include "ember/cli.hpp"
#include "ember/export.hpp"
#include "ember/fuel_model.hpp"
#include "ember/rothermel.hpp"
#include "ember/runner.hpp"
#include "ember/simulation.hpp"

#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        throw std::runtime_error(std::string(file) + ':' + std::to_string(line) +
                                 " check failed: " + expression);
    }
}

#define CHECK(expression) check((expression), #expression, __FILE__, __LINE__)

ember::SimulationConfig small_config(std::size_t width = 8, std::size_t height = 8) {
    ember::SimulationConfig config;
    config.width = width;
    config.height = height;
    config.max_steps = 12;
    config.scenarios = 1;
    config.seed = 42;
    config.non_combustible_fraction = 0.0F;
    config.ignitions = {{width / 2, height / 2}};
    return config;
}

void make_uniform_combustible(ember::WildfireSimulation& simulation) {
    auto view = simulation.grid().current_view();
    for (std::size_t index = 0; index < view.width * view.height; ++index) {
        view.state[index] = ember::CellState::Unburned;
        view.fuel[index] = 1.0F;
        view.moisture[index] = 0.0F;
        view.vegetation[index] = 1.25F;
        view.elevation[index] = 0.5F;
    }
}

void test_reproducibility() {
    auto config = small_config();
    ember::WildfireSimulation first(config, 3);
    ember::WildfireSimulation second(config, 3);
    const auto first_stats = first.run();
    const auto second_stats = second.run();
    CHECK(first_stats.scenario_seed == second_stats.scenario_seed);
    CHECK(first_stats.steps_executed == second_stats.steps_executed);
    CHECK(first_stats.burned_cells == second_stats.burned_cells);
    const auto first_grid = static_cast<const ember::GridBuffers&>(first.grid()).current_view();
    const auto second_grid = static_cast<const ember::GridBuffers&>(second.grid()).current_view();
    for (std::size_t index = 0; index < first.grid().cell_count(); ++index) {
        CHECK(first_grid.state[index] == second_grid.state[index]);
        CHECK(first_grid.fuel[index] == second_grid.fuel[index]);
        CHECK(first_grid.moisture[index] == second_grid.moisture[index]);
        CHECK(first_grid.vegetation[index] == second_grid.vegetation[index]);
        CHECK(first_grid.elevation[index] == second_grid.elevation[index]);
    }
}

void test_different_seeds() {
    auto first_config = small_config();
    auto second_config = first_config;
    second_config.seed = 43;
    ember::WildfireSimulation first(first_config, 0);
    ember::WildfireSimulation second(second_config, 0);
    first.initialize();
    second.initialize();
    const auto first_grid = static_cast<const ember::GridBuffers&>(first.grid()).current_view();
    const auto second_grid = static_cast<const ember::GridBuffers&>(second.grid()).current_view();
    bool differs = false;
    for (std::size_t index = 0; index < first.grid().cell_count(); ++index) {
        differs = differs || first_grid.fuel[index] != second_grid.fuel[index] ||
                  first_grid.moisture[index] != second_grid.moisture[index];
    }
    CHECK(differs);
}

#if EMBER_HAVE_OPENMP
void test_openmp_initialization_matches_cpu() {
    auto config = small_config(31, 19);
    config.ignitions = {{1, 1}, {28, 16}};
    for (std::uint64_t scenario_id : {0ULL, 1ULL, 7ULL}) {
        ember::WildfireSimulation cpu(config, scenario_id);
        cpu.initialize();
        config.synthetic_init_backend = ember::SyntheticInitBackend::OpenMp;
        ember::WildfireSimulation parallel(config, scenario_id);
        parallel.initialize();
        const auto a = static_cast<const ember::GridBuffers&>(cpu.grid()).current_view();
        const auto b = static_cast<const ember::GridBuffers&>(parallel.grid()).current_view();
        for (std::size_t i = 0; i < cpu.grid().cell_count(); ++i) {
            CHECK(a.state[i] == b.state[i]);
            CHECK(a.fuel[i] == b.fuel[i]);
            CHECK(a.moisture[i] == b.moisture[i]);
            CHECK(a.vegetation[i] == b.vegetation[i]);
            CHECK(a.elevation[i] == b.elevation[i]);
        }
        config.synthetic_init_backend = ember::SyntheticInitBackend::Cpu;
    }
}
#endif

void test_zero_spread() {
    auto config = small_config(5, 5);
    config.base_spread = 0.0F;
    const auto statistics = ember::run_scenario(config, 0);
    CHECK(statistics.burned_cells == 1);
    CHECK(statistics.termination == ember::TerminationReason::Extinguished);
}

void test_probability_factors() {
    auto config = small_config();
    config.base_spread = 0.5F;
    config.wind_strength = 0.8F;
    config.wind_direction_degrees = 0.0F;
    const float dry = ember::WildfireSimulation::neighbor_ignition_probability(
        config, 1.0F, 0.0F, 1.0F, 0.5F, 0.5F, 1, 0);
    const float wet = ember::WildfireSimulation::neighbor_ignition_probability(
        config, 1.0F, 1.0F, 1.0F, 0.5F, 0.5F, 1, 0);
    CHECK(dry > wet);

    const float aligned = dry;
    config.wind_direction_degrees = 180.0F;
    const float opposed = ember::WildfireSimulation::neighbor_ignition_probability(
        config, 1.0F, 0.0F, 1.0F, 0.5F, 0.5F, 1, 0);
    CHECK(aligned > opposed);

    config.wind_strength = 0.0F;
    const float uphill = ember::WildfireSimulation::neighbor_ignition_probability(
        config, 1.0F, 0.0F, 1.0F, 0.75F, 0.5F, 1, 0);
    const float downhill = ember::WildfireSimulation::neighbor_ignition_probability(
        config, 1.0F, 0.0F, 1.0F, 0.25F, 0.5F, 1, 0);
    CHECK(uphill > downhill);
}

void test_burning_transition() {
    auto config = small_config(1, 1);
    config.burn_rate = 0.2F;
    ember::WildfireSimulation simulation(config, 0);
    simulation.initialize();
    auto grid = simulation.grid().current_view();
    grid.state[0] = ember::CellState::Burning;
    grid.fuel[0] = 0.2F;
    CHECK(simulation.step(0) == 0);
    const auto result = static_cast<const ember::GridBuffers&>(simulation.grid()).current_view();
    CHECK(result.state[0] == ember::CellState::Burned);
    CHECK(result.fuel[0] == 0.0F);
}

void test_non_combustible_and_double_buffer() {
    auto config = small_config(4, 1);
    config.base_spread = 1.0F;
    config.wind_strength = 1.0F;
    config.wind_direction_degrees = 0.0F;
    config.burn_rate = 0.1F;
    config.ignitions = {{0, 0}};
    ember::WildfireSimulation simulation(config, 0);
    simulation.initialize();
    make_uniform_combustible(simulation);
    auto grid = simulation.grid().current_view();
    grid.state[0] = ember::CellState::Burning;
    CHECK(simulation.step(0) >= 1);
    auto result = static_cast<const ember::GridBuffers&>(simulation.grid()).current_view();
    CHECK(result.state[1] == ember::CellState::Burning);
    CHECK(result.state[2] == ember::CellState::Unburned);

    ember::WildfireSimulation blocked(config, 0);
    blocked.initialize();
    make_uniform_combustible(blocked);
    auto blocked_grid = blocked.grid().current_view();
    blocked_grid.state[0] = ember::CellState::Burning;
    blocked_grid.state[1] = ember::CellState::NonCombustible;
    blocked.step(0);
    const auto blocked_result = static_cast<const ember::GridBuffers&>(blocked.grid()).current_view();
    CHECK(blocked_result.state[1] == ember::CellState::NonCombustible);
    CHECK(blocked_result.state[2] == ember::CellState::Unburned);
}

void test_grid_edges() {
    for (const auto &dimensions : std::vector<std::pair<std::size_t, std::size_t>>{{1, 1}, {1, 7}, {7, 1}}) {
        auto config = small_config(dimensions.first, dimensions.second);
        const auto statistics = ember::run_scenario(config, 0);
        CHECK(statistics.cell_updates == static_cast<std::uint64_t>(dimensions.first * dimensions.second) *
                                             statistics.steps_executed);
    }
}

void test_batch_equivalence() {
    auto config = small_config();
    config.scenarios = 3;
    const auto individual = ember::run_scenario(config, 1);
    const auto batch = ember::run_batch(config);
    const auto& from_batch = batch.scenario_results.at(1);
    CHECK(individual.scenario_seed == from_batch.scenario_seed);
    CHECK(individual.steps_executed == from_batch.steps_executed);
#if !EMBER_ENABLE_CUDA
    CHECK(individual.burned_cells == from_batch.burned_cells);
#endif
    CHECK(individual.cell_updates == from_batch.cell_updates);
}

void test_validation_and_cli() {
    auto config = small_config();
    config.width = std::numeric_limits<std::size_t>::max();
    config.height = 2;
    bool threw = false;
    try {
        ember::validate_config(config);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    const char* arguments[] = {"ember", "--width", "9", "--height", "7", "--ignition", "2,3", "--seed", "99"};
    const auto options = ember::parse_cli(9, arguments);
    CHECK(options.config.width == 9);
    CHECK(options.config.height == 7);
    CHECK(options.config.seed == 99);
    CHECK(options.config.ignitions.size() == 1);
    CHECK(options.config.ignitions.front().x == 2);
    CHECK(options.config.ignitions.front().y == 3);
}

void test_exports() {
    const auto directory = std::filesystem::current_path() / "ember_test_artifacts";
    std::filesystem::remove_all(directory);
    auto config = small_config(3, 3);
    config.output_directory = directory.string();
    config.export_format = ember::ExportFormat::Both;
    const auto statistics = ember::run_batch(config);
    CHECK(statistics.completed_scenarios == 1);
    CHECK(std::filesystem::file_size(directory / "summary.csv") > 0);
    CHECK(std::filesystem::file_size(directory / "scenario_000000_final.csv") > 0);
    CHECK(std::filesystem::file_size(directory / "scenario_000000_final.ppm") > 0);
    std::filesystem::remove_all(directory);
}

void test_rothermel_physics() {
    CHECK(ember::fuel_class_for_code(102) == ember::FuelClass::Grass);
    CHECK(ember::fuel_class_for_code(145) == ember::FuelClass::Shrub);
    CHECK(ember::fuel_class_for_code(163) == ember::FuelClass::TimberUnderstory);
    CHECK(ember::fuel_class_for_code(183) == ember::FuelClass::TimberLitter);
    CHECK(ember::fuel_class_for_code(93) == ember::FuelClass::NonBurnable);

    const auto fuel = ember::fuel_model_params(ember::FuelClass::Shrub);
    const float dry = ember::rothermel_spread_rate_m_s(fuel, 0.02F, 0.0F, 0.0F);
    const float wet = ember::rothermel_spread_rate_m_s(fuel, 0.15F, 0.0F, 0.0F);
    CHECK(dry > 0.0F);
    CHECK(dry > wet);

    const float calm = ember::rothermel_spread_rate_m_s(fuel, 0.02F, 0.0F, 0.0F);
    const float windy = ember::rothermel_spread_rate_m_s(fuel, 0.02F, 2.0F, 0.0F);
    CHECK(windy > calm);

    const float flat = ember::rothermel_spread_rate_m_s(fuel, 0.02F, 0.0F, 0.0F);
    const float uphill = ember::rothermel_spread_rate_m_s(fuel, 0.02F, 0.0F, 0.3F);
    const float downhill = ember::rothermel_spread_rate_m_s(fuel, 0.02F, 0.0F, -0.3F);
    CHECK(uphill > flat);
    CHECK(flat > downhill);

    const auto inert = ember::fuel_model_params(ember::FuelClass::NonBurnable);
    CHECK(ember::rothermel_spread_rate_m_s(inert, 0.1F, 5.0F, 0.5F) == 0.0F);
}

#if !EMBER_ENABLE_CUDA
// The Rothermel path is CPU-only for now (see docs/rothermel-model.md); resolve_terrain_config
// rejects it under EMBER_ENABLE_CUDA the same way it already rejects real terrain.
void test_rothermel_gradual_ignition() {
    const auto fuel = ember::fuel_model_params(ember::FuelClass::Grass);
    const float rate = ember::rothermel_spread_rate_m_s(fuel, 0.02F, 0.0F, 0.0F);
    CHECK(rate > 0.0F);

    auto config = small_config(3, 1);
    config.spread_model = ember::SpreadModel::Rothermel;
    config.ignitions = {{0, 0}};
    config.min_moisture = 0.02F;
    config.max_moisture = 0.02F;
    config.min_elevation = 0.0F;
    config.max_elevation = 0.0F;
    config.synthetic_fuel_class = ember::FuelClass::Grass;
    config.rothermel_cell_size_m = 10.0F;
    // Choose the step duration so a single burning neighbor needs exactly four steps to
    // cross burn_fraction = 1, directly exercising the accumulate-and-carry-over behavior
    // the Rothermel path adds in place of the empirical model's single-step random draw.
    config.rothermel_time_step_s = config.rothermel_cell_size_m / rate / 4.0F;
    config.max_steps = 10;
    ember::WildfireSimulation simulation(config, 0);
    simulation.initialize();
    // Ignition fuel is otherwise a random draw from [min_fuel, max_fuel]; pin it well above
    // burn_rate * max_steps so the ignition cell cannot burn out mid-test and starve index 1.
    simulation.grid().current_view().fuel[0] = 100.0F;

    for (std::size_t step = 0; step < 3; ++step) {
        simulation.step(step);
        const auto result = static_cast<const ember::GridBuffers&>(simulation.grid()).current_view();
        CHECK(result.state[1] == ember::CellState::Unburned);
        const float expected = static_cast<float>(step + 1) * 0.25F;
        CHECK(std::abs(result.burn_fraction[1] - expected) < 0.01F);
    }
    simulation.step(3);
    const auto result = static_cast<const ember::GridBuffers&>(simulation.grid()).current_view();
    CHECK(result.state[1] == ember::CellState::Burning);
}
#endif

void test_rothermel_cli() {
    const char* arguments[] = {
        "ember", "--spread-model", "rothermel", "--wind-speed", "5",
        "--time-step", "30", "--min-spread-rate", "0.01",
        "--rothermel-cell-size", "25", "--synthetic-fuel-class", "timber-litter"};
    const auto options = ember::parse_cli(13, arguments);
    CHECK(options.config.spread_model == ember::SpreadModel::Rothermel);
    CHECK(options.config.wind_speed_m_s == 5.0F);
    CHECK(options.config.rothermel_time_step_s == 30.0F);
    CHECK(options.config.min_spread_rate_m_s == 0.01F);
    CHECK(options.config.rothermel_cell_size_m == 25.0F);
    CHECK(options.config.synthetic_fuel_class == ember::FuelClass::TimberLitter);
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"reproducibility", test_reproducibility},
        {"different seeds", test_different_seeds},
#if EMBER_HAVE_OPENMP
        {"OpenMP initialization matches CPU", test_openmp_initialization_matches_cpu},
#endif
        {"zero spread", test_zero_spread},
        {"probability factors", test_probability_factors},
        {"burning transition", test_burning_transition},
        {"non-combustible and double buffer", test_non_combustible_and_double_buffer},
        {"grid edges", test_grid_edges},
        {"batch equivalence", test_batch_equivalence},
        {"validation and CLI", test_validation_and_cli},
        {"exports", test_exports},
        {"Rothermel physics", test_rothermel_physics},
#if !EMBER_ENABLE_CUDA
        {"Rothermel gradual ignition", test_rothermel_gradual_ignition},
#endif
        {"Rothermel CLI", test_rothermel_cli},
    };

    std::size_t failures = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "[PASS] " << test.first << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test.first << ": " << error.what() << '\n';
        }
    }
    std::cout << (tests.size() - failures) << '/' << tests.size() << " tests passed\n";
    return failures == 0 ? 0 : 1;
}
