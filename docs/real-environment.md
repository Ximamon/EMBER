# Real elevation and dated weather

EMBER can prepare a portable case containing ZAFM fuel categories, metric surface
elevation and an ERA5 weather snapshot. The probability model remains uncalibrated:
real inputs do not turn its iterations into minutes or its results into forecasts.

## Build and prepare

Use Python 3.9+ and the packages in `tools/terrain/requirements.txt`. Build the scalar
CPU executable, with MPI, AVX2 and CUDA disabled:

```sh
cmake -S . -B build-environment -DCMAKE_BUILD_TYPE=Release \
  -DEMBER_ENABLE_MPI=OFF -DEMBER_ENABLE_CUDA=OFF -DAVX2=OFF
cmake --build build-environment --config Release
python tools/terrain/case.py prepare --time 2025-08-01T12:00Z \
  --output out/collserola-case
```

The example uses the bundled Collserola crop (512 × 512 at 10 m). `--terrain` accepts
another prepared ZAFM crop in WGS84 UTM 29N, 30N or 31N. The national Spain raster remains a
source for bounded crops, not a national simulation grid. Preparation downloads only
intersecting Copernicus GLO-30 tiles and one day of ERA5 hourly data, selecting exactly
the requested UTC hour. It publishes the directory only after successful validation.
Existing output directories are never overwritten. Network failures and unavailable
hours are errors; there is no automatic substitution of a forecast or another model.

Use `--dem path/to/local.tif` instead of downloading Copernicus for a local,
single-band, georeferenced raster whose unscaled heights are in metres. The caller
must supply metric values and retain that raster's original licence and attribution.

## Prepare a mainland case by coordinates

```sh
python tools/terrain/case.py prepare --latitude 40.78 --longitude -4.05 \
  --size 512 --resolution 10 --time 2025-08-01T12:00Z \
  --output out/guadarrama-case
```

Both coordinates are required together and cannot be combined with `--terrain`.
The square grid is centred geometrically on that point; `--size` defaults to 512
cells per side and `--resolution` to 10 metres (5.12 × 5.12 km). These two options
require coordinates. Without coordinates or an explicit terrain, Collserola remains
the default. Existing output directories are never overwritten.

The preparer selects WGS84 UTM 29N, 30N or 31N from the centre longitude and keeps
that projection across the entire crop, including crossings of a zone boundary.
This workflow targets mainland Spain. Its coordinate bounds are 9.5° W–3.5° E,
36–44° N, not an administrative boundary check; valid cells depend on source coverage.
Islands are outside this workflow's supported scope. NoData remains blocked, and
crops without combustible cells fail. Missing elevation on valid cells or unavailable
weather fails preparation without publishing an incomplete case.

Fuel is cropped first, elevation is aligned to that grid, and weather is requested
at its centre. `terrain.json` records the requested centre, bounds, EPSG, resolution
and source provenance. Run and render the resulting directory with the commands below,
replacing the Collserola paths with the new case and result paths. Without an explicit
`--ignition X,Y`, ignition uses the combustible cell nearest the geometric centre;
the centre coordinate need not itself be combustible.

## Run and render offline

```sh
python tools/terrain/case.py run out/collserola-case \
  --executable build-environment/ember --wind-reference 10 \
  --steps 150 --scenarios 2 --seed 42 --terrain-moisture 0.2 \
  --output out/collserola-environment
python tools/terrain/render.py out/collserola-environment
```

On Windows with Visual Studio, use `build-environment/Release/ember.exe`.
The wind reference is required and is an explicit **heuristic**, not a calibrated
physical constant; 10 m/s above is an example. Intensity is
`min(speed_m_s / reference_m_s, 1)`, and clipping is recorded. ERA5 direction is
clockwise from north and describes where wind comes **from**. EMBER direction is
counterclockwise from east and describes where it blows **toward**, so
`ember_degrees = (270 - meteorological_degrees) % 360`.

The selected central weather cell is applied uniformly and remains constant for all
steps and scenarios. ERA5 has an approximately 0.25° grid; it does not provide 10 m
weather detail. Temperature, relative humidity and precipitation are saved as context
but do not modify the kernel. Air relative humidity is **not** fuel moisture.
Fuel and fuel moisture remain explicit uniform assumptions (`--terrain-fuel`,
`--terrain-moisture`). Optional `--ignition X,Y`, `--base-spread` and `--burn-rate`
are also supported by the case runner.

