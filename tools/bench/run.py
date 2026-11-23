#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Process the benchmark set with one or more configurations.

    python3 run.py any24-learn any24 voice           # named presets below
    python3 run.py mine="--reduction 30 --smoothing 75 --learn 1.2"
    python3 run.py --deep-filter /path/to/deep-filter dfn3

Output goes to work/out/<name>/<item>.wav. --bin points at mend-denoise
(default: ../../build/tools/bench/mend-denoise). The dfn3 preset runs the
reference DeepFilterNet binary, for comparison.
"""
import argparse, json, os, shlex, shutil, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
SET = os.path.join(HERE, 'work', 'set')
OUT = os.path.join(HERE, 'work', 'out')

PRESETS = {
    'any12': '--reduction 12',
    'any24': '--reduction 24',
    'any24-learn': '--reduction 24 --learn 1.2',
    'any24-learn-s0': '--reduction 24 --learn 1.2 --smoothing 0',
    'any24-learn-s75': '--reduction 24 --learn 1.2 --smoothing 75',
    'any24-learn-s100': '--reduction 24 --learn 1.2 --smoothing 100',
    'any40-learn': '--reduction 40 --learn 1.2',
    'voice': '--mode voice',
    'voice24': '--mode voice --voice-reduction 24',
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('configs', nargs='+')
    ap.add_argument('--bin', default=os.path.join(HERE, '..', '..', 'build', 'tools', 'bench', 'mend-denoise'))
    ap.add_argument('--deep-filter', help='path to the DeepFilterNet "deep-filter" binary (for the dfn3 preset)')
    ap.add_argument('-j', type=int, default=os.cpu_count())
    a = ap.parse_args()
    items = json.load(open(os.path.join(SET, 'items.json')))
    for c in a.configs:
        name, _, args = c.partition('=')
        d = os.path.join(OUT, name)
        os.makedirs(d, exist_ok=True)
        if name.startswith('dfn3'):
            if not a.deep_filter:
                sys.exit('dfn3 needs --deep-filter')
            extra = shlex.split(args) if args else (['-a', '24'] if name == 'dfn3_a24' else [])
            with tempfile.TemporaryDirectory() as t:
                subprocess.run([a.deep_filter, '-D', '-o', t] + extra
                               + [os.path.join(SET, i['name'] + '_noisy.wav') for i in items],
                               check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                for i in items:
                    shutil.move(os.path.join(t, i['name'] + '_noisy.wav'), os.path.join(d, i['name'] + '.wav'))
        else:
            args = shlex.split(args or PRESETS[name])
            def one(i):
                subprocess.run([a.bin, os.path.join(SET, i['name'] + '_noisy.wav'), os.path.join(d, i['name'] + '.wav')]
                               + args, check=True)
            with ThreadPoolExecutor(a.j) as ex:
                list(ex.map(one, items))
        json.dump({'args': args}, open(os.path.join(d, 'config.json'), 'w'))
        print('processed', name, flush=True)


if __name__ == '__main__':
    main()
