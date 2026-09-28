/**
 * @file fuel_model.hpp
 * @brief Fuel families and representative Rothermel fuel-bed parameters.
 */

#pragma once

#include <cstdint>

namespace ember {

/**
 * @enum FuelClass
 * @brief Coarse fuel family used to drive the Rothermel spread model.
 *
 * ZAFM/Scott & Burgan numeric codes are grouped into these families instead of
 * being calibrated one-by-one: docs/real-terrain.md already documents that the
 * ZAFM legend and the standard Burgan model table disagree on what individual
 * codes mean, so per-code physical calibration is deferred. See
 * docs/fire-models-research.md for the full discussion.
 */
enum class FuelClass : std::uint8_t {
    Grass,
    Shrub,
    TimberUnderstory,
    TimberLitter,
    NonBurnable,
};

/**
 * @struct FuelModelParams
 * @brief Single-class Rothermel (1972) fuel bed inputs, SI units.
 */
struct FuelModelParams {
    float load_kg_m2;             ///< Oven-dry fuel load, w_o (kg/m^2).
    float sav_ratio_per_m;        ///< Surface-area-to-volume ratio, sigma (1/m).
    float bed_depth_m;            ///< Fuel bed depth, delta (m).
    float moisture_of_extinction; ///< Dead fuel moisture of extinction, M_x (fraction).
    float heat_content_kj_kg;     ///< Heat content, h (kJ/kg).
};

/**
 * @brief Classifies a ZAFM/Scott & Burgan style numeric fuel code into a family.
 *
 * Ranges mirror the standard Scott & Burgan 40 numbering (101-109 grass,
 * 121-129 grass-shrub, 141-149 shrub, 161-169 timber-understory, 181-189
 * timber-litter) rather than an exhaustive per-code switch, so newly accepted
 * codes in known_fuel_code() (terrain.cpp) classify sensibly without an update
 * here. Codes below 100 (ZAFM non-combustible classes) return NonBurnable.
 * @param zafm_code The raw fuel code from a loaded ZAFM/TerrainData raster.
 */
FuelClass fuel_class_for_code(int zafm_code) noexcept;

/**
 * @brief Representative fuel-bed parameters for a fuel family.
 *
 * These are order-of-magnitude placeholders for prototyping the Rothermel
 * path, not a calibrated per-model table -- see docs/fire-models-research.md,
 * section 2, for what is still needed before these can be trusted
 * quantitatively.
 * @param fuel_class The fuel family to look up.
 */
FuelModelParams fuel_model_params(FuelClass fuel_class) noexcept;

} // namespace ember
