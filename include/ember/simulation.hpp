/**
 * @file simulation.hpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Simulation class for running the Ember wildfire simulation.
 * @version 0.1
 * @date 29/7/2026
 * 
 * 
 */

#pragma once

#include "ember/config.hpp"
#include "ember/grid.hpp"
#include "ember/statistics.hpp"

#include <cstddef>
#include <cstdint>

namespace ember {


/**
 * @class WildfireSimulation
 * @brief Represents a single wildfire simulation scenario.
 * 
 */
class WildfireSimulation {
public:

    /**
     * @brief Constructs a WildfireSimulation instance with the specified configuration and scenario ID.
     * @param config The simulation configuration parameters.
     * @param scenario_id The unique identifier for the simulation scenario.
     */
    WildfireSimulation(SimulationConfig config, std::uint64_t scenario_id);

    /// @brief Initializes the simulation, setting up the grid and preparing for execution.
    void initialize();
    /**
     * @brief Performs a single simulation step.
     * @param step_index The index of the current step.
     * @return The total number of cells currently burning.
     */
    std::size_t step(std::size_t step_index);

    /**
     * @brief Runs the simulation for the specified number of steps.
     * @return The statistics for the completed simulation.
     */
    ScenarioStatistics run();

    /**
     * @brief Gets a reference to the simulation grid.
     * @return A reference to the grid.
     */
    GridBuffers& grid() noexcept { return grid_; }
    /**
     * @brief Gets a constant reference to the simulation grid.
     * @return A constant reference to the grid.
     */
    const GridBuffers& grid() const noexcept { return grid_; }
    /**
     * @brief Gets the scenario seed for the simulation.
     * @return The scenario seed.
     */
    std::uint64_t seed() const noexcept { return scenario_seed_; }

    /**
     * @brief Calculates the probability of a neighbor cell igniting.
     * @param config The simulation configuration parameters.
     * @param target_fuel The fuel load of the target cell.
     * @param target_moisture The moisture content of the target cell.
     * @param target_vegetation The vegetation type of the target cell.
     * @param target_elevation The elevation of the target cell.
     * @param neighbor_elevation The elevation of the neighbor cell.
     * @param delta_x The difference in x coordinates.
     * @param delta_y The difference in y coordinates.
     * @return The ignition probability for the neighbor cell.
     */
    static float neighbor_ignition_probability(
        const SimulationConfig& config,
        float target_fuel,
        float target_moisture,
        float target_vegetation,
        float target_elevation,
        float neighbor_elevation,
        int delta_x,
        int delta_y
    );

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

/**
 * @brief Runs a single simulation scenario with the specified configuration and scenario ID.
 * @param config The simulation configuration parameters.
 * @param scenario_id The unique identifier for the simulation scenario.
 * @return The statistics for the completed simulation.
 */
ScenarioStatistics run_scenario(const SimulationConfig& config, std::uint64_t scenario_id);

} // namespace ember
