#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Score processed outputs against the clean references.

    python3 metrics.py noisy any24-learn dfn3 ...   # 'noisy' scores the input itself

Writes work/res/<name>.json. Per item:
  sisdr       scale-invariant SDR of the signal part (dB)
  pesq, stoi  wideband PESQ and STOI at 16 kHz (speech only)
  sig/bak/ovrl  DNSMOS P.835 (speech only; skipped with --no-dnsmos)
  lsd         log-spectral distance of the long-term spectrum (music only, dB)
  nr_db       noise reduction in the signal's silent gaps (dB)
  sparkle     log10 kurtosis ratio of the residual against the input noise in
              those gaps (Uemura et al. 2012): 0 means the residual is as
              smooth as the original noise, higher means more "musical noise".
              Gap frames within 170 ms of signal are left out so that speech
              decaying into a gap doesn't count.
"""
import argparse, json, os
from concurrent.futures import ProcessPoolExecutor
import numpy as np, soundfile as sf
from scipy.signal import resample_poly, stft, welch

HERE = os.path.dirname(os.path.abspath(__file__))
SET = os.path.join(HERE, 'work', 'set')
OUT = os.path.join(HERE, 'work', 'out')
RES = os.path.join(HERE, 'work', 'res')
DNSMOS = os.path.join(HERE, 'data', 'dns', 'DNSMOS', 'DNSMOS', 'sig_bak_ovr.onnx')
SR = 48000
_dns = None


def dnsmos(x16):
    global _dns
    if _dns is None:
        import onnxruntime as ort
        _dns = ort.InferenceSession(DNSMOS)
    L = int(9.01 * 16000)
    a = x16
    while len(a) < L:
        a = np.concatenate([a, a])
    sig, bak, ovr = [], [], []
    for i in range(0, int(np.floor(len(a) / 16000) - 9.01) + 1):
        s, b, o = _dns.run(None, {'input_1': a[i * 16000:i * 16000 + L].astype(np.float32)[None]})[0][0]
        sig.append(np.poly1d([-0.08397278, 1.22083953, 0.0052439])(s))
        bak.append(np.poly1d([-0.13166888, 1.60915514, -0.39604546])(b))
        ovr.append(np.poly1d([-0.06766283, 1.11546468, 0.04602535])(o))
    return float(np.mean(sig)), float(np.mean(bak)), float(np.mean(ovr))


def sisdr(ref, est):
    ref = ref - ref.mean(); est = est - est.mean()
    t = np.dot(est, ref) / np.dot(ref, ref) * ref
    return float(10 * np.log10(np.sum(t ** 2) / np.sum((est - t) ** 2)))


def kurt(v):
    v = v.ravel(); m = v.mean()
    return np.mean((v - m) ** 4) / (np.var(v) ** 2 + 1e-30)


def evaluate(proc, it, use_dnsmos=True):
    n0 = it['name']
    c = sf.read(f'{SET}/{n0}_clean.wav')[0]
    n = sf.read(f'{SET}/{n0}_noise.wav')[0]
    y = sf.read(f'{SET}/{n0}_noisy.wav' if proc == 'noisy' else f'{OUT}/{proc}/{n0}.wav')[0]
    if y.ndim > 1:
        y = y.mean(1)
    y = y[:len(c)]
    if len(y) < len(c):
        y = np.pad(y, (0, len(c) - len(y)))
    L = int(1.5 * SR)
    cs, ys = c[L:], y[L:]
    r = {'sisdr': sisdr(cs, ys)}
    hop = 960
    nf = len(c) // hop
    fr = np.sqrt(np.mean(c[:nf * hop].reshape(nf, hop) ** 2, 1) + 1e-20)
    gap = np.where(fr < fr.max() * 10 ** (-55 / 20))[0]
    gap = gap[gap >= int(2.0 * SR) // hop]
    if len(gap) >= 5:
        idx = np.concatenate([np.arange(p * hop, (p + 1) * hop) for p in gap])
        r['nr_db'] = float(10 * np.log10(np.mean(n[idx] ** 2) / (np.mean(y[idx] ** 2) + 1e-20)))
        gs = set(gap.tolist())
        core = np.array([p for p in gap if all(p + d in gs for d in range(-8, 9))])
        if len(core) >= 5:
            def kk(sig):
                f, t, Z = stft(sig, fs=SR, nperseg=1024, noverlap=768)
                P = np.abs(Z) ** 2
                P = P[1:400, np.isin((t * SR / hop).astype(int), core)]  # up to ~18.7 kHz
                return kurt(P / (P.mean(1, keepdims=True) + 1e-30))
            r['sparkle'] = float(np.log10(kk(y) / kk(n)))
    if it['kind'] == 'speech':
        from pesq import pesq
        from pystoi import stoi
        c16, y16 = resample_poly(cs, 1, 3), resample_poly(ys, 1, 3)
        try:
            r['pesq'] = float(pesq(16000, c16, y16, 'wb'))
        except Exception:
            pass
        r['stoi'] = float(stoi(c16, y16, 16000, extended=False))
        if use_dnsmos:
            r['sig'], r['bak'], r['ovrl'] = dnsmos(resample_poly(y, 1, 3))
    else:
        f, Pc = welch(cs, SR, nperseg=4096)
        _, Py = welch(ys, SR, nperseg=4096)
        m = (f > 50) & (f < 16000)
        r['lsd'] = float(np.sqrt(np.mean((10 * np.log10(Py[m] + 1e-20) - 10 * np.log10(Pc[m] + 1e-20)) ** 2)))
    return n0, r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('procs', nargs='+')
    ap.add_argument('--no-dnsmos', action='store_true')
    ap.add_argument('-j', type=int, default=os.cpu_count())
    a = ap.parse_args()
    items = json.load(open(os.path.join(SET, 'items.json')))
    os.makedirs(RES, exist_ok=True)
    for p in a.procs:
        with ProcessPoolExecutor(a.j) as ex:
            res = dict(ex.map(evaluate, [p] * len(items), items, [not a.no_dnsmos] * len(items)))
        json.dump(res, open(os.path.join(RES, p + '.json'), 'w'), indent=0)
        print('scored', p, flush=True)


if __name__ == '__main__':
    main()
