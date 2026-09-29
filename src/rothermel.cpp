#include "ember/rothermel.hpp"

#include <algorithm>
#include <cmath>

namespace ember {
namespace {

// Rothermel's published constants are calibrated in imperial units (ft, min, lb, Btu).
// Converting once at the boundary avoids re-deriving an independent SI constant set.
constexpr double kg_m2_to_lb_ft2 = 0.204816;
constexpr double per_m_to_per_ft = 0.3048;
constexpr double m_to_ft = 3.28084;
constexpr double kj_kg_to_btu_lb = 0.429923;
constexpr double m_s_to_ft_min = 196.850394;
constexpr double ft_min_to_m_s = 0.00508;

// Standard Albini/Anderson assumptions shared by all fuel models.
constexpr double particle_density_lb_ft3 = 32.0;
constexpr double total_mineral_content = 0.0555;
constexpr double effective_mineral_content = 0.01;

double signed_square(double value) {
    return value * std::abs(value);
}

} // namespace

float rothermel_spread_rate_m_s(
    const FuelModelParams& fuel,
    float fuel_moisture_fraction,
    float wind_speed_along_m_s,
    float tan_slope_along) noexcept {
    if (!(fuel.load_kg_m2 > 0.0F) || !(fuel.sav_ratio_per_m > 0.0F) ||
        !(fuel.bed_depth_m > 0.0F) || !(fuel.moisture_of_extinction > 0.0F)) {
        return 0.0F; // non-burnable or degenerate fuel model
    }

    const double sigma = static_cast<double>(fuel.sav_ratio_per_m) * per_m_to_per_ft; // ft^-1
    const double load = static_cast<double>(fuel.load_kg_m2) * kg_m2_to_lb_ft2;       // lb/ft^2
    const double depth = static_cast<double>(fuel.bed_depth_m) * m_to_ft;             // ft
    const double heat = static_cast<double>(fuel.heat_content_kj_kg) * kj_kg_to_btu_lb; // Btu/lb
    const double moisture_of_extinction = static_cast<double>(fuel.moisture_of_extinction);
    const double moisture = std::clamp(static_cast<double>(fuel_moisture_fraction), 0.0, 1.0);

    const double bulk_density = load / depth; // rho_b, lb/ft^3
    const double packing_ratio = bulk_density / particle_density_lb_ft3; // beta
    const double optimum_packing_ratio = 3.348 * std::pow(sigma, -0.8189); // beta_op
    const double packing_ratio_relative = packing_ratio / optimum_packing_ratio;

    const double reaction_velocity_exponent = 133.0 * std::pow(sigma, -0.7913); // A
    const double max_reaction_velocity = std::pow(sigma, 1.5) / (495.0 + 0.0594 * std::pow(sigma, 1.5)); // Gamma'_max
    const double reaction_velocity = max_reaction_velocity *
        std::pow(packing_ratio_relative, reaction_velocity_exponent) *
        std::exp(reaction_velocity_exponent * (1.0 - packing_ratio_relative)); // Gamma'

    const double net_load = load * (1.0 - total_mineral_content); // w_n
    const double moisture_ratio = std::clamp(moisture / moisture_of_extinction, 0.0, 1.0); // r_M
    const double moisture_damping = std::clamp(
        1.0 - 2.59 * moisture_ratio + 5.11 * moisture_ratio * moisture_ratio -
            3.52 * moisture_ratio * moisture_ratio * moisture_ratio,
        0.0, 1.0); // eta_M
    const double mineral_damping = 0.174 * std::pow(effective_mineral_content, -0.19); // eta_s

    const double reaction_intensity = reaction_velocity * net_load * heat *
        moisture_damping * mineral_damping; // I_R, Btu/(ft^2 min)

    const double propagating_flux_ratio = // xi
        std::exp((0.792 + 0.681 * std::sqrt(sigma)) * (packing_ratio + 0.1)) /
        (192.0 + 0.2595 * sigma);

    const double wind_coefficient_c = 7.47 * std::exp(-0.1333 * std::pow(sigma, 0.55));
    const double wind_coefficient_b = 0.02526 * std::pow(sigma, 0.54);
    const double wind_coefficient_e = 0.715 * std::exp(-0.000359 * sigma);
    const double wind_speed_ft_min = std::max(0.0, static_cast<double>(wind_speed_along_m_s)) * m_s_to_ft_min;
    const double wind_factor = wind_speed_ft_min > 0.0
        ? wind_coefficient_c * std::pow(wind_speed_ft_min, wind_coefficient_b) *
              std::pow(packing_ratio_relative, -wind_coefficient_e)
        : 0.0; // phi_w

    // Signed simplification of phi_s = 5.275 * beta^-0.3 * tan(slope)^2: see rothermel.hpp.
    const double slope_factor = 5.275 * std::pow(packing_ratio, -0.3) *
        signed_square(static_cast<double>(tan_slope_along)); // phi_s

    const double wind_and_slope = std::max(0.1, 1.0 + wind_factor + slope_factor);
    const double effective_heating_number = std::exp(-138.0 / sigma); // epsilon
    const double heat_of_preignition = 250.0 + 1116.0 * moisture; // Q_ig, Btu/lb

    const double spread_rate_ft_min = (reaction_intensity * propagating_flux_ratio * wind_and_slope) /
        (bulk_density * effective_heating_number * heat_of_preignition);

    return static_cast<float>(spread_rate_ft_min * ft_min_to_m_s);
}

} // namespace ember
