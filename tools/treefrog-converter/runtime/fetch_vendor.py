#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
"""Fetch small public, pinned API/parser sources; no SDK or game payload."""
import hashlib
import json
from pathlib import Path
import urllib.request

HERE = Path(__file__).resolve().parent
SOURCES = {
    'libretro.h': ('libretro/libretro-common', 'include/libretro.h'),
    'jsmn.h': ('zserge/jsmn', 'jsmn.h'),
    'jsmn-LICENSE': ('zserge/jsmn', 'LICENSE'),
}

def fetch():
    out = HERE / 'vendor'
    out.mkdir(exist_ok=True)
    lock = out / 'sources.json'
    old = json.loads(lock.read_text()) if lock.exists() else {}
    records = {}
    for name, (repo, path) in SOURCES.items():
        if name in old:
            commit = old[name]['commit']
        else:
            req = urllib.request.Request('https://api.github.com/repos/' + repo + '/commits?per_page=1', headers={'User-Agent': 'lilretro-source-fetch'})
            commit = json.load(urllib.request.urlopen(req))[0]['sha']
        url = 'https://raw.githubusercontent.com/' + repo + '/' + commit + '/' + path
        data = urllib.request.urlopen(url).read()
        digest = hashlib.sha256(data).hexdigest()
        if name in old and digest != old[name]['sha256']:
            raise ValueError('Pinned source digest changed: ' + name)
        (out / name).write_bytes(data)
        records[name] = dict(url=url, commit=commit, sha256=digest, bytes=len(data))
    lock.write_text(json.dumps(records, indent=2) + '\n')
    print(json.dumps(records, indent=2))

if __name__ == '__main__':
    fetch()
