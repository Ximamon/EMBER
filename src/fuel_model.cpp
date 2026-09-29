#include "ember/fuel_model.hpp"

namespace ember {

FuelClass fuel_class_for_code(int zafm_code) noexcept {
    if (zafm_code >= 100 && zafm_code < 120) return FuelClass::Grass;
    if (zafm_code >= 120 && zafm_code < 140) return FuelClass::Shrub; // grass-shrub, treated as shrub in v1
    if (zafm_code >= 140 && zafm_code < 160) return FuelClass::Shrub;
    if (zafm_code >= 160 && zafm_code < 180) return FuelClass::TimberUnderstory;
    if (zafm_code >= 180 && zafm_code < 190) return FuelClass::TimberLitter;
    return FuelClass::NonBurnable; // NB classes (<100) and anything else unclassified
}

FuelModelParams fuel_model_params(FuelClass fuel_class) noexcept {
    // Representative values per family, not per exact ZAFM/Burgan code (see fuel_model.hpp).
    // heat_content follows the ~8000 Btu/lb value Albini/Anderson assume for most wildland
    // fuel models (~18,600 kJ/kg); moisture_of_extinction follows Scott & Burgan's climate
    // bands (arid ~15%, sub-humid ~20-25%).
    switch (fuel_class) {
    case FuelClass::Grass:
        return {0.35F, 11'483.0F, 0.40F, 0.15F, 18'600.0F};
    case FuelClass::Shrub:
        return {2.00F, 4'000.0F, 1.20F, 0.20F, 18'600.0F};
    case FuelClass::TimberUnderstory:
        return {1.00F, 3'000.0F, 0.60F, 0.25F, 18'600.0F};
    case FuelClass::TimberLitter:
        return {0.60F, 2'000.0F, 0.05F, 0.25F, 18'600.0F};
    case FuelClass::NonBurnable:
        break;
    }
    return {0.0F, 1.0F, 1.0F, 1.0F, 0.0F}; // inert; combustible_code() keeps this out of the spread loop
}

} // namespace ember
