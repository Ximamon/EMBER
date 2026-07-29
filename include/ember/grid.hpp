#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ember {

enum class CellState : std::uint8_t {
    Unburned,
    Burning,
    Burned,
    NonCombustible
};

struct GridView {
    std::size_t width{};
    std::size_t height{};
    CellState* state{};
    float* fuel{};
    float* moisture{};
    float* vegetation{};
    float* elevation{};
};

struct ConstGridView {
    std::size_t width{};
    std::size_t height{};
    const CellState* state{};
    const float* fuel{};
    const float* moisture{};
    const float* vegetation{};
    const float* elevation{};
};

class GridBuffers {
public:
    GridBuffers() = default;
    GridBuffers(std::size_t width, std::size_t height);

    std::size_t width() const noexcept { return width_; }
    std::size_t height() const noexcept { return height_; }
    std::size_t cell_count() const noexcept { return width_ * height_; }

    GridView current_view() noexcept;
    GridView next_view() noexcept;
    ConstGridView current_view() const noexcept;
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

const char* to_string(CellState state) noexcept;

} // namespace ember