## Geometry and model behaviour

The elevation preparer uses bilinear reprojection to the exact ZAFM grid. Fuel
categories retain nearest-neighbour handling in the existing terrain preparer.
GLO-30 is a surface model including vegetation and buildings at approximately 30 m;
resampling onto 10 m cells does not create 10 m detail. Heights remain metres,
including valid negative elevations. Missing heights on any valid terrain cell are
rejected; NoData outside the ZAFM domain remains blocked.

The native CLI accepts `--terrain terrain.asc --elevation elevation.asc`. The second
ASCII grid must have the same WGS84 UTM `.prj` (EPSG:32629, 32630 or 32631), dimensions, cell size and lower-left
origin (serialization tolerance 1e-7 m). Its six header fields follow the terrain
format, but heights are floating-point and its finite NoData sentinel is explicit.

For real elevation, slope is `(target_height - source_height) / horizontal_distance`,
where distance is cell size for orthogonal neighbours and `sqrt(2) * cell_size` for
diagonal neighbours. Clamp slope to [-1,1], then use the existing `1 + 0.5 * slope`
factor. The synthetic and flat-terrain paths keep their historical arithmetic.
New elevation cases reject AVX2, MPI and CUDA builds before loading; there is no
silent backend fallback. No physical rate-of-spread model is introduced.

## Provenance and replay

- `case.json`: schema version, timestamp, preparation software versions and SHA-256
  for every packaged input. Hashes detect accidental changes, not source authenticity.
- `terrain.asc/.prj` and optional `terrain.json`: copied fuel crop and provenance.
- `elevation.asc/.prj/.json`: aligned heights, source URLs or local path, source hashes,
  resolution, interpolation and attribution. Downloaded original DEM tiles are temporary.
- `weather-response.json`: original API response bytes.
- `weather.json`: exact request URL (`models=era5`), requested and returned coordinates,
  units, selected values, UTC timestamp, download time and attribution.

Results contain an `inputs/` copy of the complete package. `case-run.json` records
the executable hash, input-manifest hash, simulation parameters, wind conversion,
source metadata and output CSV hashes. `run.json` records resolved ignition and grid
geometry; `elevation_units` is `metres` for elevation input. The renderer consumes
only result files and shows fuel classes, surface elevation, final state, time and wind.
Replay using `case.py run RESULT/inputs ... --output NEW_RESULT`; no original DEM,
original case directory or internet connection is needed. Use the same executable
and configuration for deterministic comparison; timing columns are not deterministic.

## Tests

```sh
ctest --test-dir build-environment -C Release --output-on-failure
# Set EMBER_EXECUTABLE to the absolute executable path if not using build/ember.
python -m unittest discover -s tests -p 'test_*tools.py' -v
```

Tests use local rasters and mocked HTTP responses. They cover alignment and orientation,
missing elevations, slopes and diagonal distances, wind cardinal directions and units,
unavailable weather, checksums, failed preparation, deterministic and portable replay,
and rendering. CI does not download environmental data.
Coordinate cases also cover all three supported UTM zones, zone-boundary crossings,
centred geometry, weather request coordinates and rejection of mismatched projections.

## Sources and attribution

- [Copernicus DEM GLO-30 on AWS](https://registry.opendata.aws/copernicus-dem/),
  2021 release. © DLR e.V. 2010–2014; © Airbus Defence and Space GmbH 2014–2018.
  Provided under COPERNICUS by the European Union and ESA; all rights reserved.
  The data licence linked by the registry applies to the derived elevation raster.
- [Open-Meteo Historical Weather API](https://open-meteo.com/en/docs/historical-weather-api):
  ERA5 reanalysis from Copernicus Climate Change Service / ECMWF, delivered through
  Open-Meteo under CC BY 4.0. These are model-based reanalysis fields, not station readings.
- ZAFM and underlying fuel-map attribution remain in [real-terrain.md](real-terrain.md#attribution).

Dynamic weather, physical calibration, fuel-class remapping and GPU parity are deferred.
