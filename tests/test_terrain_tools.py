"""Offline integration tests: GeoTIFF -> ASCII -> C++ -> PNG."""
import csv
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import numpy as np
import rasterio
from rasterio.transform import from_origin
from rasterio.warp import transform

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('prepare', ROOT / 'tools/terrain/prepare.py')
prepare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(prepare)


class TerrainToolsTests(unittest.TestCase):
    def test_offline_pipeline_and_geography(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            source = tmp / 'source.tif'
            # Four different quadrants detect transposition and north/south flips.
            pixels = np.full((200, 200), 102, dtype=np.uint16)
            pixels[:100, 100:] = 91
            pixels[100:, :100] = 98
            pixels[100:, 100:] = 145
            pixels[:30, :30] = 0
            with rasterio.open(source, 'w', driver='GTiff', width=200, height=200,
                               count=1, dtype='uint16', nodata=0, crs='EPSG:4326',
                               transform=from_origin(2.09, 41.46, .0001, .0001)) as src:
                src.write(pixels, 1)
            output = tmp / 'terrain.asc'
            meta = prepare.prepare(str(source), output, size=32)
            self.assertEqual(meta['ascii_sha256'], hashlib.sha256(output.read_bytes()).hexdigest())
            with rasterio.open(output) as dst, rasterio.open(source) as src:
                data = dst.read(1)
                self.assertEqual(dst.crs.to_epsg(), 32631)
                for row, col in [(2, 2), (2, 28), (28, 2), (28, 28)]:
                    x, y = dst.xy(row, col)
                    lon, lat = transform(dst.crs, src.crs, [x], [y])
                    expected = next(src.sample([(lon[0], lat[0])]))[0]
                    self.assertEqual(data[row, col], expected)
                self.assertEqual(set(np.unique(data)), {91, 98, 102, 145})
            exe = Path(os.environ.get('EMBER_EXECUTABLE', ROOT / 'build/ember')).resolve()
            result = tmp / 'result'
            command = [str(exe), '--terrain', str(output), '--steps', '8', '--scenarios', '2',
                       '--export', 'csv', '--output', str(result)]
            subprocess.run(command, check=True, capture_output=True, text=True)
            with (result / 'summary.csv').open() as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual(len(rows), 3)
            for row in rows:
                self.assertNotIn(None, row)
                self.assertNotIn(None, row.values())
            self.assertGreater(float(rows[-1]['terrain_load_seconds']), 0)
            meta_run = json.loads((result / 'run.json').read_text())
            self.assertEqual(meta_run['width'], 32)
            self.assertEqual(meta_run['fuel'], 1)
            self.assertAlmostEqual(meta_run['moisture'], .2)
            first_export = (result / 'scenario_000000_final.csv').read_bytes()
            with (result / 'scenario_000000_final.csv').open() as stream:
                cells = list(csv.DictReader(stream))
            burned = sum(r['state'] in ('Burning', 'Burned') for r in cells)
            self.assertAlmostEqual(float(rows[0]['burned_hectares']), burned * .01)
            for index, row in enumerate(cells):
                self.assertEqual(int(row['fuel_code']), int(data.flat[index]))
                if int(row['fuel_code']) < 100:
                    self.assertEqual(row['state'], 'Non-combustible')
            subprocess.run(command, check=True, capture_output=True)
            self.assertEqual(first_export, (result / 'scenario_000000_final.csv').read_bytes())
            subprocess.run([os.sys.executable, str(ROOT / 'tools/terrain/render.py'), str(result)],
                           check=True, capture_output=True)
            self.assertGreater((result / 'scenario_000000.png').stat().st_size, 10000)
            bad = subprocess.run([str(exe), '--terrain', str(output), '--width', '16'], capture_output=True)
            self.assertEqual(bad.returncode, 2)
            self.assertIn(b'conflict', bad.stderr)
            # Once all source files are removed, exported results still render offline.
            source.unlink(); output.unlink(); output.with_suffix('.prj').unlink(); output.with_suffix('.json').unlink()
            subprocess.run([os.sys.executable, str(ROOT / 'tools/terrain/render.py'), str(result)],
                           check=True, capture_output=True)

    def test_reject_unknown_source_class(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'bad.tif'
            with rasterio.open(source, 'w', driver='GTiff', width=200, height=200,
                               count=1, dtype='uint16', nodata=0, crs='EPSG:4326',
                               transform=from_origin(2.09, 41.46, .0001, .0001)) as src:
                src.write(np.full((200, 200), 999, dtype=np.uint16), 1)
            with self.assertRaisesRegex(ValueError, 'unknown source codes'):
                prepare.prepare(str(source), Path(tmp) / 'bad.asc', size=16)

    def test_packaged_crop_integrity(self):
        path = ROOT / 'data/terrain/collserola.asc'
        meta = json.loads(path.with_suffix('.json').read_text())
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), meta['ascii_sha256'])
        with rasterio.open(path) as src:
            self.assertEqual(src.crs.to_epsg(), 32631)
            self.assertEqual(src.shape, (512, 512))
            data = src.read(1)
            codes, counts = np.unique(data, return_counts=True)
            self.assertEqual({str(int(c)): int(n) for c, n in zip(codes, counts)}, meta['classes'])
            self.assertEqual(src.transform.a, 10)
            self.assertEqual(src.transform.e, -10)


if __name__ == '__main__':
    unittest.main()
