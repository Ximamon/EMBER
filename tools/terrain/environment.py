#!/usr/bin/env python3
"""Prepare aligned metric elevation and a fixed ERA5 weather snapshot."""
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import shutil
from urllib.parse import urlencode
from urllib.request import urlopen

import numpy as np
import rasterio
from rasterio.warp import reproject, Resampling, transform_bounds

WEATHER_UNITS = {'wind_speed_10m': 'm/s', 'wind_direction_10m': '°',
                 'temperature_2m': '°C', 'relative_humidity_2m': '%', 'precipitation': 'mm'}
DEM_ATTRIBUTION = ('Copernicus DEM GLO-30 (2021 release). © DLR e.V. 2010-2014; '
                   '© Airbus Defence and Space GmbH 2014-2018. '
                   'Provided under COPERNICUS by the European Union and ESA; all rights reserved. '
                   'https://registry.opendata.aws/copernicus-dem/')


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + '\n',
                          encoding='utf-8')


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def parse_time(text):
    value = datetime.strptime(text, '%Y-%m-%dT%H:%MZ').replace(tzinfo=timezone.utc)
    if value.minute or value.strftime('%Y-%m-%dT%H:%MZ') != text:
        raise ValueError('time must be an exact UTC hour: YYYY-MM-DDTHH:00Z')
    if value > datetime.now(timezone.utc):
        raise ValueError('ERA5 requires a past date; future weather is not available')
    return value


def select_weather(raw, timestamp):
    """Validate units and select exactly one hour; never substitute missing values."""
    requested = parse_time(timestamp).strftime('%Y-%m-%dT%H:%M')
    if raw.get('utc_offset_seconds') != 0:
        raise ValueError('weather response must use UTC')
    hourly = raw['hourly']
    if hourly['time'].count(requested) != 1:
        raise ValueError('requested weather hour is unavailable or duplicated')
    index = hourly['time'].index(requested)
    selected = {}
    for variable, unit in WEATHER_UNITS.items():
        if raw['hourly_units'].get(variable) != unit:
            raise ValueError(f'unexpected weather unit for {variable}; expected {unit}')
        if len(hourly[variable]) != len(hourly['time']):
            raise ValueError(f'inconsistent weather series: {variable}')
        value = hourly[variable][index]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
            raise ValueError(f'missing or invalid weather value: {variable}')
        selected[variable] = value
    if (selected['wind_speed_10m'] < 0 or not 0 <= selected['wind_direction_10m'] <= 360 or
            not 0 <= selected['relative_humidity_2m'] <= 100 or selected['precipitation'] < 0):
        raise ValueError('weather values outside supported bounds')
    for key, bound in [('latitude', 90), ('longitude', 180)]:
        if not isinstance(raw[key], (int, float)) or not math.isfinite(raw[key]) or abs(raw[key]) > bound:
            raise ValueError('invalid returned weather coordinates')
    return selected


def prepare_weather(directory, longitude, latitude, timestamp):
    date = parse_time(timestamp).strftime('%Y-%m-%d')
    query = dict(latitude=latitude, longitude=longitude, start_date=date, end_date=date,
                 hourly=','.join(WEATHER_UNITS), models='era5', timezone='UTC',
                 wind_speed_unit='ms', temperature_unit='celsius', precipitation_unit='mm')
    url = 'https://archive-api.open-meteo.com/v1/archive?' + urlencode(query)
    with urlopen(url, timeout=45) as response:
        content = response.read(4 * 1024 * 1024 + 1)
    if len(content) > 4 * 1024 * 1024:
        raise ValueError('weather response exceeds size limit')
    raw = json.loads(content)
    values = select_weather(raw, timestamp)
    (directory / 'weather-response.json').write_bytes(content)
    weather = dict(schema_version=1, timestamp_utc=timestamp, model='era5', request_url=url,
                   requested_coordinates=dict(longitude=longitude, latitude=latitude),
                   returned_coordinates={key: raw[key] for key in ('longitude', 'latitude')},
                   downloaded_at=utc_now(), units=WEATHER_UNITS, values=values,
                   attribution='ERA5 (Copernicus Climate Change Service / ECMWF), via Open-Meteo; CC BY 4.0. '
                               'https://open-meteo.com/en/docs/historical-weather-api',
                   assumptions=['Reanalysis, not local direct observations.',
                                'One central weather cell, constant throughout the simulation.',
                                'Air humidity is not fuel moisture; steps are not minutes.'])
    write_json(directory / 'weather.json', weather)
    return weather


