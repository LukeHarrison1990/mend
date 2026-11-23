#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build the benchmark set in work/set from the recordings in data/.

138 items at 48 kHz. Speech: 4 utterances (one whispered) x 10 noises x SNR
0/5/15 dB. Music: 3 clips x 3 noises x SNR 10/20 dB. Each item starts with
1.5 s of noise alone (for the learned profile) and has silent gaps in the
signal, where the residual noise and its artefacts are measured. For each
item we keep the clean signal, the scaled noise and their sum.
Deterministic: the same data gives the same set.
"""
import json, os
import numpy as np, soundfile as sf
from scipy.signal import resample_poly, butter, sosfilt

HERE = os.path.dirname(os.path.abspath(__file__))
D = os.path.join(HERE, 'data')
OUT = os.path.join(HERE, 'work', 'set')
SR = 48000
rng = np.random.default_rng(7)

ESC = {'rain': '3-157149-A-10', 'vacuum_cleaner': '5-188365-A-36', 'engine': '1-50661-A-44',
       'crackling_fire': '5-186924-A-12', 'keyboard_typing': '2-118817-A-32', 'wind': '1-51036-A-16'}
SPEECH = ['p001/rainbow_03_regular', 'p002/sentences_16_regular', 'p001/sentences_21_regular',
          'p002/rainbow_02_whisper']
BABBLE = ['p001/emo_' + e + '_sentences' for e in ['adoration', 'confusion', 'cuteness', 'distress', 'fear', 'guilt']]
MUSIC = ['sorohanro_-_solo-trumpet-06.hq.ogg',
         'Hungarian_Dance_number_5_-_Allegro_in_F_sharp_minor_(string_orchestra).hq.ogg',
         'Karissa_Hobbs_-_Lets_Go_Fishin.hq.ogg']


def load48(f):
    x, sr = sf.read(f, always_2d=True)
    x = x.mean(1)
    if sr != SR:
        from math import gcd
        g = gcd(SR, sr)
        x = resample_poly(x, SR // g, sr // g)
    return x.astype(np.float64)


def rms(x): return np.sqrt(np.mean(x ** 2) + 1e-20)


def loop_to(n, x):  # crossfaded looping to length n
    if len(x) >= n:
        return x[:n]
    xf = int(0.25 * SR)
    out = x.copy()
    while len(out) < n:
        w = np.linspace(0, 1, xf)
        out = np.concatenate([out[:-xf], out[-xf:] * (1 - w) + x[:xf] * w, x[xf:]])
    return out[:n]


N = int(14.0 * SR)


def pink(n):
    f = np.fft.rfft(rng.standard_normal(n)); k = np.arange(len(f)); k[0] = 1
    return np.fft.irfft(f / np.sqrt(k), n)


def hvac(n):
    f = np.fft.rfft(rng.standard_normal(n)); fr = np.fft.rfftfreq(n, 1 / SR)
    x = np.fft.irfft(f / (1 + fr / 150.0), n); x /= rms(x)
    t = np.arange(n) / SR
    return x + 0.15 * np.sin(2 * np.pi * 119.7 * t) + 0.08 * np.sin(2 * np.pi * 239.4 * t)


def hiss(n):
    w = rng.standard_normal(n)
    return 0.3 * w + sosfilt(butter(2, 3000, 'highpass', fs=SR, output='sos'), w)


def main():
    os.makedirs(OUT, exist_ok=True)
    noises = {'pink': pink(N), 'hvac': hvac(N), 'hiss': hiss(N)}
    for cat, f in ESC.items():
        noises[cat] = loop_to(N, load48(f'{D}/esc/audio/{f}.wav'))
    bab = np.zeros(N)
    for f in BABBLE:
        x = load48(f'{D}/ears/{f}.wav')
        bab += np.roll(loop_to(N, x / rms(x)), rng.integers(N))
    noises['babble'] = bab
    for k in noises:
        noises[k] = noises[k] / rms(noises[k])

    def write(name, clean, nz, **meta):
        noisy = clean + nz
        peak = np.max(np.abs(noisy)); sc = 0.9 / peak if peak > 0.9 else 1.0
        for tag, x in (("clean", clean), ("noise", nz), ("noisy", noisy)):
            sf.write(f'{OUT}/{name}_{tag}.wav', (x * sc).astype(np.float32), SR, subtype='FLOAT')
        items.append(dict(name=name, **meta))

    LEAD, SEG = int(1.5 * SR), int(8.0 * SR)
    items = []
    for si, f in enumerate(SPEECH):
        s = load48(f'{D}/ears/{f}.wav')[int(0.3 * SR):int(0.3 * SR) + SEG]
        fade = np.ones(len(s)); h = int(0.03 * SR); mid = len(s) // 2
        fade[mid - h:mid] = np.linspace(1, 0, h); fade[mid:mid + h] = np.linspace(0, 1, h); fade[-h:] = np.linspace(1, 0, h)
        s = s * fade
        s = np.concatenate([s[:mid], np.zeros(int(1.2 * SR)), s[mid:], np.zeros(int(1.0 * SR))])
        s = s / rms(s) * 0.05  # about -26 dBFS RMS
        clean = np.concatenate([np.zeros(LEAD), s])
        for nk, nz in noises.items():
            off = rng.integers(0, N - len(clean)) if N > len(clean) else 0
            nseg = nz[off:off + len(clean)]
            for snr in [0, 5, 15]:
                g = rms(s[np.abs(s) > 0]) / rms(nseg[LEAD:]) / 10 ** (snr / 20)
                write(f'sp{si}_{nk}_{snr}', clean, g * nseg, kind='speech', speech=si, noise=nk, snr=snr)
    for mi, f in enumerate(MUSIC):
        m = load48(f'{D}/librosa/audio/{f}')[SR:SR + SEG]
        m = m / rms(m) * 0.05
        clean = np.concatenate([np.zeros(LEAD), m])
        for nk in ['pink', 'hiss', 'rain']:
            nseg = noises[nk][:len(clean)]
            for snr in [10, 20]:
                g = rms(m) / rms(nseg[LEAD:]) / 10 ** (snr / 20)
                write(f'mu{mi}_{nk}_{snr}', clean, g * nseg, kind='music', speech=mi, noise=nk, snr=snr)
    json.dump(items, open(f'{OUT}/items.json', 'w'), indent=0)
    print(len(items), 'items in', OUT)


if __name__ == '__main__':
    main()
