/**
 * @file grid.cpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Implementation of the grid data structures.
 * @version 0.5
 * @date 29/7/2026
 * 
 * 
 */

#include "ember/grid.hpp"

#include <stdexcept>

namespace ember {

/**
 * @brief Constructs grid buffers and allocates underlying storage vectors.
 */
GridBuffers::GridBuffers(std::size_t width, std::size_t height)
    : width_(width), height_(height), current_index_(0) {
    
    // Check for mathematical overflow when multiplying width * height
    // (e.g., if a user inputs absurdly large numbers in the config)
    if (width == 0 || height == 0 || width > static_cast<std::size_t>(-1) / height) {
        throw std::invalid_argument("invalid grid dimensions");
    }
    
    const auto count = width * height;
    
    // states_ and fuels_ have size [2] (Double Buffering) because their values 
    // change during each simulation tick, and we need to read from the old 
    // and write to the new simultaneously without data races.
    for (auto& states : states_) {
        states.resize(count, CellState::Unburned);
    }
    for (auto& fuels : fuels_) {
        fuels.resize(count, 0.0F);
    }
    // burn_fractions_ tracks the Rothermel model's accumulated P_burn per cell and is
    // double-buffered for the same reason as fuels_: it changes every step.
    for (auto& burn_fractions : burn_fractions_) {
        burn_fractions.resize(count, 0.0F);
    }

    // moisture_, vegetation_, elevation_, and fuel_classes_ only need a single buffer
    // because they are static terrain properties (they do not change during the fire).
    moisture_.resize(count, 0.0F);
    vegetation_.resize(count, 0.0F);
    elevation_.resize(count, 0.0F);
    fuel_classes_.resize(count, 0U);
}

/**
 * @brief Retrieves a mutable view of the active simulation buffers.
 */
GridView GridBuffers::current_view() noexcept {
    return {width_, height_, states_[current_index_].data(), fuels_[current_index_].data(),
            moisture_.data(), vegetation_.data(), elevation_.data(),
            fuel_classes_.data(), burn_fractions_[current_index_].data()};
}

/**
 * @brief Retrieves a mutable view of the target ping-pong buffers for the next timestep.
 */
GridView GridBuffers::next_view() noexcept {
    // Fast mathematical trick to alternate between index 0 and 1 without using an 'if' branch
    const auto next_index = 1U - current_index_;
    return {width_, height_, states_[next_index].data(), fuels_[next_index].data(),
            moisture_.data(), vegetation_.data(), elevation_.data(),
            fuel_classes_.data(), burn_fractions_[next_index].data()};
}

/**
 * @brief Retrieves a read-only view of the active simulation buffers.
 */
ConstGridView GridBuffers::current_view() const noexcept {
    return {width_, height_, states_[current_index_].data(), fuels_[current_index_].data(),
            moisture_.data(), vegetation_.data(), elevation_.data(),
            fuel_classes_.data(), burn_fractions_[current_index_].data()};
}

/**
 * @brief Alternates the active ping-pong double buffer index.
 */
void GridBuffers::swap_buffers() noexcept {
    // At the end of the tick, the "next" buffer becomes the "current" buffer
    current_index_ = 1U - current_index_;
}

/**
 * @brief Converts a CellState enum value to its human-readable string representation.
 */
const char* to_string(CellState state) noexcept {
    // Direct conversion for logging and result export
    switch (state) {
    case CellState::Unburned: return "Unburned";
    case CellState::Burning: return "Burning";
    case CellState::Burned: return "Burned";
    case CellState::NonCombustible: return "Non-combustible";
    }
    return "Unknown";
}

} // namespace ember
