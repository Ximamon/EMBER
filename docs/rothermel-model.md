# Rothermel propagation model (experimental)

This is the physical alternative to the [simplified propagation model](model.md), built on the
`rothermel-fire-model` branch per [issue #4](https://github.com/Ximamon/EMBER/issues/4) and the
research in [docs/fire-models-research.md](fire-models-research.md). Select it with
`--spread-model rothermel`; the default (`empirical`) is completely unaffected by this model
existing in the codebase. **CPU-only**: `resolve_terrain_config` rejects `--spread-model rothermel`
under `EMBER_ENABLE_CUDA`, the same way it already rejects `--terrain`.

Like the empirical model, this is an educational implementation: the equation itself is
Rothermel's, but the fuel parameters feeding it are representative per-family placeholders, not a
calibrated per-ZAFM-code table (see [Fuel parameters](#fuel-parameters) below). Do not use it for
operational decisions.

## Spread rate

For each Moore neighbor $j$ of an unburned target cell $i$, a rate of spread is computed from
Rothermel (1972):

$$
R = \frac{I_R \, \xi \, (1 + \phi_w + \phi_s)}{\rho_b \, \varepsilon \, Q_{ig}}
$$

implemented in `rothermel_spread_rate_m_s()` ([src/rothermel.cpp](../src/rothermel.cpp)) entirely
in the classic imperial units (ft, min, lb, Btu) that the published constants below are calibrated
for, converting SI inputs/outputs only at the function boundary:

- $\varepsilon = e^{-138/\sigma}$, $Q_{ig} = 250 + 1116\,M_f$
- $\beta_{op} = 3.348\,\sigma^{-0.8189}$, $A = 133\,\sigma^{-0.7913}$,
  $\Gamma'_{max} = \sigma^{1.5}/(495 + 0.0594\,\sigma^{1.5})$,
  $\Gamma' = \Gamma'_{max} (\beta/\beta_{op})^A e^{A(1-\beta/\beta_{op})}$
- $\eta_M = 1 - 2.59\,r_M + 5.11\,r_M^2 - 3.52\,r_M^3$ where $r_M = \text{clamp}(M_f/M_x, 0, 1)$;
  $\eta_s = 0.174\,S_e^{-0.19}$
- $I_R = \Gamma' \, w_n \, h \, \eta_M \, \eta_s$, where $w_n = w_o(1-S_T)$
- $\xi = e^{(0.792 + 0.681\sqrt{\sigma})(\beta + 0.1)} / (192 + 0.2595\,\sigma)$
- $\phi_w = C\,U^B\,(\beta/\beta_{op})^{-E}$ with $C = 7.47\,e^{-0.1333\,\sigma^{0.55}}$,
  $B = 0.02526\,\sigma^{0.54}$, $E = 0.715\,e^{-0.000359\,\sigma}$

$\beta = \rho_b/\rho_{particle}$ is the packing ratio ($\rho_{particle} = 32\ \text{lb/ft}^3$); $S_T
= 0.0555$ and $S_e = 0.01$ are the standard Albini/Anderson mineral content assumptions used by
every fuel model. $\sigma$ (SAV ratio), $w_o$ (fuel load), $\delta$ (bed depth, via $\rho_b =
w_o/\delta$), $M_x$ (moisture of extinction) and $h$ (heat content) come from the target cell's
`FuelClass` (below). $M_f$ is the target cell's existing `moisture` field, reused as-is.

Two simplifications versus the published model, chosen to fit the existing per-neighbor,
8-direction architecture instead of a full elliptical fire shape:

- $\phi_w$ and $\phi_s$ are evaluated **per neighbor direction**, exactly like the empirical
  model's wind/slope factors ([docs/model.md](model.md)): wind speed is projected onto the
  propagation direction (`rothermel_neighbor_rate`, [src/simulation.cpp](../src/simulation.cpp)),
  and only a positive (headwind) projection contributes to $\phi_w$.
- $\phi_s = 5.275\,\beta^{-0.3}\,\tan^2(\text{slope})$ in the published model always aids the
  direction of interest (it assumes that direction is upslope). Here it is **signed**
  ($\text{sign}(\tan\theta) \cdot 5.275\,\beta^{-0.3}\,\tan^2\theta$) so that, matching the
  empirical model's convention, fire is slowed rather than only ever sped up when a neighbor
  direction points downhill. With `--elevation`, `tan(slope)` is the metric height
  difference divided by horizontal neighbour distance (cell size, or `sqrt(2)` times
  cell size diagonally). Synthetic inputs retain the `slope_scale` abstraction.
- $(1 + \phi_w + \phi_s)$ is floored at 0.1 so a steep, wind-free downhill direction damps spread
  heavily rather than going to zero or negative.

A direction whose resulting $R$ is below `--min-spread-rate` (default $0.0017\ \text{m/s} \approx
0.1\ \text{m/min}$) is treated as not spreading at all, per the "if R gets below a certain speed
the fire is extinguished" note in issue #4.

## Fuel parameters

ZAFM/Scott & Burgan numeric codes are grouped into four families by `fuel_class_for_code()`
([src/fuel_model.cpp](../src/fuel_model.cpp)), by numeric range (101-109 grass, 121-149
grass-shrub/shrub, 161-169 timber-understory, 181-189 timber-litter), not looked up per exact
code. Each family maps to one representative `FuelModelParams` (load, SAV ratio, bed depth,
moisture of extinction, heat content). These are **placeholders**, not calibrated values: as
[docs/fire-models-research.md](fire-models-research.md#2-shared-blocker-fuel-code--physical-meaning)
and [docs/real-terrain.md](real-terrain.md#data-and-assumptions) already document, the ZAFM
dataset's own legend disagrees with the standard Scott & Burgan table on what individual codes
mean, so per-code calibration is deferred. Replace `fuel_model_params()` once that is resolved;
nothing else needs to change, since the rest of the pipeline only depends on the `FuelClass` enum.

On real terrain, `initialize_terrain()` sets each cell's `FuelClass` from its ZAFM code. On
synthetic grids, every combustible cell uses one configurable family
(`--synthetic-fuel-class`, default Shrub; set via `SimulationConfig::synthetic_fuel_class`) scaled
by nothing else -- the per-cell `fuel`/`vegetation` scalars are empirical-model-only and are not
read by this path.

## Cell lifecycle

Rothermel replaces the empirical model's per-step Bernoulli draw with a deterministic
accumulator, per issue #4:

$$
P_{burn}(t+1) = P_{burn}(t) + \frac{R_{max}\,\Delta t}{\text{cellsize}}
$$

stored in the new `burn_fraction` grid buffer ([include/ember/grid.hpp](../include/ember/grid.hpp)),
using `R / sqrt(2)` for diagonal neighbours to account for their longer travel distance,
double-buffered like `fuel` since it changes every step. $R_{max}$ is the largest rate among the
target's currently-burning neighbors (the fastest-approaching front governs; rates are not summed,
which would double-count the same front). When $P_{burn} \geq 1$ the cell becomes `Burning`;
otherwise the fraction carries over unchanged to the next step, exactly as issue #4 describes.
$\Delta t$ is `--time-step` (default 60 simulated seconds) and cellsize is the terrain's real
`cell_size_m` when `--terrain` is used, or `--rothermel-cell-size` (default 10 m) on synthetic
grids -- this is the first place in EMBER a step is given physical duration; the empirical model
and its documentation are explicit that its steps have no such meaning.

Once a cell reaches `Burning`, it follows the *existing* fuel/`--burn-rate` lifecycle unchanged
(docs/model.md): Rothermel only governs when the front arrives, not how long a cell keeps burning
afterward.

## What this is not

- Not an elliptical fire-shape model: there is no single head-fire direction or eccentricity term,
  only the per-neighbor-direction simplification above.
- Not calibrated: fuel parameters are representative placeholders (see above), and no attempt is
  made to correct Rothermel's well-documented over-sensitivity to wind for fine fuels (the "wind
  speed limit" literature cited in docs/fire-models-research.md).
- Not available on GPU: CUDA continues to run its own, separate empirical-style kernel
  ([src/cuda/simulation_cuda.cu](../src/cuda/simulation_cuda.cu)); this model has no CUDA or AVX2
  path yet.

## CLI

```sh
./build/ember --spread-model rothermel --wind-speed 4 --time-step 60 \
  --min-spread-rate 0.0017 --width 128 --height 128 --steps 200
```

`--wind-speed` is a real wind speed in m/s (distinct from the empirical model's dimensionless
`--wind-strength`); `--wind-direction` is shared between both models.

For portable real-data cases, `tools/terrain/case.py run` accepts `--spread-model
rothermel`, `--time-step` and `--min-spread-rate`. It passes the saved ERA5 speed in
m/s directly and records the selected model in both run manifests. Fuel moisture
remains an explicit assumption; it is not inferred from air humidity.

The terrain loader additionally accepts metric EPSG:32628 (Canary Islands) and
EPSG:3035 (ETRS89 LAEA Europe) for a national grid across mainland UTM zones.
The coordinate-centred preparer still targets mainland Spain.
