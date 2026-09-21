# Real terrain demo (v1)

EMBER uses a real **mapped distribution of fuel categories** with a simplified,
uncalibrated stochastic spread model. This is not a forecast of an actual fire.
The map is derived from remote sensing and fuel typology, not live measurements.

## Run the prepared Collserola demo offline

The repository includes `data/terrain/collserola.asc`, its `.prj`, and a JSON
provenance manifest. Once dependencies are installed, no network is needed.

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r tools/terrain/requirements.txt
# If CMake is not installed, install it in this same environment:
.venv/bin/python -m pip install cmake
.venv/bin/cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DEMBER_ENABLE_MPI=OFF -DEMBER_ENABLE_CUDA=OFF -DAVX2=OFF
.venv/bin/cmake --build build --parallel 2
.venv/bin/ctest --test-dir build --output-on-failure
```

The three workflow commands (preparation is optional for the bundled crop):

```sh
# 1. Prepare from the public COG; this command requires network access.
.venv/bin/python tools/terrain/prepare.py
# 2. Simulate from local files.
./build/ember --terrain data/terrain/collserola.asc --steps 500 --scenarios 2 \
  --seed 42 --terrain-fuel 1 --terrain-moisture 0.2 \
  --output out/collserola --export csv
# 3. Render from local results.
.venv/bin/python tools/terrain/render.py out/collserola
```

For a more compact growing-fire illustration use `--steps 150`. Neither 150 nor
500 iterations implies a duration in minutes. Do not interpret burned hectares
as a calibrated forecast. `--scenario 1` selects the second scenario when rendering.

## Data and assumptions

- Centre: 2.10° E, 41.45° N, Collserola, Catalonia.
- 512 × 512 cells, 10 m square cells, 5.12 × 5.12 km, EPSG:32631.
- Original input: EPSG:4326, UInt16 COG, one categorical band, NoData 0.
- Reading is windowed; reprojection uses nearest neighbour, never interpolation of IDs.
- Fuel is uniform 1.0 and moisture 0.2 by default; configurable using
  `--terrain-fuel` in (0,1] and `--terrain-moisture` in [0,1].
- Vegetation multiplier 1.0 and elevation 0.0: no random/artificial topography.
- Wind remains a user-specified model parameter, not observed weather.
- Land cover baseline: ESA WorldCover 2021; fuel typology baseline: 2015–2020.
- All scenarios use the same geography and initial fuel. Only their fire draws differ.

`zafm_legend.csv` and `burgan_models_table.csv` disagree on category descriptions:
for example, 102 is described as timber litter in the former and GR2 grass in the
latter. No physical parameters are inferred from these descriptions. All IDs are
preserved. Ask the dataset authors to clarify the correspondence before calibrating
per-class parameters. Non-combustible labels in the figure follow the published
table; they are not an independent validation of land use at each pixel.

Input 0 is outside the valid domain; 91, 92, 93 and 98 are valid non-combustible
classes. Other supported ZAFM codes are combustible. Both invalid and non-combustible
cells use the existing blocking state in the numerical kernel. The immutable
`TerrainData` mask, CSV `valid` and `fuel_code` distinguish them in outputs and metrics.
A diagonal Moore-neighbour connection is retained from the original model; a barrier
must therefore block diagonal as well as orthogonal paths.

## Input contract and errors

`--terrain` reads an ESRI ASCII Grid with exactly six header lines: `ncols`,
`nrows`, `xllcorner`, `yllcorner`, `cellsize`, `NODATA_value`. Header names are
case-insensitive, unique, and may appear in any order. Values follow north-to-south,
row-major order. Only integer ZAFM IDs are accepted. `NODATA_value` must be 0.
The `.prj` sidecar must identify EPSG:32631; v1 intentionally supports this metric
CRS only. The supplied preparer writes the expected WKT. A same-stem `.json`
manifest is copied into results when present.

Limits: at most 100,000 rows/columns and 16 million cells in C++; the preparer
limits the square target to 4,000 × 4,000 cells and the source read to 64 million.
Headers with invalid dimensions, nonfinite geometry, invalid UTM extents, invalid
cell sizes, unknown codes, missing/extra cells, or no combustible cells fail clearly.

The file determines width/height. Explicit conflicting `--width`/`--height` values
are errors (API callers mark these using `width_explicit`/`height_explicit`).
`--ignition X,Y` uses zero-based column,row coordinates from the northwest corner.
An ignition must be on combustible terrain; no fuel is manufactured to ignite it.
Without an explicit ignition, use the combustible cell nearest the geometric centre;
ties select the first cell in row-major order. The resolved ignition is printed and
stored in `run.json`. Unknown classes require an explicit code update, never an
implicit combustible fallback.

Real terrain is rejected in CUDA builds because the current CUDA model differs from
CPU. Use the scalar, non-MPI build above for the demo. The legacy synthetic mode
remains available by omitting `--terrain` and retains its original seeded behaviour.

## Outputs and timing

- `run.json`: geometry, resolved ignitions, seed and assumed simulation parameters.
- `terrain-source.json`: copied preprocessing manifest with DOI, source window,
  software versions, SHA-256 of the ASCII crop, assumptions and attribution.
- `scenario_NNNNNN_final.csv`: existing fields plus original `fuel_code` and `valid`.
- `summary.csv`: existing columns plus `valid_cells`, `nodata_cells`,
  `non_combustible_cells`, `initially_combustible_cells`, `burned_hectares`,
  `combustible_burned_percent`, and batch `terrain_load_seconds`.
- `scenario_NNNNNN.png`: input categories and final fire state, with metric axes,
  scale, ignition, legend, assumptions and source attribution. It is self-contained:
  rendering uses only the result directory, not the original raster or a web map.

Burned area includes burning + burned cells. `burned_percent` retains its historical
rectangular denominator; `combustible_burned_percent` divides by initial combustible
cells. Hectares use square metric cells. Hectares are blank for synthetic grids.
Valid non-combustible cells exclude NoData in the new statistics.

Terrain loading/resolution is timed once per batch (once per process if using MPI),
separately from per-scenario initialization and simulation. The historical core-time
and throughput definitions are unchanged and exclude input loading and export.
The scalar non-MPI demo is the supported measurement configuration for this release.

## Tests and follow-up structure

```sh
ctest --test-dir build --output-on-failure
.venv/bin/python -m unittest discover -s tests -p 'test_terrain_tools.py' -v
```

The C++ terrain suite covers parsing, mask/geometry, both buffers, shared geography,
independent state, ignition validation, determinism, metrics, CLI and exports. Python
integration creates a local GeoTIFF, checks projected samples against the original,
runs the simulator, checks result tables, repeats deterministically and renders after
removing source files. CI runs these offline; it does not depend on Zenodo availability.

The first delivery adds a terrain module and dedicated tools without moving the
existing engines. Next phase: separate domain, engines, execution and I/O; move the
RNG microbenchmark out of the public header; unify CPU/CUDA semantics and metrics;
stop versioning generated Doxygen HTML; archive benchmark results with configuration
and code version. This migration and GPU parity are deliberately deferred.

## Attribution

Sánchez, P., González, I., Carrillo, C., Cortés, A., Suppi, R., & Margalef, T. (2025).
*High-Resolution Fuel Map Dataset for Southern Mediterranean Europe (ZAFM Europe
v1.0, EPSG:4326).* https://doi.org/10.5281/zenodo.18788338

© Paula Sánchez, 2025. Data and derived crop: **CC BY 4.0**
(https://creativecommons.org/licenses/by/4.0/). Changes: spatial crop, nearest-neighbour
reprojection to EPSG:32631 and conversion to ASCII; category codes preserved.
Repository source code remains MIT.

© ESA WorldCover project 2021 / Contains modified Copernicus Sentinel data (2021)
processed by ESA WorldCover consortium. Zanaga et al. (2022), ESA WorldCover 10 m
2021 v200: https://doi.org/10.5281/zenodo.7254221

Aragoneses, E., García, M., & Chuvieco, E. (2022). FirEUrisk Europe fuel map:
https://doi.org/10.21950/YABYCN

Methodology: Sánchez et al. (2025), *Zone adaptive fuel mapping for high resolution
wildfire spread forecasting*: https://doi.org/10.1038/s41598-025-06402-1

Implementation references:
https://rasterio.readthedocs.io/en/stable/topics/windowed-rw.html
https://rasterio.readthedocs.io/en/stable/topics/reproject.html