def wind_parameters(speed, direction, reference):
    if not all(math.isfinite(v) for v in (speed, direction, reference)) or reference <= 0 or speed < 0:
        raise ValueError('wind reference must be positive and finite; speed must be non-negative')
    if not 0 <= direction <= 360:
        raise ValueError('wind direction must be in [0,360]')
    # Meteorological north-clockwise FROM -> mathematical east-counterclockwise TOWARD.
    return dict(wind_direction_degrees=(270 - direction) % 360,
                wind_strength=min(speed / reference, 1.0), reference_m_s=reference,
                clipped=speed > reference,
                interpretation='Heuristic normalization, not physical calibration.')


def copernicus_tiles(bounds):
    west, south, east, north = bounds
    tiles = []
    for lat in range(math.floor(south), math.ceil(north)):
        for lon in range(math.floor(west), math.ceil(east)):
            name = (f'Copernicus_DSM_COG_10_{"N" if lat >= 0 else "S"}{abs(lat):02d}_00_'
                    f'{"E" if lon >= 0 else "W"}{abs(lon):03d}_00_DEM')
            tiles.append(f'https://copernicus-dem-30m.s3.eu-central-1.amazonaws.com/{name}/{name}.tif')
    if not tiles or len(tiles) > 16:
        raise ValueError('elevation crop requires more than 16 tiles; prepare a smaller region')
    return tiles


def prepare_elevation(terrain_path, output, source=None):
    """Reproject only intersecting DEM tiles; preserve metric heights and valid-domain holes."""
    with rasterio.open(terrain_path) as terrain:
        if terrain.crs is None or terrain.crs.to_epsg() not in (32628, 32629, 32630, 32631, 3035) or terrain.count != 1:
            raise ValueError('terrain must use WGS84 UTM 28N-31N or ETRS89 LAEA Europe')
        if (terrain.width * terrain.height > 16000000 or terrain.transform.a <= 0 or
                terrain.transform.e != -terrain.transform.a or terrain.transform.b or terrain.transform.d):
            raise ValueError('terrain must have bounded square north-up cells')
        valid = terrain.read(1) != 0
        if not np.any(valid):
            raise ValueError('terrain has no valid cells')
        bounds = transform_bounds(terrain.crs, 'EPSG:4326', *terrain.bounds, densify_pts=21)
        sources = [str(Path(source).resolve())] if source else copernicus_tiles(bounds)
        heights = np.full(terrain.shape, np.nan, dtype=np.float32)
        details = []
        for number, location in enumerate(sources):
            local = Path(location) if source else output.parent / f'dem-source-{number}.tif'
            if not source:
                with urlopen(location, timeout=45) as response, local.open('wb') as stream:
                    shutil.copyfileobj(response, stream)
            with rasterio.open(local) as dem:
                if dem.crs is None or dem.count != 1 or dem.scales[0] != 1 or dem.offsets[0] != 0:
                    raise ValueError('DEM must have one georeferenced band of unscaled metric heights')
                if dem.units[0] not in (None, 'm', 'metre', 'meter', 'metres', 'meters'):
                    raise ValueError('DEM heights must use metres')
                projected = np.full(terrain.shape, np.nan, dtype=np.float32)
                reproject(rasterio.band(dem, 1), projected, src_transform=dem.transform,
                          src_crs=dem.crs, src_nodata=dem.nodata,
                          dst_transform=terrain.transform, dst_crs=terrain.crs,
                          dst_nodata=np.nan, resampling=Resampling.bilinear)
                take = np.isfinite(projected)
                heights[take] = projected[take]
                details.append(dict(source=location, sha256=sha256(local), crs=str(dem.crs),
                                    resolution=list(dem.res), units='metres'))
            if not source:
                local.unlink() # Derived raster and source hash are sufficient for offline replay.
        if not np.all(np.isfinite(heights[valid])):
            raise ValueError('missing elevation on valid terrain; no zero-height fallback')
        heights[~valid] = -9999
        with output.open('w', encoding='ascii', newline='\n') as stream:
            stream.write(f'ncols {terrain.width}\nnrows {terrain.height}\n'
                         f'xllcorner {terrain.bounds.left:.12f}\nyllcorner {terrain.bounds.bottom:.12f}\n'
                         f'cellsize {terrain.transform.a:.12f}\nNODATA_value -9999\n')
            np.savetxt(stream, heights, fmt='%.9g')
        output.with_suffix('.prj').write_text(terrain.crs.to_wkt() + '\n', encoding='utf-8')
    manifest = dict(schema_version=1, sources=details, prepared_at=utc_now(), resampling='bilinear',
                    units='metres', attribution=DEM_ATTRIBUTION if not source else 'User-supplied metric DEM; retain source licence.',
                    limitations=['GLO-30 is a surface model including vegetation and structures.',
                                 'Resampling to 10 m does not add detail to a 30 m source.'])
    write_json(output.with_suffix('.json'), manifest)
    return manifest
