#include "ember/simulation.hpp"

#include "ember/random.hpp"

#include <immintrin.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace ember {
namespace {

constexpr float inverse_sqrt_two = 0.70710678118654752440F;

struct NeighborDirection {
    int row_offset;
    int column_offset;
};

constexpr std::array<NeighborDirection, 8> neighbor_directions{{
    {-1, -1}, {-1, 0}, {-1, 1}, {0, -1},
    {0, 1},   {1, -1}, {1, 0},  {1, 1},
}};

std::uint64_t as_u64(std::size_t value) {
    return static_cast<std::uint64_t>(value);
}

__m256 clamp_ps(__m256 value, float minimum, float maximum) {
    return _mm256_min_ps(_mm256_max_ps(value, _mm256_set1_ps(minimum)),
                         _mm256_set1_ps(maximum));
}

__m256 load_state_mask(const CellState* states, CellState expected) {
    const __m128i state_bytes = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(states));
    const __m256i state_values = _mm256_cvtepu8_epi32(state_bytes);
    const __m256i expected_values = _mm256_set1_epi32(static_cast<int>(expected));
    return _mm256_castsi256_ps(_mm256_cmpeq_epi32(state_values, expected_values));
}

} // namespace

std::size_t WildfireSimulation::step_avx2(std::size_t step_index) {
    const ConstGridView current = static_cast<const GridBuffers&>(grid_).current_view();
    auto next = grid_.next_view();
    std::size_t burning_next = 0;

    const std::size_t width = config_.width;
    const std::size_t height = config_.height;
    const __m256 one = _mm256_set1_ps(1.0F);
    const __m256 zero = _mm256_setzero_ps();
    const __m256 burn_rate = _mm256_set1_ps(config_.burn_rate);
    const __m256 base_spread = _mm256_set1_ps(config_.base_spread);
    const __m256 wind_strength = _mm256_set1_ps(config_.wind_strength);
    const __m256 slope_scale = _mm256_set1_ps(config_.slope_scale);

    for (std::size_t row = 0; row < height; ++row) {
        const std::size_t row_start = row * width;
        const bool can_vectorize_row = height >= 3 && row > 0 && row + 1 < height && width >= 10;
        if (!can_vectorize_row) {
            for (std::size_t column = 0; column < width; ++column) {
                burning_next += step_cell(current, next, step_index, row, column);
            }
            continue;
        }

        burning_next += step_cell(current, next, step_index, row, 0);
        std::size_t column = 1;
        for (; column <= width - 9; column += 8) {
            const std::size_t index = row_start + column;
            const __m256 current_fuel = _mm256_loadu_ps(current.fuel + index);
            const __m256 burning_fuel = _mm256_max_ps(zero, _mm256_sub_ps(current_fuel, burn_rate));
            const __m256 burning_mask = load_state_mask(current.state + index, CellState::Burning);
            const __m256 next_fuel = _mm256_blendv_ps(current_fuel, burning_fuel, burning_mask);
            _mm256_storeu_ps(next.fuel + index, next_fuel);

            std::array<double, 8> probability_not_ignited{};
            probability_not_ignited.fill(1.0);
            bool target_vectors_loaded = false;
            __m256 target_vegetation = _mm256_setzero_ps();
            __m256 target_elevation = _mm256_setzero_ps();
            __m256 target_fuel = _mm256_setzero_ps();
            __m256 moisture_factor = _mm256_setzero_ps();

            for (const auto& direction : neighbor_directions) {
                const std::size_t neighbor_row =
                    static_cast<std::size_t>(static_cast<std::ptrdiff_t>(row) + direction.row_offset);
                const std::size_t neighbor_column =
                    static_cast<std::size_t>(static_cast<std::ptrdiff_t>(column) + direction.column_offset);
                const std::size_t neighbor_index = neighbor_row * width + neighbor_column;

                const __m256 burning_neighbors =
                    load_state_mask(current.state + neighbor_index, CellState::Burning);
                const __m256i burning_neighbor_values = _mm256_castps_si256(burning_neighbors);
                if (_mm256_testz_si256(burning_neighbor_values, burning_neighbor_values) != 0) {
                    continue;
                }

                alignas(32) std::array<int, 8> burning_lanes{};
                _mm256_store_si256(
                    reinterpret_cast<__m256i*>(burning_lanes.data()),
                    _mm256_castps_si256(burning_neighbors));
                if (!target_vectors_loaded) {
                    const __m256 target_moisture = _mm256_loadu_ps(current.moisture + index);
                    target_vegetation = _mm256_loadu_ps(current.vegetation + index);
                    target_elevation = _mm256_loadu_ps(current.elevation + index);
                    target_fuel = clamp_ps(current_fuel, 0.0F, 1.0F);
                    const __m256 moisture = clamp_ps(target_moisture, 0.0F, 1.0F);
                    moisture_factor = _mm256_sub_ps(
                        one, _mm256_mul_ps(_mm256_set1_ps(0.8F), moisture));
                    target_vectors_loaded = true;
                }

                const __m256 neighbor_elevation = _mm256_loadu_ps(current.elevation + neighbor_index);
                const float distance_factor = direction.row_offset != 0 && direction.column_offset != 0
                                                  ? inverse_sqrt_two
                                                  : 1.0F;
                const float direction_x = static_cast<float>(-direction.column_offset) * distance_factor;
                const float direction_y = static_cast<float>(direction.row_offset) * distance_factor;
                const __m256 alignment = _mm256_add_ps(
                    _mm256_mul_ps(_mm256_set1_ps(direction_x), _mm256_set1_ps(wind_x_)),
                    _mm256_mul_ps(_mm256_set1_ps(direction_y), _mm256_set1_ps(wind_y_)));
                const __m256 wind_factor = clamp_ps(
                    _mm256_add_ps(one, _mm256_mul_ps(wind_strength, alignment)), 0.25F, 2.0F);
                const __m256 slope = clamp_ps(
                    _mm256_div_ps(_mm256_sub_ps(target_elevation, neighbor_elevation), slope_scale),
                    -1.0F, 1.0F);
                const __m256 slope_factor = clamp_ps(
                    _mm256_add_ps(one, _mm256_mul_ps(_mm256_set1_ps(0.5F), slope)), 0.5F, 1.5F);

                __m256 probability = _mm256_mul_ps(base_spread, target_fuel);
                probability = _mm256_mul_ps(probability, target_vegetation);
                probability = _mm256_mul_ps(probability, moisture_factor);
                probability = _mm256_mul_ps(probability, wind_factor);
                probability = _mm256_mul_ps(probability, slope_factor);
                probability = _mm256_mul_ps(probability, _mm256_set1_ps(distance_factor));
                probability = clamp_ps(probability, 0.0F, 1.0F);

                alignas(32) std::array<float, 8> probabilities{};
                _mm256_store_ps(probabilities.data(), probability);
                for (std::size_t lane = 0; lane < probabilities.size(); ++lane) {
                    if (burning_lanes[lane] != 0) {
                        probability_not_ignited[lane] *=
                            1.0 - static_cast<double>(probabilities[lane]);
                    }
                }
            }

            alignas(32) std::array<int, 8> current_states{};
            for (std::size_t lane = 0; lane < current_states.size(); ++lane) {
                current_states[lane] = static_cast<int>(current.state[index + lane]);
            }

            for (std::size_t lane = 0; lane < current_states.size(); ++lane) {
                const std::size_t cell_index = index + lane;
                const auto state = static_cast<CellState>(current_states[lane]);
                if (state == CellState::NonCombustible || state == CellState::Burned) {
                    next.state[cell_index] = state;
                    continue;
                }
                if (state == CellState::Burning) {
                    next.state[cell_index] = next.fuel[cell_index] <= 0.0F
                                                  ? CellState::Burned
                                                  : CellState::Burning;
                    if (next.state[cell_index] == CellState::Burning) {
                        ++burning_next;
                    }
                    continue;
                }
                
                // If any neighbor isnt burning, we can skip the ignition probability calculation for this cell.
                if (probability_not_ignited[lane] >= 1.0) {
                    next.state[cell_index] = CellState::Unburned;
                    continue;
                }

                const double ignition_probability = 1.0 - probability_not_ignited[lane];
                const double draw = uniform01(keyed_hash(
                    scenario_seed_, random_tag::spread, as_u64(step_index), as_u64(cell_index)));
                next.state[cell_index] = draw < ignition_probability
                                             ? CellState::Burning
                                             : CellState::Unburned;
                if (next.state[cell_index] == CellState::Burning) {
                    ++burning_next;
                }
            }
        }

        for (; column < width; ++column) {
            burning_next += step_cell(current, next, step_index, row, column);
        }
    }
    return burning_next;
}

} // namespace ember
