#!/usr/bin/env python3
"""Prepare a dated terrain case online, then run it reproducibly without network access."""
import argparse
import json
import math
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

import numpy as np
import rasterio
from rasterio.warp import transform
from prepare import prepare as prepare_terrain, centre_epsg, SOURCE

from environment import (prepare_elevation, prepare_weather, select_weather, wind_parameters,
                         parse_time, sha256, write_json, utc_now)

ROOT = Path(__file__).resolve().parents[2]
REQUIRED_FILES = ('terrain.asc', 'terrain.prj', 'elevation.asc', 'elevation.prj',
                  'elevation.json', 'weather-response.json', 'weather.json')


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def prepare_case(terrain, output, timestamp, dem=None, longitude=None, latitude=None,
                 size=None, resolution=None, source=SOURCE):
    """Publish a complete portable package only after all inputs have been validated."""
    parse_time(timestamp)
    if (longitude is None) != (latitude is None):
        raise ValueError('latitude and longitude must be supplied together')
    if longitude is not None:
        if terrain is not None:
            raise ValueError('coordinates cannot be combined with --terrain')
        centre_epsg(longitude, latitude)
    elif size is not None or resolution is not None:
        raise ValueError('size and resolution require coordinates')
    else:
        terrain = Path(terrain or ROOT / 'data/terrain/collserola.asc').resolve()
    output = Path(output).resolve()
    if output.exists():
        raise ValueError('case output already exists; choose a new directory')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.ember-case-', dir=output.parent) as temporary:
        staging = Path(temporary) / 'case'
        staging.mkdir()
        if longitude is not None:
            prepare_terrain(source, staging / 'terrain.asc', longitude, latitude,
                            512 if size is None else size, 10.0 if resolution is None else resolution)
        else:
            for extension in ('.asc', '.prj', '.json'):
                path = terrain.with_suffix(extension)
                if extension != '.json' or path.exists():
                    shutil.copyfile(path, staging / ('terrain' + extension))
        with rasterio.open(staging / 'terrain.asc') as grid:
            x, y = grid.transform * (grid.width / 2, grid.height / 2)
            lon, lat = transform(grid.crs, 'EPSG:4326', [x], [y])
        prepare_elevation(staging / 'terrain.asc', staging / 'elevation.asc', dem)
        prepare_weather(staging, lon[0], lat[0], timestamp)
        manifest = dict(schema_version=1, prepared_at=utc_now(), timestamp_utc=timestamp,
                        files={p.name: sha256(p) for p in sorted(staging.iterdir())},
                        software=dict(python=platform.python_version(), numpy=np.__version__,
                                      rasterio=rasterio.__version__, gdal=rasterio.__gdal_version__),
                        preparation_code_sha256={name: sha256(Path(__file__).with_name(name))
                                                 for name in ('case.py', 'environment.py', 'prepare.py')},
                        assumptions=['Scalar CPU only.', 'Fixed hourly weather at the crop centre.',
                                     'Uniform fuel and fuel moisture are explicit model assumptions.'])
        write_json(staging / 'case.json', manifest)
        verify_case(staging)
        staging.rename(output)
    return output


def verify_case(directory):
    directory = Path(directory)
    manifest = read_json(directory / 'case.json')
    if manifest['schema_version'] != 1 or not set(REQUIRED_FILES).issubset(manifest['files']):
        raise ValueError('incomplete or unsupported case manifest')
    for name, expected in manifest['files'].items():
        if Path(name).name != name or '/' in name or '\\' in name or name in ('.', '..'):
            raise ValueError('invalid case filename')
        if sha256(directory / name) != expected:
            raise ValueError(f'case checksum mismatch: {name}')
    weather = read_json(directory / 'weather.json')
    if weather['model'] != 'era5' or weather['timestamp_utc'] != manifest['timestamp_utc']:
        raise ValueError('case must contain ERA5 for the requested time')
    values = select_weather(read_json(directory / 'weather-response.json'), weather['timestamp_utc'])
    if values != weather['values']:
        raise ValueError('weather snapshot disagrees with original response')
    return manifest, weather


