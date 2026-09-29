"""Offline tests for metric relief, ERA5 validation and portable case replay."""
import copy
import csv
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
import rasterio
from rasterio.transform import from_origin
from rasterio.warp import transform

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/terrain'))
import environment
import case

TIME = '2025-08-01T12:00Z'


def weather_response():
    return dict(latitude=41.5, longitude=2.0, utc_offset_seconds=0,
                hourly_units=environment.WEATHER_UNITS.copy(),
                hourly=dict(time=['2025-08-01T12:00'], wind_speed_10m=[5], wind_direction_10m=[90],
                            temperature_2m=[30], relative_humidity_2m=[40], precipitation=[0]))


class EnvironmentTests(unittest.TestCase):
    def test_coordinate_arguments(self):
        for arguments in (dict(latitude=40), dict(longitude=-4),
                          dict(latitude=40, longitude=-4, terrain='existing.asc'),
                          dict(size=32), dict(resolution=20),
                          dict(latitude=28, longitude=-16),
                          dict(latitude=float('nan'), longitude=-4)):
            with self.subTest(arguments=arguments), tempfile.TemporaryDirectory() as temporary:
                output = Path(temporary) / 'case'
                values = dict(terrain=None, output=output, timestamp=TIME)
                values.update(arguments)
                with self.assertRaises(ValueError):
                    case.prepare_case(**values)
                self.assertFalse(output.exists())

    def test_coordinate_cases_across_utm_zones(self):
        executable = Path(os.environ.get('EMBER_EXECUTABLE', ROOT / 'build/ember')).resolve()
        for longitude, epsg in [(-8, 32629), (-6.0001, 32629), (-6, 32630),
                                (-4, 32630), (-.0001, 32630), (0, 32631), (2, 32631)]:
            with self.subTest(longitude=longitude), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                source, dem = directory / 'fuel.tif', directory / 'dem.tif'
                geometry = dict(width=100, height=100, count=1, crs='EPSG:4326',
                                transform=from_origin(longitude - .01, 41.01, .0002, .0002))
                pixels = np.full((100, 100), 102, dtype=np.uint16)
                pixels[:50, :50] = 91
                with rasterio.open(source, 'w', driver='GTiff', dtype='uint16', nodata=0, **geometry) as grid:
                    grid.write(pixels, 1)
                with rasterio.open(dem, 'w', driver='GTiff', dtype='float32', nodata=-9999, **geometry) as grid:
                    grid.write(np.full((100, 100), 100, dtype=np.float32), 1)
                package = directory / 'case'
                response = json.dumps(weather_response()).encode()
                with patch.object(environment, 'urlopen', return_value=io.BytesIO(response)):
                    case.prepare_case(None, package, TIME, dem, longitude, 41, 32, 10, str(source))
                with rasterio.open(package / 'terrain.asc') as grid, rasterio.open(package / 'elevation.asc') as relief:
                    self.assertEqual(grid.crs.to_epsg(), epsg)
                    self.assertEqual(grid.crs, relief.crs)
                    self.assertEqual(grid.transform, relief.transform)
                    self.assertEqual(grid.shape, (32, 32))
                    x, y = grid.transform * (16, 16)
                    lon, lat = transform(grid.crs, 'EPSG:4326', [x], [y])
                    self.assertAlmostEqual(lon[0], longitude)
                    self.assertAlmostEqual(lat[0], 41)
                    self.assertEqual(int(grid.read(1)[0, 0]), 91)
                    self.assertEqual(int(grid.read(1)[-1, -1]), 102)
                metadata = case.read_json(package / 'terrain.json')
                self.assertEqual(metadata['centre_lon_lat'], [longitude, 41])
                weather = case.read_json(package / 'weather.json')['requested_coordinates']
                self.assertAlmostEqual(weather['longitude'], longitude)
                self.assertAlmostEqual(weather['latitude'], 41)
                with patch.object(environment, 'urlopen', side_effect=AssertionError('unexpected network')):
                    case.run_case(package, executable, directory / 'result', 10, steps=3)
                self.assertEqual(case.read_json(directory / 'result/run.json')['epsg'], epsg)
                # Equal numeric geometry does not make two different projections compatible.
                (package / 'elevation.prj').write_text(rasterio.crs.CRS.from_epsg(
                    32630 if epsg != 32630 else 32629).to_wkt())
                failed = subprocess.run([str(executable), '--terrain', str(package / 'terrain.asc'),
                                         '--elevation', str(package / 'elevation.asc')], capture_output=True, text=True)
                self.assertNotEqual(failed.returncode, 0)
                self.assertIn('projection does not match', failed.stderr)
                with patch.object(environment, 'urlopen', side_effect=OSError('offline')):
                    with self.assertRaises(OSError):
                        case.prepare_case(None, directory / 'failed', TIME, dem, longitude, 41, 32, 10, str(source))
                self.assertFalse((directory / 'failed').exists())
                with rasterio.open(source, 'r+') as grid:
                    grid.write(np.zeros_like(pixels), 1)
                with self.assertRaisesRegex(ValueError, 'no combustible'):
                    case.prepare_case(None, directory / 'empty', TIME, dem, longitude, 41, 32, 10, str(source))
                self.assertFalse((directory / 'empty').exists())

    def test_wind_cardinals_and_reference(self):
        for source, target in [(0, 270), (90, 180), (180, 90), (270, 0), (360, 270)]:
            converted = environment.wind_parameters(5, source, 10)
            self.assertEqual(converted['wind_direction_degrees'], target)
            self.assertEqual(converted['wind_strength'], .5)
        self.assertTrue(environment.wind_parameters(15, 0, 10)['clipped'])
        self.assertEqual(environment.wind_parameters(0, 0, 10)['wind_strength'], 0)
        for reference in (0, -1, float('nan'), float('inf')):
            with self.assertRaises(ValueError):
                environment.wind_parameters(1, 0, reference)

    def test_weather_validation(self):
        raw = weather_response()
        self.assertEqual(environment.select_weather(raw, TIME)['relative_humidity_2m'], 40)
        for variable in environment.WEATHER_UNITS:
            for value in (None, float('nan'), float('inf')):
                bad = copy.deepcopy(raw)
                bad['hourly'][variable] = [value]
                with self.assertRaises(ValueError):
                    environment.select_weather(bad, TIME)
            bad = copy.deepcopy(raw)
            bad['hourly_units'][variable] = 'wrong'
            with self.assertRaises(ValueError):
                environment.select_weather(bad, TIME)
        with self.assertRaises(ValueError):
            environment.select_weather(raw, '2025-08-01T13:00Z')
        with self.assertRaises(ValueError):
            environment.select_weather(raw, '2025-08-01T12:30Z')
        raw['utc_offset_seconds'] = 3600
        with self.assertRaises(ValueError):
            environment.select_weather(raw, TIME)

    def test_tile_selection(self):
        urls = environment.copernicus_tiles((2.05, 41.4, 2.15, 41.5))
        self.assertEqual(len(urls), 1)
        self.assertIn('N41_00_E002_00', urls[0])
        self.assertEqual(len(environment.copernicus_tiles((1.99, 41.99, 2.01, 42.01))), 4)

    def write_dem(self, directory, missing=False):
        path = directory / 'dem.tif'
        # Larger than the fixture, with north/south and east/west variation.
        heights = np.arange(30, dtype=np.float32).reshape(5, 6) * 2 - 10
        if missing:
            heights[:] = -9999
        with rasterio.open(path, 'w', driver='GTiff', width=6, height=5, count=1, dtype='float32',
                           nodata=-9999, crs='EPSG:32631', transform=from_origin(419990, 4590040, 10, 10)) as dem:
            dem.write(heights, 1)
        return path, heights[1:4, 1:5]

    def test_elevation_coverage_alignment_and_orientation(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source, expected = self.write_dem(directory)
            terrain = ROOT / 'tests/fixtures/terrain.asc'
            output = directory / 'elevation.asc'
            environment.prepare_elevation(terrain, output, source)
            with rasterio.open(terrain) as grid, rasterio.open(output) as relief:
                self.assertEqual(relief.transform, grid.transform)
                self.assertEqual(relief.crs, grid.crs)
                valid = grid.read(1) != 0
                np.testing.assert_allclose(relief.read(1)[valid], expected[valid])
            self.write_dem(directory, missing=True)
            with self.assertRaisesRegex(ValueError, 'missing elevation'):
                environment.prepare_elevation(terrain, output, source)

    def test_portable_case_offline_and_reproducible(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            dem, expected = self.write_dem(directory)
            package = directory / 'case'
            response = json.dumps(weather_response()).encode()
            with patch.object(environment, 'urlopen', return_value=io.BytesIO(response)) as request:
                case.prepare_case(ROOT / 'tests/fixtures/terrain.asc', package, TIME, dem)
                self.assertIn('models=era5', request.call_args.args[0])
                self.assertIn('wind_speed_unit=ms', request.call_args.args[0])
            dem.unlink()
            executable = Path(os.environ.get('EMBER_EXECUTABLE', ROOT / 'build/ember')).resolve()
            with patch.object(environment, 'urlopen', side_effect=AssertionError('offline replay accessed network')):
                for name in ('first', 'repeat'):
                    case.run_case(package, executable, directory / name, 10, steps=8, scenarios=2)
            first, repeat = directory / 'first', directory / 'repeat'
            self.assertEqual((first / 'scenario_000000_final.csv').read_bytes(),
                             (repeat / 'scenario_000000_final.csv').read_bytes())
            metadata = case.read_json(first / 'case-run.json')
            self.assertEqual(metadata['wind_conversion']['wind_direction_degrees'], 180)
            self.assertEqual(metadata['parameters']['moisture'], .2)
            self.assertEqual(case.read_json(first / 'run.json')['elevation_units'], 'metres')
            with (first / 'scenario_000000_final.csv').open() as stream:
                for i, row in enumerate(csv.DictReader(stream)):
                    if int(row['valid']):
                        self.assertAlmostEqual(float(row['elevation']), float(expected.flat[i]))
            # Replay the copy contained in the results after moving the original package away.
            package.rename(directory / 'moved-package')
            with patch.object(environment, 'urlopen', side_effect=AssertionError('unexpected network')):
                case.run_case(first / 'inputs', executable, directory / 'portable', 10, steps=8, scenarios=2)
            self.assertEqual((first / 'scenario_000000_final.csv').read_bytes(),
                             (directory / 'portable/scenario_000000_final.csv').read_bytes())
            subprocess.run([sys.executable, str(ROOT / 'tools/terrain/render.py'), str(first)],
                           check=True, capture_output=True)
            self.assertGreater((first / 'scenario_000000.png').stat().st_size, 10000)
            with (first / 'inputs/weather-response.json').open('ab') as stream:
                stream.write(b' ')
            with self.assertRaisesRegex(ValueError, 'checksum'):
                case.verify_case(first / 'inputs')

    def test_failed_download_does_not_publish_case(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            dem, _ = self.write_dem(directory)
            with patch.object(environment, 'urlopen', side_effect=OSError('offline')):
                with self.assertRaises(OSError):
                    case.prepare_case(ROOT / 'tests/fixtures/terrain.asc', directory / 'case', TIME, dem)
            self.assertFalse((directory / 'case').exists())


if __name__ == '__main__':
    unittest.main()
