"""Stage optional cores and their source notices, never a physical card."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

PROFILES = {
    'nxengine': ('NXEngine', 'Cave Story', 'exe'),
    'prboom': ('PrBoom', 'Doom', 'wad'),
    'stella2014': ('Stella 2014', 'Atari 2600', 'a26|bin|rom'),
    'prosystem': ('ProSystem', 'Atari 7800', 'a78|bin'),
    'atari800': ('Atari800', 'Atari 800 / 5200', 'atr|xfd|xex|car|rom|a52|bin|cas'),
    'race': ('RACE', 'Neo Geo Pocket', 'ngp|ngc|ngpc'),
    'pokemini': ('PokeMini', 'Pokemon Mini', 'min'),
    'wswan': ('Beetle WonderSwan', 'WonderSwan', 'ws|wsc'),
    'picodrive': ('PicoDrive', 'Mega Drive / Master System / Game Gear', 'md|gen|smd|bin|sms|gg'),
    'quicknes': ('QuickNES', 'NES', 'nes'),
    'snes9x2002': ('Snes9x 2002', 'SNES', 'sfc|smc|fig|bs'),
    'gpsp': ('gpSP', 'Game Boy Advance', 'gba|agb'),
    'pcsx_rearmed': ('PCSX ReARMed', 'Sony PlayStation', 'cue|bin|img|iso|pbp|m3u|exe'),
}


def stage(built, sources, lock, output):
    if output.resolve() in (built.resolve(), sources.resolve()):
        raise ValueError('output must be separate from builds and sources')
    files = []
    for line in lock.read_text().splitlines():
        if not line or line.startswith('#'):
            continue
        core, repository, commit, _, _, filename = line.split('|')
        name, system, extensions = PROFILES[core]
        binary = built / filename
        provenance = built / (filename + '.build-info')
        if not binary.is_file() or not provenance.is_file():
            raise ValueError('missing built/provenance files for ' + core)
        digest = hashlib.sha256(binary.read_bytes()).hexdigest()
        evidence = provenance.read_text()
        if 'commit=' + commit not in evidence or digest not in evidence:
            raise ValueError('binary does not match pinned build report: ' + core)
        target = output / 'emulators/libretro' / filename
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(binary, target)
        notices = output / 'emulators/licenses' / core
        notices.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(provenance, notices / 'build-info.txt')
        found = []
        root = sources / core
        directories = [root] + [p for p in root.iterdir() if p.is_dir() and
                               p.name not in ('.git', 'libretro-common', 'lilsmoke-build')]
        for directory in directories:
            if directory.is_dir():
                for notice in directory.iterdir():
                    if notice.is_file() and notice.name.lower().startswith(('license', 'copying', 'copyright')):
                        shutil.copyfile(notice, notices / notice.name)
                        found.append(notice.name)
        if core == 'pcsx_rearmed':
            # The dynarec has independent LGPL notices in nested submodules.
            for submodule in ('lightrec', 'lightning'):
                for notice in (root / 'deps' / submodule).glob('COPYING*'):
                    if notice.is_file():
                        name = submodule + '-' + notice.name
                        shutil.copyfile(notice, notices / name)
                        found.append(name)
        if not found:
            raise ValueError('missing source license notices: ' + core)
        info = output / 'emulators/info' / (filename.removesuffix('.so') + '.info')
        info.parent.mkdir(parents=True, exist_ok=True)
        info.write_text(f'display_name = "{name} ({system})"\ncorename = "{name}"\n'
                        f'systemname = "{system}"\nsupported_extensions = "{extensions}"\n'
                        'categories = "Emulator"\nsupports_no_game = "false"\n', encoding='utf-8')
        files.append(dict(core=core, path=str(target.relative_to(output)), sha256=digest,
                          bytes=target.stat().st_size, source=repository, commit=commit,
                          notices=sorted(set(found)), qualification='build; requires runtime qualification'))
    (output / 'cores-manifest.json').write_text(json.dumps({'schema': 1, 'cores': files}, indent=2) + '\n')
    return files


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('built', 'sources', 'lock', 'output'):
        parser.add_argument('--' + option, type=Path, required=True)
    args = parser.parse_args()
    staged = stage(args.built, args.sources, args.lock, args.output)
    print(f'Staged {len(staged)} optional cores; no device writes.')
