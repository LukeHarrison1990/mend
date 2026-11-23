# De-noise benchmark

Scripts to measure Mend's De-noise objectively, so changes to it can be
judged by numbers as well as by ear. They build a fixed test set of speech
and music mixed with ten kinds of noise, run it through `mend-denoise`
(a command-line wrapper for the plugin's engine, built with Mend), and
score the results.

## Running it

Build Mend first (`mend-denoise` is built by default; turn it off with
`-DMEND_BUILD_TOOLS=OFF`). Then, with Python 3.9 or newer:

```
cd tools/bench
pip install -r requirements.txt
python3 fetch_data.py        # about 65 MB, into data/
python3 make_set.py          # 138 items, into work/set/
python3 run.py any24-learn any24 voice
python3 metrics.py noisy any24-learn any24 voice
python3 summary.py noisy any24-learn any24 voice
```

`run.py` takes the named presets listed at its top, or your own settings:
`python3 run.py mine="--reduction 30 --smoothing 75 --learn 1.2"`. Pass
`--bin` if your build folder isn't `../../build`. To compare with the
reference DeepFilterNet binary, download `deep-filter` from its releases and
run `python3 run.py --deep-filter path/to/deep-filter dfn3`.
`metrics.py --no-dnsmos` skips the slowest metric while you iterate;
`summary.py --by noise` (or `--by snr`) breaks results down.

## The test set

- **Speech**: four utterances from the EARS dataset (two speakers, one
  whispered), each 8 s with a 1.2 s gap in the middle and 1 s at the end.
- **Noise**: pink noise, HVAC rumble with mains hum, hiss (synthetic);
  rain, vacuum cleaner, engine, crackling fire, keyboard typing and wind
  (ESC-50); babble (six other EARS recordings). Mixed at 0, 5 and 15 dB SNR.
- **Music**: three clips (trumpet, string orchestra, a song) with pink noise,
  hiss and rain at 10 and 20 dB SNR.

Every item starts with 1.5 s of noise alone, which the `-learn` presets use
to learn the noise profile.

## Metrics

| Metric | Measures | Better |
|---|---|---|
| SI-SDR | how close the output is to the clean signal (dB) | higher |
| PESQ, STOI | speech quality and intelligibility against the clean speech | higher |
| SIG, BAK, OVRL | DNSMOS P.835: predicted listener ratings of the speech, the background and overall, 1-5 | higher |
| LSD | music: difference between the long-term spectra (dB) | lower |
| NR dB | how far the noise is turned down in the signal's gaps | higher |
| Sparkle | musical noise: how much spikier the residual noise is than the original, as a log kurtosis ratio (Uemura et al.) | nearer 0 |

## Results (Mend 0.10.0)

Averages over the set, all at 24 dB reduction; `-learn` presets learn the
noise from the lead-in, the others use Adaptive. "0.9" is the previous Any
sound engine.

Speech (120 items):

| config | SI-SDR | PESQ | STOI | BAK | OVRL | NR dB | Sparkle |
|---|---|---|---|---|---|---|---|
| noisy input | 5.6 | 1.29 | 0.794 | 2.07 | 1.95 | 0.0 | 0.00 |
| 0.9, learned | 12.5 | 1.83 | 0.802 | 2.95 | 2.38 | 17.5 | 0.39 |
| Any sound, learned, smoothing 0 | 12.2 | 1.87 | 0.804 | 3.11 | 2.37 | 19.3 | 0.45 |
| Any sound, learned (default smoothing 50) | 11.5 | 1.83 | 0.796 | 3.18 | 2.39 | 20.6 | 0.34 |
| Any sound, learned, smoothing 75 | 10.8 | 1.79 | 0.789 | 3.18 | 2.40 | 21.2 | 0.22 |
| Any sound, learned, smoothing 100 | 9.2 | 1.65 | 0.766 | 3.00 | 2.39 | 18.3 | 0.03 |
| 0.9, adaptive | 7.9 | 1.68 | 0.798 | 2.66 | 2.25 | 16.8 | 0.36 |
| Any sound, adaptive | 8.1 | 1.70 | 0.798 | 2.78 | 2.27 | 17.7 | 0.33 |
| Voice, Full | 9.9 | 2.37 | 0.829 | 3.85 | 2.74 | 33.5 | 0.48 |

Music (18 items):

| config | SI-SDR | LSD | NR dB | Sparkle |
|---|---|---|---|---|
| noisy input | 15.1 | 15.17 | 0.0 | 0.01 |
| 0.9, learned | 22.6 | 7.72 | 20.1 | 0.46 |
| Any sound, learned | 21.9 | 8.74 | 23.4 | 0.31 |
| Any sound, learned, smoothing 100 | 19.9 | 9.58 | 18.9 | 0.08 |
| 0.9, adaptive | 14.8 | 9.32 | 17.4 | 0.46 |
| Any sound, adaptive | 14.3 | 9.50 | 20.3 | 0.39 |
| Voice, Full | -12.6 | 29.70 | 35.1 | 0.11 |

Voice mode is for speech only: on music it removes the music too.

## Data and licences

The scripts are MIT, like Mend. The recordings they download are not part
of Mend and must not be committed or redistributed with it; `data/` and
`work/` are ignored by git.

- EARS (Richter et al., 2024): CC BY-NC 4.0.
- ESC-50 (Piczak, 2015): the clips used here are CC BY-NC 3.0.
- librosa example data: each clip under its own Creative Commons licence,
  listed in that repository.
- DNSMOS model (Microsoft DNS Challenge repository, under its licence): used only to score.
