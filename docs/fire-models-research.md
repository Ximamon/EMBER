# Fire spread model research: physical vs. normalized empirical

This is the research hand-off for [issue #4, "Real Datasheet Discussion"](https://github.com/Ximamon/EMBER/issues/4): whether to evolve the spread model toward a physical rate-of-spread equation or toward a data-driven version of the existing empirical model, now that a real, nation-scale fuel map (ZAFM Europe, the Spain crop posted in that issue) is available instead of synthetic terrain.

Both directions are prototyped on separate branches (see [Branches](#branches)) so they can be evaluated independently before committing to one.

## 1. Where the current model stands

The shipped model ([docs/model.md](model.md)) already computes, per burning neighbor $j$ of target cell $i$:

$$
p_{j \to i} = \text{clamp01}\big(P_{\text{base}} \cdot M_{\text{fuel}} \cdot M_{\text{veg}} \cdot f_{\text{moisture}} \cdot f_{\text{wind}} \cdot f_{\text{slope}} \cdot f_{\text{distance}}\big)
$$

implemented twice (scalar path and the hot-loop copy) in [`src/simulation.cpp:134-165`](../src/simulation.cpp) and [`:167-191`](../src/simulation.cpp). This is already close in shape to "approach 2" below — the gap is what feeds it, not the formula itself.

For real terrain, `initialize_terrain()` ([`src/simulation.cpp:101-113`](../src/simulation.cpp)) only uses the ZAFM raster to decide **combustible vs. non-combustible** (`combustible_code(code)`, [`src/terrain.cpp:23`](../src/terrain.cpp)). Every other field is a flat constant regardless of which of the ~20 known ZAFM classes a cell has:

```cpp
current.fuel[i]       = burns ? config_.terrain_fuel : 0.0F;   // one scalar for all classes
current.moisture[i]   = config_.terrain_moisture;              // one scalar, whole grid
current.vegetation[i] = 1.0F;                                  // constant
current.elevation[i]  = 0.0F;                                  // flat, no DEM
```

`tools/terrain/prepare.py` bakes the same assumption into the manifest it writes (`assumptions=dict(fuel=1.0, moisture=0.2, vegetation=1.0, elevation=0.0)`, [`tools/terrain/prepare.py:87`](../tools/terrain/prepare.py)). So today "real data" means a real combustible mask over a real coastline, not yet real per-class fuel behavior.

Scale gap: `prepare.py` is hard-limited to a centre in UTM zone 31N and a ≤4000×4000 crop ([`tools/terrain/prepare.py:24-28`](../tools/terrain/prepare.py)) — enough for the bundled Collserola demo, not for the Spain-wide raster (`ESP_4326_ZAFM.tif`) shown in issue #4. Covering the country needs either a coarser cell size, tiling into multiple crops, or reusing the existing MPI path to split a national domain across ranks. That's a data-pipeline task orthogonal to which spread model wins below.

## 2. Shared blocker: fuel code → physical meaning

Both approaches need a table mapping each ZAFM code to fuel properties. [`docs/real-terrain.md`](real-terrain.md#data-and-assumptions) already flags that this mapping is **not settled**: `zafm_legend.csv` and `burgan_models_table.csv` disagree on what the same code means (e.g. code 102 is "timber litter" in one and "GR2 grass" — a standard Scott & Burgan class — in the other), and the docs explicitly defer calibration until the dataset authors clarify it.

Two candidate sources for the table, once the mapping is resolved:

- **Scott & Burgan 40** standard fuel models (GTR-153): the generic reference table (load, surface-area-to-volume ratio $\sigma$, bed depth $\delta$, moisture of extinction $M_x$, heat content $h$ per class) used across US fire behavior tools. The ZAFM codes in this repo's known set (`102,104,106-109` grass, `142,143,145,147-149` shrub, `161-163,165` timber-understory, `183` timber-litter — [`src/terrain.cpp:14-19`](../src/terrain.cpp)) follow this numbering *if* the legend mismatch above turns out not to matter for these specific classes.
- **FCCS / medfate**: [CREAF's medfate package](https://emf-creaf.github.io/medfatebook/fuel-characteristics-and-fire-behaviour.html) reimplements the same Rothermel inputs from Mediterranean forest-inventory data (species, DBH, cover) instead of the US fuel-model catalog, which is a closer physical match for Spanish shrub/pine-litter fuels than an off-the-shelf Scott & Burgan lookup, at the cost of needing per-species allometry rather than a flat per-code table.

Whoever picks up the ZAFM legend question in issue #4 unblocks both branches at once — it's worth resolving before deep work on either model.

## 3. Approach A (physical): Rothermel surface spread

$$
R = \frac{I_R \, \xi \, (1 + \phi_w + \phi_s)}{\rho_b \, \varepsilon \, Q_{ig}}
$$

| Symbol | Meaning | Depends on |
|---|---|---|
| $I_R$ | Reaction intensity — energy release rate of the flaming front | fuel load, heat content, moisture and mineral damping |
| $\xi$ | Propagating flux ratio — fraction of $I_R$ that preheats fuel ahead of the front | SAV ratio $\sigma$, packing ratio $\beta$ |
| $\phi_w$ | Wind coefficient | wind speed, $\sigma$, $\beta$ |
| $\phi_s$ | Slope coefficient | slope steepness, $\beta$ |
| $\rho_b$ | Oven-dry bulk density of the fuel bed | load / bed depth |
| $\varepsilon$ | Effective heating number | $\sigma$ |
| $Q_{ig}$ | Heat of preignition | fuel moisture $M_f$ |

Confirmed published sub-formulas (Rothermel 1972; restated in Andrews 2018, [RMRS-GTR-371](https://www.fs.usda.gov/rm/pubs_series/rmrs/gtr/rmrs_gtr371.pdf)) — **verify coefficients against that primary source before implementing**, this is a research summary, not a spec:

$$
\varepsilon = e^{-138/\sigma}, \qquad Q_{ig} = 250 + 1116\,M_f \ \text{(Btu/lb, imperial)}
$$

$$
\xi = \frac{e^{(0.792 + 0.681\sqrt{\sigma})(\beta + 0.1)}}{192 + 0.2595\,\sigma}
$$

$I_R = \Gamma' w_n h \,\eta_M \eta_s$ ($\Gamma'$ = optimum reaction velocity from $\sigma,\beta$; $w_n$ = net fuel load; $\eta_M,\eta_s$ = moisture/mineral damping), $\phi_w = C U^B (\beta/\beta_{op})^{-E}$, $\phi_s = 5.275\,\beta^{-0.3}(\tan\phi)^2$. These four have more moving parts than are useful to restate here without the source table in hand.

**Units**: the classic constants above are imperial (ft, min, Btu, lb). Either convert at the model boundary or use a metric reformulation (Wilson 1980, [INT-RN-292](https://www.fs.usda.gov/rm/pubs_int/int_rn292.pdf)) — picking one and being consistent matters more than which one.

**Fitting it into this codebase**: $R$ is a speed (distance/time), not a probability, so the per-step update becomes a burned-fraction accumulator instead of the current Bernoulli draw:

$$
P_{\text{burn}}(t+1) = P_{\text{burn}}(t) + \frac{R \, \Delta t}{\text{cellsize}}
$$

A cell only starts contributing to its neighbors once $P_{\text{burn}} \geq 1$; below that it just stores progress and repeats next step. This still fits the existing 8-neighbor loop in `neighbor_probability`/`step_cell` (direction-dependent $\phi_w,\phi_s$ per neighbor is exactly what that loop already does), but needs new per-cell state: the current `fuel`/`state` double buffer ([`include/ember/grid.hpp:120-127`](../include/ember/grid.hpp)) has no field for "fraction of this edge already traversed by the front" — either repurpose a buffer or add one, and decide how it composes with the existing fuel-depletion lifecycle (`docs/model.md`'s $M_{\text{fuel}}(t+1) = \max(0, M_{\text{fuel}}(t) - R_{\text{burn}})$) rather than replacing it outright.

**Extinction**: below some $R$ threshold, treat the front as extinguished (matches the issue discussion). **Known, accepted gap for v1**: if $R\,\Delta t/\text{cellsize} \gg 1$ the timestep is too coarse relative to spread speed; the issue explicitly defers fixing this, so v1 can too — just don't silently clamp it away.

**CUDA**: out of scope for a first prototype. `docs/real-terrain.md` already documents that CUDA runs a divergent model and rejects real-terrain input, with "unify CPU/CUDA semantics" listed as deferred follow-up work — a physical-model prototype should target the CPU scalar path and inherit that same follow-up rather than trying to solve GPU parity at the same time.

## 4. Approach B (simplified): normalized empirical model with real inputs

Structurally this **is** the formula already in `src/simulation.cpp` — the work is upstream of it, not in it:

- Replace the constant `target_vegetation = 1.0`, `target_moisture = config constant` in `initialize_terrain()` with values derived per cell from its ZAFM code, using the same fuel-code table from [§2](#2-shared-blocker-fuel-code--physical-meaning) (e.g. load → $M_{\text{fuel}}$, live/dead ratio → $M_{\text{veg}}$).
- Normalize each factor by the maximum value present in the *loaded* raster/config rather than a fixed clamp, so the model adapts to whatever fuel mix is in view.
- Add a tuning constant $k$: chaining five or six independent terms that are each $\leq 1$ pushes the product toward 0 fast, so in practice $k$ is more likely to need to be a boost than a small correction — expect to fit it empirically against a known reference fire rather than derive it.

This is honestly nonphysical (the issue's own framing), but it's the smaller delta from what's already merged, and the real-terrain CPU path already exists to build on ([docs/real-terrain.md](real-terrain.md)).

## 5. Comparison

| | A — Rothermel (physical) | B — Normalized (empirical) |
|---|---|---|
| Physical validity | Real ROS in m/s, calibratable | None claimed, tuning-constant dependent |
| New state needed | Per-cell burn-fraction accumulator | None — reuses existing fields |
| Fuel-table dependency | Hard requirement, precise parameters | Softer — only needs relative ordering |
| Formula change | Replaces the ignition-probability model | None; only inputs change |
| CUDA/AVX2 impact | Deferred (CPU-only prototype) | Low — same shape as today's kernels |
| Implementation effort | High | Low–medium |
| Main risk | Getting the physical constants/units wrong silently | Looking data-driven while still being arbitrary |

## 6. Recommendation

Prototype both, as the issue thread proposes — they answer different questions (B: "does real fuel heterogeneity change today's model's behavior noticeably?"; A: "can we get physically meaningful spread rates at all?") and share the [§2](#2-shared-blocker-fuel-code--physical-meaning) fuel-table groundwork. Whoever starts should resolve the ZAFM/Burgan legend ambiguity first and land it on `dev`, since both branches otherwise duplicate that investigation.

## Branches

Created off `dev` (no code yet — this document is the shared starting point on both):

- [`rothermel-fire-model`](https://github.com/Ximamon/EMBER/tree/rothermel-fire-model) — approach A.
- [`normalized-fuel-model`](https://github.com/Ximamon/EMBER/tree/normalized-fuel-model) — approach B.

## References

- Rothermel, R.C. (1972). *A mathematical model for predicting fire spread in wildland fuels.* [USDA Forest Service research paper](https://research.fs.usda.gov/download/treesearch/32533.pdf).
- Andrews, P.L. (2018). *The Rothermel surface fire spread model and associated developments: A comprehensive explanation.* [RMRS-GTR-371](https://www.fs.usda.gov/rm/pubs_series/rmrs/gtr/rmrs_gtr371.pdf).
- Wilson, R. (1980). *Reformulation of forest fire spread equations in SI units.* [INT-RN-292](https://www.fs.usda.gov/rm/pubs_int/int_rn292.pdf).
- Scott, J.H. & Burgan, R.E. (2005). *Standard fire behavior fuel models: A comprehensive set for use with Rothermel's surface fire spread model.* [GTR-153](https://gacc.nifc.gov/oncc/docs/40-Standard%20Fire%20Behavior%20Fuel%20Models.pdf).
- De Cáceres, M. et al. — [medfate: fuel characteristics and fire behaviour (CREAF)](https://emf-creaf.github.io/medfatebook/fuel-characteristics-and-fire-behaviour.html), an FCCS/Rothermel adaptation for Mediterranean fuels.
- Sánchez, P. et al. (2025). *High-Resolution Fuel Map Dataset for Southern Mediterranean Europe (ZAFM Europe v1.0).* [doi:10.5281/zenodo.18788338](https://doi.org/10.5281/zenodo.18788338) — the dataset behind issue #4 and [docs/real-terrain.md](real-terrain.md#attribution).
