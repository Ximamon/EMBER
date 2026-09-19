/**
 * @file grid.hpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Grid structure for the Ember simulation.
 * @version 0.1
 * @date 29/7/2026
 * 
 * 
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ember {

/**
 * @enum CellState
 * @brief Enumerates the possible states of a cell in the simulation grid.
 * The available states include:
 * - Unburned: The cell has not been ignited and is available to burn.
 * - Burning: The cell is currently on fire.
 * - Burned: The cell has already burned and cannot burn again.
 * - NonCombustible: The cell cannot burn due to the presence of non-combustible material.
 * 
 * The enum is defined as a uint8_t to minimize memory usage, as the number of states is small and fits within 8 bits.
 */
enum class CellState : std::uint8_t {
    Unburned,
    Burning,
    Burned,
    NonCombustible
};

/**
 * @class GridView
 * @brief Represents a view of the simulation grid, providing access to cell states and properties.
 * This structure holds the width and height of the grid, along with pointers to arrays representing the state, 
 * fuel, moisture, vegetation, and elevation of each cell. It allows for efficient access and manipulation of the
 * grid data without copying the underlying data.
 * 
 */
struct GridView {
    std::size_t width{};
    std::size_t height{};

    /// @brief Pointer to the array of cell states in the grid. Each cell state is represented by a value from the CellState enum.
    CellState* state{};
    /// @brief Pointer to the array of fuel values for each cell in the grid. Each value represents the amount of combustible material in the cell.
    float* fuel{};
    /// @brief Pointer to the array of moisture values for each cell in the grid. Each value represents the moisture content of the cell.
    float* moisture{};
    /// @brief Pointer to the array of vegetation values for each cell in the grid. Each value represents the density of vegetation in the cell.
    float* vegetation{};
    /// @brief Pointer to the array of elevation values for each cell in the grid. Each value represents the elevation of the cell.
    float* elevation{};
};

/**
 * @class ConstGridView
 * @brief Represents a read-only view of the simulation grid, providing access to cell states and properties.
 * This structure holds the width and height of the grid, along with const pointers to arrays representing the state, 
 * fuel, moisture, vegetation, and elevation of each cell. It allows for efficient access and manipulation of the grid data without modifying the underlying data.
 * 
 */
struct ConstGridView {
    std::size_t width{};
    std::size_t height{};

    /// @brief Const pointer to the array of cell states in the grid. Each cell state is represented by a value from the CellState enum.
    const CellState* state{};
    /// @brief Const pointer to the array of fuel values for each cell in the grid. Each value represents the amount of combustible material in the cell.
    const float* fuel{};
    /// @brief Const pointer to the array of moisture values for each cell in the grid. Each value represents the moisture content of the cell.
    const float* moisture{};
    /// @brief Const pointer to the array of vegetation values for each cell in the grid. Each value represents the density of vegetation in the cell.
    const float* vegetation{};
    /// @brief Const pointer to the array of elevation values for each cell in the grid. Each value represents the elevation of the cell.
    const float* elevation{};
};

/**
 * @class GridBuffers
 * @brief Manages the buffers for the simulation grid, allowing for efficient access and manipulation of cell states and properties.
 * 
 * This class holds two sets of buffers for cell states and properties, allowing for double buffering during the simulation.
 * It provides methods to access the current and next views of the grid, as well as swap_buffers() to swap the buffers after each simulation step. 
 * The class also provides methods to retrieve the width, height, and total cell count of the grid.
 */
class GridBuffers {
public:
    GridBuffers() = default;

    /**
     * @brief Constructs a GridBuffers instance with the specified width and height.
     * @param width The width of the grid.
     * @param height The height of the grid.
     */
    GridBuffers(std::size_t width, std::size_t height);

    /// @brief Returns the width of the grid.
    std::size_t width() const noexcept { return width_; }
    /// @brief Returns the height of the grid.
    std::size_t height() const noexcept { return height_; }
    /// @brief Returns the total number of cells in the grid.
    std::size_t cell_count() const noexcept { return width_ * height_; }

    /// @brief Returns the current view of the grid.
    GridView current_view() noexcept;
    /// @brief Returns the next view of the grid, which will be used in the next simulation step.
    GridView next_view() noexcept;
    /// @brief Returns the current view of the grid. This view is read-only and cannot be modified.
    ConstGridView current_view() const noexcept;
    /// @brief Swaps the current and next buffers, preparing for the next simulation step. This method should be called after each simulation step to update the grid state.
    void swap_buffers() noexcept;

private:
    std::size_t width_{};
    std::size_t height_{};
    std::size_t current_index_{};
    std::vector<CellState> states_[2];
    std::vector<float> fuels_[2];
    std::vector<float> moisture_;
    std::vector<float> vegetation_;
    std::vector<float> elevation_;
};

/// @brief Converts a CellState enum value to its corresponding string representation.
/// @param state The CellState enum value to convert.
/// @return const char* The string representation of the CellState value. Returns "Unknown" for unrecognized values.
const char* to_string(CellState state) noexcept;

} // namespace ember
