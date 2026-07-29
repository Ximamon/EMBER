# Simplified propagation model

EMBER deliberately uses an educational local model. It is suitable for correctness, reproducibility and performance experiments, but it is not scientifically validated and must not be used for operational decisions.

## State and storage

Each cell is `Unburned`, `Burning`, `Burned` or `NonCombustible`. State and fuel use current/next buffers; moisture, vegetation and elevation remain constant. Every step reads only current data and writes every next cell before swapping the buffers.

The model inspects the eight-cell Moore neighborhood. Out-of-bounds neighbors are ignored. Newly ignited cells cannot influence another cell until the following step.

## Ignition probability

For every burning neighbor, a contribution is computed from the target cell's fuel, moisture and vegetation; wind alignment; elevation difference; and neighbor distance:

```text
moisture_factor = 1 - 0.8 * moisture
wind_factor     = clamp(1 + wind_strength * alignment, 0.25, 2.0)
slope           = clamp((target_elevation - neighbor_elevation) / slope_scale, -1, 1)
slope_factor    = clamp(1 + 0.5 * slope, 0.5, 1.5)
distance_factor = 1 for cardinal neighbors, 1/sqrt(2) for diagonals

p_neighbor = clamp(base_spread * fuel * vegetation * moisture_factor *
                   wind_factor * slope_factor * distance_factor, 0, 1)
```

`alignment` is the dot product between the unit vector from the burning neighbor to the target and the wind vector. Contributions are combined as independent opportunities:

```text
p_total = 1 - product(1 - p_neighbor)
```

One stateless random draw keyed by scenario, step and target cell decides ignition. A burning cell loses `burn_rate` fuel per step and becomes `Burned` at zero. A newly burning cell starts consuming fuel on its next step.

## Synthetic terrain

Fuel, moisture, vegetation, elevation and non-combustibility use independent hash tags. A scenario seed is derived from the global seed and scenario identifier. This avoids mutable RNG state and makes later parallel traversal deterministic.

Default terrain samples are independent per cell. Spatially correlated terrain, real fuel classes, dynamic weather, physical spread rates and validated slope functions are intentionally outside the MVP. Those features can replace terrain initialization and the per-neighbor probability calculation without changing the runner or buffer protocol.
