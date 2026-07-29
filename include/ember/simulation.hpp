#pragma once

#include "ember/config.hpp"
#include "ember/grid.hpp"
#include "ember/statistics.hpp"

#include <cstddef>
#include <cstdint>

namespace ember {

class WildfireSimulation {
public:
    WildfireSimulation(SimulationConfig config, std::uint64_t scenario_id);

    void initialize();
    std::size_t step(std::size_t step_index);
    ScenarioStatistics run();

    GridBuffers& grid() noexcept { return grid_; }
    const GridBuffers& grid() const noexcept { return grid_; }
    std::uint64_t seed() const noexcept { return scenario_seed_; }

    static float neighbor_ignition_probability(
        const SimulationConfig& config,
        float target_fuel,
        float target_moisture,
        float target_vegetation,
        float target_elevation,
        float neighbor_elevation,
        int delta_x,
        int delta_y);

private:
    SimulationConfig config_;
    std::uint64_t scenario_id_{};
    std::uint64_t scenario_seed_{};
    GridBuffers grid_;
    float wind_x_{};
    float wind_y_{};
    bool initialized_{};

    float neighbor_probability(
        const ConstGridView& current,
        std::size_t target_index,
        std::size_t neighbor_index,
        int delta_x,
        int delta_y) const;
};

ScenarioStatistics run_scenario(const SimulationConfig& config, std::uint64_t scenario_id);

} // namespace ember
