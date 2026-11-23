#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Download the source recordings the benchmark is built from, into ./data.

Nothing downloaded here is part of Mend or may be redistributed with it: the
EARS speech and ESC-50 noise recordings are CC BY-NC 4.0, and the librosa
example clips and the DNSMOS model have their own terms (see README.md). They
are fetched for local evaluation only and are ignored by git.

Only the files the benchmark uses are fetched: EARS speakers come as large zip
archives, so we read just the members we need with HTTP range requests.
"""
import io, os, subprocess, sys, urllib.request, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, 'data')

EARS_URL = 'https://github.com/facebookresearch/ears_dataset/releases/download/dataset/{}.zip'
EARS_FILES = {
    'p001': ['rainbow_03_regular', 'sentences_21_regular', 'emo_adoration_sentences', 'emo_confusion_sentences',
             'emo_cuteness_sentences', 'emo_distress_sentences', 'emo_fear_sentences', 'emo_guilt_sentences'],
    'p002': ['sentences_16_regular', 'rainbow_02_whisper'],
}
REPOS = [  # (folder, url, commit, paths to check out)
    ('esc', 'https://github.com/karolpiczak/ESC-50.git', '33c8ce9eb2cf0b1c2f8bcf322eb349b6be34dbb6',
     ['audio/' + f for f in ['3-157149-A-10.wav', '5-188365-A-36.wav', '1-50661-A-44.wav', '5-186924-A-12.wav',
                             '2-118817-A-32.wav', '1-51036-A-16.wav']]),
    ('librosa', 'https://github.com/librosa/data.git', '5285c94e33c74bbe97501f5c9fa95e5e2ce55d48',
     ['audio/sorohanro_-_solo-trumpet-06.hq.ogg',
      'audio/Hungarian_Dance_number_5_-_Allegro_in_F_sharp_minor_(string_orchestra).hq.ogg',
      'audio/Karissa_Hobbs_-_Lets_Go_Fishin.hq.ogg']),
    ('dns', 'https://github.com/microsoft/DNS-Challenge.git', '785ab12275d5cf9341c5e946b0eee9f8825ece3b',
     ['DNSMOS/DNSMOS/sig_bak_ovr.onnx']),
]


class HttpRangeFile(io.RawIOBase):
    """A seekable read-only file over HTTP, so zipfile can read single members."""

    def __init__(self, url):
        req = urllib.request.Request(url, method='HEAD')
        with urllib.request.urlopen(req) as r:
            self.url = r.geturl()  # follow the release redirect once
            self.size = int(r.headers['Content-Length'])
        self.pos = 0

    def seekable(self): return True
    def readable(self): return True
    def tell(self): return self.pos

    def seek(self, off, whence=0):
        self.pos = {0: off, 1: self.pos + off, 2: self.size + off}[whence]
        return self.pos

    def readinto(self, b):
        if self.pos >= self.size:
            return 0
        end = min(self.size, self.pos + len(b)) - 1
        req = urllib.request.Request(self.url, headers={'Range': f'bytes={self.pos}-{end}'})
        with urllib.request.urlopen(req) as r:
            data = r.read()
        b[:len(data)] = data
        self.pos += len(data)
        return len(data)


def fetch_ears():
    for spk, names in EARS_FILES.items():
        out = os.path.join(DATA, 'ears', spk)
        need = [n for n in names if not os.path.exists(os.path.join(out, n + '.wav'))]
        if not need:
            continue
        print(f'EARS {spk}: {len(need)} files', flush=True)
        os.makedirs(out, exist_ok=True)
        f = io.BufferedReader(HttpRangeFile(EARS_URL.format(spk)), buffer_size=1 << 20)
        with zipfile.ZipFile(f) as z:
            members = {os.path.splitext(os.path.basename(m))[0]: m for m in z.namelist() if m.endswith('.wav')}
            for n in need:
                with z.open(members[n]) as src, open(os.path.join(out, n + '.wav'), 'wb') as dst:
                    dst.write(src.read())


def fetch_repos():
    for folder, url, commit, paths in REPOS:
        d = os.path.join(DATA, folder)
        if all(os.path.exists(os.path.join(d, p)) for p in paths):
            continue
        print(f'{folder}: {url}', flush=True)
        if not os.path.isdir(os.path.join(d, '.git')):
            subprocess.run(['git', 'clone', '-q', '--filter=blob:none', '--no-checkout', url, d], check=True)
        subprocess.run(['git', '-C', d, 'sparse-checkout', 'set', '--no-cone'] + paths, check=True)
        subprocess.run(['git', '-C', d, '-c', 'advice.detachedHead=false', 'checkout', '-q', commit], check=True)


if __name__ == '__main__':
    os.makedirs(DATA, exist_ok=True)
    fetch_repos()
    fetch_ears()
    print('data ready in', DATA)
