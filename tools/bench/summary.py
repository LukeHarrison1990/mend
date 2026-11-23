#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Print averages for scored configurations.

    python3 summary.py noisy any24-learn dfn3 [--by noise|snr] [--markdown]
"""
import argparse, json, os
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SET = os.path.join(HERE, 'work', 'set')
RES = os.path.join(HERE, 'work', 'res')
SPEECH = [('sisdr', 'SI-SDR', 1), ('pesq', 'PESQ', 2), ('stoi', 'STOI', 3), ('sig', 'SIG', 2), ('bak', 'BAK', 2),
          ('ovrl', 'OVRL', 2), ('nr_db', 'NR dB', 1), ('sparkle', 'Sparkle', 2)]
MUSIC = [('sisdr', 'SI-SDR', 1), ('lsd', 'LSD', 2), ('nr_db', 'NR dB', 1), ('sparkle', 'Sparkle', 2)]


def mean(res, names, key):
    v = [res[n][key] for n in names if key in res[n] and np.isfinite(res[n][key])]
    return np.mean(v) if v else np.nan


def table(title, cols, rows, md):
    head = ['config'] + [c[1] for c in cols]
    print(f'\n{title}')
    if md:
        print('| ' + ' | '.join(head) + ' |\n|' + '---|' * len(head))
    else:
        print('  '.join(f'{h:>16s}' if i == 0 else f'{h:>7s}' for i, h in enumerate(head)))
    for name, vals in rows:
        cells = [('' if np.isnan(v) else f'{v:.{c[2]}f}') for v, c in zip(vals, cols)]
        print('| ' + ' | '.join([name] + cells) + ' |' if md else
              '  '.join([f'{name:>16s}'] + [f'{x:>7s}' for x in cells]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('procs', nargs='+')
    ap.add_argument('--by', choices=['noise', 'snr'])
    ap.add_argument('--markdown', action='store_true')
    a = ap.parse_args()
    items = {i['name']: i for i in json.load(open(os.path.join(SET, 'items.json')))}
    res = {p: json.load(open(os.path.join(RES, p + '.json'))) for p in a.procs}
    for kind, cols in (('speech', SPEECH), ('music', MUSIC)):
        groups = sorted({items[n][a.by] for n in items if items[n]['kind'] == kind}) if a.by else [None]
        for g in groups:
            names = [n for n in items if items[n]['kind'] == kind and (g is None or items[n][a.by] == g)]
            rows = [(p, [mean(res[p], names, k) for k, _, _ in cols]) for p in a.procs]
            table(f'{kind.upper()}' + (f'  {a.by}={g}' if g is not None else '') + f'  ({len(names)} items)', cols,
                  rows, a.markdown)


if __name__ == '__main__':
    main()
