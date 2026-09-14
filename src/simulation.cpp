/**
 * @file simulation.cpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Implementation of the wildfire simulation.
 * @version 0.1
 * @date 29/7/2026
 * 
 * 
 */

#include "ember/simulation.hpp"

#include "ember/random.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace ember {
namespace {

constexpr float pi = 3.14159265358979323846F;
constexpr float inverse_sqrt_two = 0.70710678118654752440F;

/**
 * @brief Clamps a floating-point value to the range [0.0, 1.0].
 * @param value The value to clamp.
 * @return The clamped value.
 */
float clamp01(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

/**
 * @brief Safely casts a size_t to a uint64_t.
 * @param value The value to cast.
 * @return The 64-bit unsigned integer representation.
 */
std::uint64_t as_u64(std::size_t value) {
    return static_cast<std::uint64_t>(value);
}

} // namespace

WildfireSimulation::WildfireSimulation(SimulationConfig config, std::uint64_t scenario_id)
    : config_(std::move(config)),
      scenario_id_(scenario_id),
      scenario_seed_(ember::scenario_seed(config_.seed, scenario_id)) {
    validate_config(config_);
    const float radians = config_.wind_direction_degrees * pi / 180.0F;
    wind_x_ = std::cos(radians);
    wind_y_ = std::sin(radians);
}

void WildfireSimulation::initialize() {
    grid_ = GridBuffers(config_.width, config_.height);
    auto current = grid_.current_view();
    auto next = grid_.next_view();
    const auto count = grid_.cell_count();

    for (std::size_t index = 0; index < count; ++index) {
        // We use keyed hashing to ensure that fuel, moisture, and vegetation 
        // get independent pseudo-random streams, even though they share the same seed and index.
        const auto key = as_u64(index);
        const float fuel = uniform_range(
            keyed_hash(scenario_seed_, random_tag::fuel, key), config_.min_fuel, config_.max_fuel);
        current.fuel[index] = fuel;
        next.fuel[index] = fuel;
        current.moisture[index] = uniform_range(
            keyed_hash(scenario_seed_, random_tag::moisture, key), config_.min_moisture, config_.max_moisture);
        current.vegetation[index] = uniform_range(
            keyed_hash(scenario_seed_, random_tag::vegetation, key), config_.min_vegetation, config_.max_vegetation);
        current.elevation[index] = uniform_range(
            keyed_hash(scenario_seed_, random_tag::elevation, key), config_.min_elevation, config_.max_elevation);
        const bool non_combustible = uniform01(
            keyed_hash(scenario_seed_, random_tag::non_combustible, key)) < config_.non_combustible_fraction;
        current.state[index] = non_combustible ? CellState::NonCombustible : CellState::Unburned;
        next.state[index] = current.state[index];
    }

    std::vector<IgnitionPoint> default_ignition;
    const std::vector<IgnitionPoint>* ignitions = &config_.ignitions;
    if (ignitions->empty()) {
        default_ignition.push_back({config_.width / 2, config_.height / 2});
        ignitions = &default_ignition;
    }
    for (const auto& point : *ignitions) {
        const auto index = point.y * config_.width + point.x;
        current.state[index] = CellState::Burning;
        next.state[index] = CellState::Burning;
        const float ignition_fuel = std::max(current.fuel[index], config_.burn_rate);
        current.fuel[index] = ignition_fuel;
        next.fuel[index] = ignition_fuel;
    }
    initialized_ = true;
}

float WildfireSimulation::neighbor_ignition_probability(
    const SimulationConfig& config,
    float target_fuel,
    float target_moisture,
    float target_vegetation,
    float target_elevation,
    float neighbor_elevation,
    int delta_x,
    int delta_y) {
    // Diagonal neighbors are further away (sqrt(2) distance), so their influence is reduced.
    const bool diagonal = delta_x != 0 && delta_y != 0;
    const float distance_factor = diagonal ? inverse_sqrt_two : 1.0F;
    
    const float direction_x = static_cast<float>(delta_x) * distance_factor;
    const float direction_y = static_cast<float>(-delta_y) * distance_factor;
    const float radians = config.wind_direction_degrees * pi / 180.0F;
    
    // Dot product between wind direction and fire propagation direction.
    // Positive alignment means wind blows towards the target; negative means against it.
    const float alignment = direction_x * std::cos(radians) + direction_y * std::sin(radians);
    const float wind_factor = std::clamp(1.0F + config.wind_strength * alignment, 0.25F, 2.0F);
    
    // Fire travels faster uphill (positive slope) and slower downhill (negative slope).
    const float slope = std::clamp((target_elevation - neighbor_elevation) / config.slope_scale, -1.0F, 1.0F);
    const float slope_factor = std::clamp(1.0F + 0.5F * slope, 0.5F, 1.5F);
    
    const float moisture_factor = 1.0F - 0.8F * clamp01(target_moisture);
    
    // The final probability is the product of all environmental modifiers.
    return clamp01(config.base_spread * clamp01(target_fuel) * target_vegetation *
                   moisture_factor * wind_factor * slope_factor * distance_factor);
}

float WildfireSimulation::neighbor_probability(
    const ConstGridView& current,
    std::size_t target_index,
    std::size_t neighbor_index,
    int delta_x,
    int delta_y) const {
    // Diagonal neighbors are further away (sqrt(2) distance), so their influence is reduced.
    const bool diagonal = delta_x != 0 && delta_y != 0;
    const float distance_factor = diagonal ? inverse_sqrt_two : 1.0F;
    const float direction_x = static_cast<float>(delta_x) * distance_factor;
    const float direction_y = static_cast<float>(-delta_y) * distance_factor;
    
    // Dot product using precalculated wind vectors (wind_x_, wind_y_) 
    // to avoid expensive trigonometric functions (cos, sin) in the hot loop.
    const float alignment = direction_x * wind_x_ + direction_y * wind_y_;
    const float wind_factor = std::clamp(1.0F + config_.wind_strength * alignment, 0.25F, 2.0F);
    const float slope = std::clamp(
        (current.elevation[target_index] - current.elevation[neighbor_index]) / config_.slope_scale,
        -1.0F, 1.0F);
    const float slope_factor = std::clamp(1.0F + 0.5F * slope, 0.5F, 1.5F);
    const float moisture_factor = 1.0F - 0.8F * clamp01(current.moisture[target_index]);
    return clamp01(config_.base_spread * clamp01(current.fuel[target_index]) *
                   current.vegetation[target_index] * moisture_factor * wind_factor *
                   slope_factor * distance_factor);
}

std::size_t WildfireSimulation::step(std::size_t step_index) {
    if (!initialized_) {
        throw std::logic_error("simulation must be initialized before stepping");
    }
    const ConstGridView current = static_cast<const GridBuffers&>(grid_).current_view();
    auto next = grid_.next_view();
    std::size_t burning_next = 0;

    for (std::size_t row = 0; row < config_.height; ++row) {
        for (std::size_t column = 0; column < config_.width; ++column) {
            const auto index = row * config_.width + column;
            const auto state = current.state[index];
            next.fuel[index] = current.fuel[index];

            if (state == CellState::NonCombustible || state == CellState::Burned) {
                next.state[index] = state;
                continue;
            }
            if (state == CellState::Burning) {
                next.fuel[index] = std::max(0.0F, current.fuel[index] - config_.burn_rate);
                next.state[index] = next.fuel[index] <= 0.0F ? CellState::Burned : CellState::Burning;
                if (next.state[index] == CellState::Burning) {
                    ++burning_next;
                }
                continue;
            }

            // Calculate the probability of the target cell IGNITING from any of its burning neighbors.
            // We use the independent probability rule: P(ignites) = 1 - P(does NOT ignite from ANY neighbor).
            // P(does NOT ignite from ANY) = Product of (1 - P(ignite from neighbor i)).
            double probability_not_ignited = 1.0;
            for (int row_offset = -1; row_offset <= 1; ++row_offset) {
                for (int column_offset = -1; column_offset <= 1; ++column_offset) {
                    if (row_offset == 0 && column_offset == 0) {
                        continue;
                    }
                    const auto neighbor_row_signed = static_cast<std::ptrdiff_t>(row) + row_offset;
                    const auto neighbor_column_signed = static_cast<std::ptrdiff_t>(column) + column_offset;
                    if (neighbor_row_signed < 0 || neighbor_column_signed < 0 ||
                        neighbor_row_signed >= static_cast<std::ptrdiff_t>(config_.height) ||
                        neighbor_column_signed >= static_cast<std::ptrdiff_t>(config_.width)) {
                        continue;
                    }
                    const auto neighbor_row = static_cast<std::size_t>(neighbor_row_signed);
                    const auto neighbor_column = static_cast<std::size_t>(neighbor_column_signed);
                    const auto neighbor_index = neighbor_row * config_.width + neighbor_column;
                    if (current.state[neighbor_index] == CellState::Burning) {
                        const float probability = neighbor_probability(
                            current, index, neighbor_index, -column_offset, -row_offset);
                        probability_not_ignited *= 1.0 - static_cast<double>(probability);
                    }
                }
            }
            const double ignition_probability = 1.0 - probability_not_ignited;
            const double draw = uniform01(keyed_hash(
                scenario_seed_, random_tag::spread, as_u64(step_index), as_u64(index)));
            next.state[index] = draw < ignition_probability ? CellState::Burning : CellState::Unburned;
            if (next.state[index] == CellState::Burning) {
                ++burning_next;
            }
        }
    }
    grid_.swap_buffers();
    return burning_next;
}

ScenarioStatistics WildfireSimulation::run() {
    using clock = std::chrono::steady_clock;
    const auto initialization_start = clock::now();
    initialize();
    const auto initialization_end = clock::now();

    ScenarioStatistics statistics;
    statistics.scenario_id = scenario_id_;
    statistics.scenario_seed = scenario_seed_;

    std::size_t burning_cells = 1;
    const auto simulation_start = clock::now();
    
    // Main simulation loop: process steps until the fire extinguishes naturally or we hit the maximum allowed steps.
    for (std::size_t step_index = 0; step_index < config_.max_steps; ++step_index) {
        burning_cells = step(step_index);
        statistics.steps_executed = step_index + 1;
        if (burning_cells == 0) {
            statistics.termination = TerminationReason::Extinguished;
            statistics.extinguished_at_step = static_cast<std::int64_t>(statistics.steps_executed);
            break;
        }
    }
    const auto simulation_end = clock::now();
    if (burning_cells != 0) {
        statistics.termination = TerminationReason::MaxSteps;
    }

    // Post-simulation analysis: sweep the final grid state to tally up the damage and remaining cells.
    const auto count = grid_.cell_count();
    const auto view = static_cast<const GridBuffers&>(grid_).current_view();
    for (std::size_t index = 0; index < count; ++index) {
        if (view.state[index] == CellState::Burning || view.state[index] == CellState::Burned) {
            ++statistics.burned_cells;
        }
        if (view.state[index] == CellState::Burning) {
            ++statistics.burning_cells;
        }
        if (view.state[index] == CellState::NonCombustible) {
            ++statistics.non_combustible_cells;
        }
    }

    // Calculate performance metrics (throughput and core time) for benchmarking.
    statistics.cell_updates = static_cast<std::uint64_t>(count) *
                              static_cast<std::uint64_t>(statistics.steps_executed);
    statistics.burned_percent = 100.0 * static_cast<double>(statistics.burned_cells) /
                                static_cast<double>(count);
    statistics.initialization_seconds =
        std::chrono::duration<double>(initialization_end - initialization_start).count();
    statistics.simulation_seconds =
        std::chrono::duration<double>(simulation_end - simulation_start).count();
    statistics.total_core_seconds = statistics.initialization_seconds + statistics.simulation_seconds;
    statistics.throughput_cell_updates_per_second =
        statistics.simulation_seconds > 0.0
            ? static_cast<double>(statistics.cell_updates) / statistics.simulation_seconds
            : 0.0;
    return statistics;
}

ScenarioStatistics run_scenario(const SimulationConfig& config, std::uint64_t scenario_id) {
    WildfireSimulation simulation(config, scenario_id);
    return simulation.run();
}

} // namespace ember
