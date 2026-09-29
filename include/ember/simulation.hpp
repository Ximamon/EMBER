/**
 * @file simulation.hpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Simulation class for running the Ember wildfire simulation.
 * @version 0.5
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

    /// @brief Allocates host output storage without field generation when input is generated directly on the GPU.
    void initialize_empty();
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
    /// @brief Simulation configuration parameters.
    SimulationConfig config_;
    /// @brief Unique scenario index within the batch.
    std::uint64_t scenario_id_{};
    /// @brief Seed derived for this specific scenario.
    std::uint64_t scenario_seed_{};
    /// @brief Double-buffered grid holding simulation layers.
    GridBuffers grid_;
    /// @brief Precalculated X-component of wind vector.
    float wind_x_{};
    /// @brief Precalculated Y-component of wind vector.
    float wind_y_{};
    /// @brief Flag indicating if terrain and ignitions have been initialized.
    bool initialized_{};

    /// @brief Accumulated time spent computing cellular stencil updates in seconds.
    double step_compute_seconds_{0.0};
    /// @brief Accumulated time spent swapping ping-pong buffers in seconds.
    double swap_seconds_{0.0};
    /// @brief Minimum observed step duration in seconds.
    double min_step_seconds_{0.0};
    /// @brief Maximum observed step duration in seconds.
    double max_step_seconds_{0.0};

    /// @brief Initializes grid layers from parsed real terrain raster data.
    void initialize_terrain();
    /// @brief Generates procedural synthetic terrain layers using keyed hashing.
    void initialize_synthetic_terrain();
    /// @brief Applies configured ignition points to ignite the fire.
    void apply_ignitions();

    /// @brief Executes a single simulation step using standard scalar CPU logic.
    /// @param step_index Current timestep index.
    /// @return Number of cells currently burning.
    std::size_t step_scalar(std::size_t step_index);

    /// @brief Evaluates state transitions and fire spread for an individual grid cell.
    /// @param current Read-only view of current grid state.
    /// @param next Mutable view of next grid state.
    /// @param step_index Current timestep index.
    /// @param row Cell row index.
    /// @param column Cell column index.
    /// @return 1 if cell remains or becomes burning, 0 otherwise.
    std::size_t step_cell(
        const ConstGridView& current,
        GridView next,
        std::size_t step_index,
        std::size_t row,
        std::size_t column) const;

    /// @brief Executes a single simulation step using AVX2 SIMD vector instructions.
    /// @param step_index Current timestep index.
    /// @return Number of cells currently burning.
    std::size_t step_avx2(std::size_t step_index);

    /// @brief Evaluates fire spread probability from a burning neighbor to a target cell.
    /// @param current Read-only view of the grid.
    /// @param target_index Linear index of the candidate unburned cell.
    /// @param neighbor_index Linear index of the burning neighbor cell.
    /// @param delta_x Relative X offset to neighbor (-1, 0, or 1).
    /// @param delta_y Relative Y offset to neighbor (-1, 0, or 1).
    /// @return Calculated ignition probability in [0, 1].
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