def run_case(directory, executable, output, reference, steps=500, scenarios=1, seed=42,
             fuel=1.0, moisture=0.2, base_spread=0.25, burn_rate=0.2, ignitions=()):
    directory, executable, output = Path(directory).resolve(), Path(executable).resolve(), Path(output).resolve()
    manifest, weather = verify_case(directory)
    wind = wind_parameters(weather['values']['wind_speed_10m'], weather['values']['wind_direction_10m'], reference)
    if (steps <= 0 or scenarios <= 0 or seed < 0 or
            not all(math.isfinite(v) for v in (fuel, moisture, base_spread, burn_rate)) or
            not 0 < fuel <= 1 or not 0 <= moisture <= 1 or not 0 <= base_spread <= 1 or not 0 < burn_rate <= 1):
        raise ValueError('invalid simulation parameters')
    if output.exists():
        raise ValueError('result output already exists; choose a new directory')
    executable_hash = sha256(executable)
    output.parent.mkdir(parents=True, exist_ok=True)
    # Stage results so failure never leaves an apparently complete result package.
    with tempfile.TemporaryDirectory(prefix='.ember-run-', dir=output.parent) as temporary:
        staging = Path(temporary) / 'result'
        staging.mkdir()
        inputs = staging / 'inputs'
        inputs.mkdir()
        for name in [*manifest['files'], 'case.json']:
            shutil.copyfile(directory / name, inputs / name)
        command = [str(executable), '--terrain', str(inputs / 'terrain.asc'),
                   '--elevation', str(inputs / 'elevation.asc'), '--output', str(staging), '--export', 'csv',
                   '--steps', str(steps), '--scenarios', str(scenarios), '--seed', str(seed),
                   '--terrain-fuel', str(fuel), '--terrain-moisture', str(moisture),
                   '--base-spread', str(base_spread), '--burn-rate', str(burn_rate),
                   '--wind-direction', str(wind['wind_direction_degrees']),
                   '--wind-strength', str(wind['wind_strength'])]
        for point in ignitions:
            command += ['--ignition', point]
        result = subprocess.run(command, check=True, capture_output=True, text=True)
        log = result.stdout.replace(str(staging), str(output))
        (staging / 'simulation.log').write_text(log, encoding='utf-8')
        metadata = dict(schema_version=1, executed_at=utc_now(), executable_sha256=executable_hash,
                        runner_sha256=sha256(__file__),
                        case_manifest_sha256=sha256(inputs / 'case.json'), weather=weather,
                        elevation=read_json(inputs / 'elevation.json'), wind_conversion=wind,
                        parameters=dict(steps=steps, scenarios=scenarios, seed=seed, fuel=fuel,
                                        moisture=moisture, base_spread=base_spread, burn_rate=burn_rate,
                                        ignitions=list(ignitions)),
                        output_sha256={p.name: sha256(p) for p in staging.glob('*.csv')})
        write_json(staging / 'case-run.json', metadata)
        staging.rename(output)
    print(log, end='')
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest='action', required=True)
    prepare = actions.add_parser('prepare', help='fetch DEM and ERA5 into a reusable case')
    prepare.add_argument('--terrain', type=Path, help='prepared terrain; defaults to Collserola without coordinates')
    prepare.add_argument('--latitude', type=float, help='mainland centre latitude in degrees')
    prepare.add_argument('--longitude', type=float, help='mainland centre longitude in degrees')
    prepare.add_argument('--size', type=int, help='square grid side in cells (default: 512 with coordinates)')
    prepare.add_argument('--resolution', type=float, help='cell size in metres (default: 10 with coordinates)')
    prepare.add_argument('--dem', type=Path, help='local single-band GeoTIFF with heights in metres')
    prepare.add_argument('--time', required=True, help='exact hour in UTC: YYYY-MM-DDTHH:00Z')
    prepare.add_argument('--output', type=Path, required=True)
    run = actions.add_parser('run', help='verify and run a saved case offline')
    run.add_argument('directory', type=Path)
    run.add_argument('--executable', type=Path, required=True)
    run.add_argument('--output', type=Path, required=True)
    run.add_argument('--wind-reference', type=float, required=True, help='positive heuristic reference in m/s')
    run.add_argument('--steps', type=int, default=500)
    run.add_argument('--scenarios', type=int, default=1)
    run.add_argument('--seed', type=int, default=42)
    run.add_argument('--terrain-fuel', type=float, default=1)
    run.add_argument('--terrain-moisture', type=float, default=.2)
    run.add_argument('--base-spread', type=float, default=.25)
    run.add_argument('--burn-rate', type=float, default=.2)
    run.add_argument('--ignition', action='append', default=[])
    args = parser.parse_args()
    try:
        if args.action == 'prepare':
            output = prepare_case(args.terrain, args.output, args.time, args.dem,
                                  args.longitude, args.latitude, args.size, args.resolution)
        else:
            output = run_case(args.directory, args.executable, args.output, args.wind_reference,
                              args.steps, args.scenarios, args.seed, args.terrain_fuel, args.terrain_moisture,
                              args.base_spread, args.burn_rate, args.ignition)
        print(output)
    except subprocess.CalledProcessError as error:
        parser.exit(2, f'Simulation failed: {error.stderr}\n')
    except (ValueError, OSError, KeyError, TypeError, IndexError) as error:
        parser.exit(2, f'Case {args.action} failed: {error}\n')


if __name__ == '__main__':
    main()
