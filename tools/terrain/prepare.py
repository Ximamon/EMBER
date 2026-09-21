#!/usr/bin/env python3
"""Prepare a bounded ZAFM crop; network is needed only when source is a URL."""
import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np
import rasterio
from rasterio.crs import CRS
from rasterio.transform import from_origin, array_bounds
from rasterio.warp import transform, transform_bounds, reproject, Resampling
from rasterio.windows import Window, from_bounds

SOURCE = 'https://zenodo.org/api/records/18788338/files/ESP_4326_ZAFM.tif/content'
KNOWN = {0, 91, 92, 93, 98, 102, 104, 106, 107, 108, 109, 142, 143, 145,
         147, 148, 149, 161, 162, 163, 165, 183}
ATTRIBUTION = ('Sánchez et al. (2025), ZAFM Europe v1.0, '
               'doi:10.5281/zenodo.18788338. © Paula Sánchez 2025. CC BY 4.0.')


def prepare(source, output, longitude=2.10, latitude=41.45, size=512, resolution=10.0):
    if not (math.isfinite(longitude) and math.isfinite(latitude) and
            0 <= longitude <= 6 and 0 < latitude < 84):
        raise ValueError('v1 requires a centre in UTM zone 31N (0–6° E, 0–84° N)')
    if not 1 <= size <= 4000 or not math.isfinite(resolution) or resolution <= 0:
        raise ValueError('size must be 1–4000 and resolution must be positive and finite')
    crs = CRS.from_epsg(32631)
    xs, ys = transform('EPSG:4326', crs, [longitude], [latitude])
    half = size * resolution / 2
    dst_transform = from_origin(xs[0] - half, ys[0] + half, resolution, resolution)
    bounds = array_bounds(size, size, dst_transform)
    if bounds[0] < 100000 or bounds[2] > 900000 or bounds[1] < 0 or bounds[3] > 10000000:
        raise ValueError('crop is outside supported UTM extent')
    destination = np.zeros((size, size), dtype=np.uint16)
    # Prevent directory listings and unrelated sidecar requests on Zenodo.
    with rasterio.Env(GDAL_DISABLE_READDIR_ON_OPEN='EMPTY_DIR',
                      GDAL_HTTP_TIMEOUT='45', GDAL_HTTP_MAX_RETRY='2',
                      GDAL_HTTP_RETRY_DELAY='2'):
        with rasterio.open(source) as src:
            if src.count != 1 or src.crs != CRS.from_epsg(4326) or src.nodata != 0 or src.dtypes[0] != 'uint16':
                raise ValueError('expected a single-band UInt16 EPSG:4326 raster with NoData=0')
            if src.transform.a <= 0 or src.transform.e >= 0 or src.transform.b or src.transform.d:
                raise ValueError('expected a north-up source raster')
            bbox = transform_bounds(crs, src.crs, *bounds, densify_pts=21)
            window = from_bounds(*bbox, transform=src.transform)
            left, top = math.floor(window.col_off) - 2, math.floor(window.row_off) - 2
            right = math.ceil(window.col_off + window.width) + 2
            bottom = math.ceil(window.row_off + window.height) + 2
            window = Window(left, top, right - left, bottom - top).intersection(Window(0, 0, src.width, src.height))
            if window.width * window.height > 64000000:
                raise ValueError('source window too large; reduce size or resolution')
            pixels = src.read(1, window=window)
            unexpected = set(map(int, np.unique(pixels))) - KNOWN
            if unexpected:
                raise ValueError(f'unknown source codes: {sorted(unexpected)}')
            reproject(pixels, destination, src_transform=src.window_transform(window),
                      src_crs=src.crs, src_nodata=0, dst_transform=dst_transform,
                      dst_crs=crs, dst_nodata=0, resampling=Resampling.nearest)
            source_details = dict(width=src.width, height=src.height, crs=str(src.crs),
                                  window=[window.col_off, window.row_off, window.width, window.height])
    codes, counts = np.unique(destination, return_counts=True)
    if not np.any(destination >= 100):
        raise ValueError('crop has no combustible cells')
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix('.asc.tmp')
    with temporary.open('w', encoding='ascii', newline='\n') as stream:
        stream.write(f'ncols {size}\nnrows {size}\nxllcorner {bounds[0]:.12f}\n'
                     f'yllcorner {bounds[1]:.12f}\ncellsize {resolution:.12f}\nNODATA_value 0\n')
        np.savetxt(stream, destination, fmt='%d')
    temporary.replace(output)
    output.with_suffix('.prj').write_text(crs.to_wkt() + '\n', encoding='utf-8')
    manifest = dict(schema_version=1, doi='10.5281/zenodo.18788338', source=source,
                    source_file='ESP_4326_ZAFM.tif', source_details=source_details,
                    centre_lon_lat=[longitude, latitude], epsg=32631, bounds=list(bounds),
                    transform=list(dst_transform)[:6], width=size, height=size,
                    cell_size_m=resolution, resampling='nearest', nodata=0,
                    classes={str(int(c)): int(n) for c, n in zip(codes, counts)},
                    valid_cells=int(np.count_nonzero(destination)),
                    combustible_cells=int(np.count_nonzero(destination >= 100)),
                    ascii_sha256=hashlib.sha256(output.read_bytes()).hexdigest(),
                    attribution=ATTRIBUTION,
                    source_attribution=['© ESA WorldCover project 2021 / Contains modified Copernicus Sentinel data (2021) processed by ESA WorldCover consortium',
                                        'FirEUrisk Europe fuel map: Aragoneses, García & Chuvieco (2022), doi:10.21950/YABYCN'],
                    assumptions=dict(fuel=1.0, moisture=0.2, vegetation=1.0, elevation=0.0),
                    limitations=['Fuel categories are mapped estimates, not live measurements.',
                                 'Legend and Scott–Burgan descriptions disagree; codes are preserved without physical calibration.',
                                 'WorldCover baseline 2021; fuel typology baseline 2015–2020.',
                                 'No observed humidity or elevation; simulation steps are not minutes.'],
                    software=dict(rasterio=rasterio.__version__, gdal=rasterio.__gdal_version__, numpy=np.__version__))
    output.with_suffix('.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + '\n')
    print(f'{output}: {size} × {size}, {resolution:g} m, {manifest["combustible_cells"]} combustible cells')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', default=SOURCE, help='remote COG URL or local GeoTIFF')
    parser.add_argument('--output', type=Path, default=Path('data/terrain/collserola.asc'))
    parser.add_argument('--longitude', type=float, default=2.10)
    parser.add_argument('--latitude', type=float, default=41.45)
    parser.add_argument('--size', type=int, default=512)
    parser.add_argument('--resolution', type=float, default=10.0)
    args = parser.parse_args()
    try:
        prepare(args.source, args.output, args.longitude, args.latitude, args.size, args.resolution)
    except (ValueError, OSError, rasterio.errors.RasterioError) as error:
        parser.exit(2, f'Preparation failed: {error}\n')


if __name__ == '__main__':
    main()
