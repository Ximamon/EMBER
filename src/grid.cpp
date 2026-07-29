#include "ember/grid.hpp"

#include <stdexcept>

namespace ember {

GridBuffers::GridBuffers(std::size_t width, std::size_t height)
    : width_(width), height_(height), current_index_(0) {
    if (width == 0 || height == 0 || width > static_cast<std::size_t>(-1) / height) {
        throw std::invalid_argument("invalid grid dimensions");
    }
    const auto count = width * height;
    for (auto& states : states_) {
        states.resize(count, CellState::Unburned);
    }
    for (auto& fuels : fuels_) {
        fuels.resize(count, 0.0F);
    }
    moisture_.resize(count, 0.0F);
    vegetation_.resize(count, 0.0F);
    elevation_.resize(count, 0.0F);
}

GridView GridBuffers::current_view() noexcept {
    return {width_, height_, states_[current_index_].data(), fuels_[current_index_].data(),
            moisture_.data(), vegetation_.data(), elevation_.data()};
}

GridView GridBuffers::next_view() noexcept {
    const auto next_index = 1U - current_index_;
    return {width_, height_, states_[next_index].data(), fuels_[next_index].data(),
            moisture_.data(), vegetation_.data(), elevation_.data()};
}

ConstGridView GridBuffers::current_view() const noexcept {
    return {width_, height_, states_[current_index_].data(), fuels_[current_index_].data(),
            moisture_.data(), vegetation_.data(), elevation_.data()};
}

void GridBuffers::swap_buffers() noexcept {
    current_index_ = 1U - current_index_;
}

const char* to_string(CellState state) noexcept {
    switch (state) {
    case CellState::Unburned: return "Unburned";
    case CellState::Burning: return "Burning";
    case CellState::Burned: return "Burned";
    case CellState::NonCombustible: return "Non-combustible";
    }
    return "Unknown";
}

} // namespace ember
