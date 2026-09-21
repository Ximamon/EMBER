#!/usr/bin/env python3
"""Render an exported terrain scenario using only its local result directory."""
import argparse
import csv
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.colors import ListedColormap, BoundaryNorm
from matplotlib.patches import Patch
import numpy as np


def render(directory, scenario=0, output=None):
    directory = Path(directory)
    meta = json.loads((directory / 'run.json').read_text())
    width, height = meta['width'], meta['height']
    codes = np.zeros((height, width), dtype=np.uint16)
    states = np.zeros((height, width), dtype=np.uint8)
    state_ids = {'Unburned': 0, 'Burning': 1, 'Burned': 2, 'Non-combustible': 3}
    with (directory / f'scenario_{scenario:06d}_final.csv').open() as stream:
        count = 0
        for i, row in enumerate(csv.DictReader(stream)):
            y, x = divmod(i, width)
            if y >= height or int(row['x']) != x or int(row['y']) != y:
                raise ValueError('scenario dimensions or row order do not match run.json')
            codes[y, x] = int(row['fuel_code'])
            if int(row['valid']) != int(codes[y, x] != 0):
                raise ValueError('inconsistent NoData mask')
            states[y, x] = state_ids[row['state']]
            count += 1
        if count != width * height:
            raise ValueError('truncated scenario CSV')
    with (directory / 'summary.csv').open() as stream:
        summary = next(r for r in csv.DictReader(stream)
                       if r['record_type'] == 'scenario' and int(r['scenario_id']) == scenario)
    present = np.unique(codes)
    # Labels retain source IDs deliberately: upstream legend and model table disagree.
    labels = {0: 'Sin datos', 91: 'Urbano', 92: 'Suelo desnudo', 93: 'No combustible', 98: 'Agua'}
    colors = {0: '#e5e7eb', 91: '#777e87', 92: '#d6bc93', 93: '#c6c0ae', 98: '#4296ca'}
    colors.update(dict(zip(
        [102, 104, 106, 107, 108, 109, 142, 143, 145, 147, 148, 149, 161, 162, 163, 165, 183],
        ['#c7de83', '#accb5c', '#89ac44', '#728f35', '#586f2d', '#3e5421',
         '#f2d06b', '#dac371', '#ce9651', '#b37645', '#a29550', '#847d46',
         '#4e8c61', '#73a981', '#397552', '#a0bb83', '#689b94'])))
    categorical = np.searchsorted(present, codes)
    cmap = ListedColormap([colors[int(c)] for c in present])
    norm = BoundaryNorm(np.arange(len(present) + 1) - .5, len(present))
    cell = meta['cell_size_m']
    x0, y0 = meta['xllcorner'] / 1000, meta['yllcorner'] / 1000
    extent = [x0, x0 + width * cell / 1000, y0, y0 + height * cell / 1000]
    fig, axes = plt.subplots(1, 2, figsize=(14, 8.5))
    fig.subplots_adjust(left=.07, right=.97, top=.82, bottom=.26, wspace=.17)
    for ax in axes:
        ax.imshow(categorical, cmap=cmap, norm=norm, origin='upper', extent=extent, interpolation='nearest')
        ax.set_xlabel('Este UTM (km)'); ax.set_ylabel('Norte UTM (km)')
        ax.ticklabel_format(useOffset=False, style='plain')
        for x, y in meta['ignitions']:
            ax.plot(x0 + (x + .5) * cell / 1000,
                    extent[3] - (y + .5) * cell / 1000, '*', color='#ffeb3b',
                    markeredgecolor='black', markersize=12)
        length = min(1.0, width * cell / 1000 / 4)
        bx, by = x0 + .07 * (extent[1] - x0), y0 + .08 * (extent[3] - y0)
        ax.plot([bx, bx + length], [by, by], color='black', linewidth=4)
        ax.text(bx, by + .03 * (extent[3] - y0), f'{length:g} km', fontsize=9,
                bbox=dict(facecolor='white', edgecolor='none', alpha=.85))
    overlay = np.zeros((height, width, 4))
    overlay[states == 2] = matplotlib.colors.to_rgba('#171b22')
    overlay[states == 1] = matplotlib.colors.to_rgba('#ff4d18')
    axes[1].imshow(overlay, origin='upper', extent=extent, interpolation='nearest')
    axes[0].set_title('Mapa de clases · códigos ZAFM conservados', fontsize=11)
    axes[1].set_title(f'Escenario {scenario} · {summary["steps_executed"]} iteraciones\n'
                      f'{float(summary["burned_hectares"]):.2f} ha afectadas · '
                      f'{float(summary["combustible_burned_percent"]):.2f}% del combustible', fontsize=11)
    fig.suptitle('EMBER | Propagación sobre terreno real', x=.07, ha='left', y=.96, fontsize=22, weight='bold')
    fig.text(.07, .905, f'EPSG:{meta["epsg"]} · celdas de {cell:g} m · '
             f'combustible uniforme {meta["fuel"]:.2g} · humedad uniforme {meta["moisture"]:.2g}', fontsize=11)
    fig.text(.07, .865, 'Modelo estocástico sin calibrar · relieve plano · las iteraciones no representan minutos', fontsize=10)
    handles = [Patch(color=colors[int(c)], label=f'{c} · {labels[int(c)]}' if int(c) in labels else f'ZAFM {c}') for c in present]
    handles += [Patch(color='#171b22', label='Quemado'), Patch(color='#ff4d18', label='Ardiendo'),
                plt.Line2D([], [], marker='*', color='#ffeb3b', markeredgecolor='black', linestyle='', label='Ignición')]
    fig.legend(handles=handles, loc='lower center', bbox_to_anchor=(.5, .09), ncol=6, frameon=False, fontsize=9)
    fig.text(.07, .06, 'Sánchez et al. · ZAFM Europe v1.0 · doi:10.5281/zenodo.18788338 · © Paula Sánchez 2025 · CC BY 4.0', fontsize=8)
    fig.text(.07, .035, 'Fuentes: ESA WorldCover 2021 / Copernicus Sentinel · FirEUrisk Europe. Las clases son estimaciones cartográficas.', fontsize=8)
    output = Path(output) if output else directory / f'scenario_{scenario:06d}.png'
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=160, facecolor='white')
    plt.close(fig)
    print(output)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--scenario', type=int, default=0)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        render(args.directory, args.scenario, args.output)
    except (ValueError, OSError, KeyError, StopIteration) as error:
        parser.exit(2, f'Rendering failed: {error}\n')


if __name__ == '__main__':
    main()
