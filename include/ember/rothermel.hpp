/**
 * @file rothermel.hpp
 * @brief Rothermel (1972) single-fuel-class surface fire spread rate.
 */

#pragma once

#include "ember/fuel_model.hpp"

namespace ember {

/**
 * @brief Quasi-steady-state Rothermel surface fire spread rate, in one direction.
 *
 * Implements R = I_R * xi * (1 + phi_w + phi_s) / (rho_b * epsilon * Q_ig)
 * (Rothermel 1972; sub-formulas per Andrews 2018, RMRS-GTR-371) for a single
 * fuel class. All arguments and the return value are SI; the classic
 * imperial-unit constants are applied and undone internally.
 *
 * Two v1 simplifications versus the full published model, documented in
 * docs/rothermel-model.md:
 *  - wind_speed_along_m_s and tan_slope_along are the components of wind and
 *    slope already resolved onto the direction being evaluated (matching how
 *    the existing empirical model projects wind/slope per neighbor direction)
 *    rather than computing a single head-fire direction and an elliptical
 *    fire shape.
 *  - the slope term is signed (uphill speeds up, downhill slows down) rather
 *    than the magnitude-only phi_s from the published model, which assumes
 *    slope always aids the direction of interest.
 *
 * @param fuel Fuel bed parameters for the target cell.
 * @param fuel_moisture_fraction Dead fuel moisture content, M_f, in [0, 1].
 * @param wind_speed_along_m_s Wind speed component along the spread direction (m/s); 0 or negative means no wind assist.
 * @param tan_slope_along Signed tangent of the slope along the spread direction; positive is upslope.
 * @return Rate of spread in m/s, or 0 for degenerate fuel parameters (e.g. non-burnable).
 */
float rothermel_spread_rate_m_s(
    const FuelModelParams& fuel,
    float fuel_moisture_fraction,
    float wind_speed_along_m_s,
    float tan_slope_along
) noexcept;

} // namespace ember
