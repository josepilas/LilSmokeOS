#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""First-run native build backend for the original SM64 engine."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path, PurePosixPath
import shlex
import shutil
import signal
import sys
import tarfile
import time

from cook import Cache, Cancelled, digest_file, emit, regular, run_stage, verify_mips_executable
from resume import MARKER, checkpoint, clean_inflight, inflight, inflight_restart, source_files
from build_progress import BuildPlan, BuildProgress

ASSET_TOOLS = ('n64graphics', 'skyconv', 'mio0', 'aifc_decode')
TOOL_BUILD_CHECK = 'DUMMY != $(MAKE) -s -C tools >&2 || echo FAIL'
VERIFIED_TOOLS = 'DUMMY := # LilSmoke: native asset helpers verified before build'
HEXDUMP_RECIPE = '\thexdump -v -e \'1/1 "0x%X,"\' $< > $@'
BIN2C_RECIPE = '\t$(LILSMOKE_BIN2C) $< > $@'


def prepare_build_clock(stage, work, runtime, cancel=lambda: False):
    # A stock M8 can run at Unix epoch 1970. FAT-loaded inputs are at least
    # 1980, while newly written cached inodes can still report 1970. A logical
    # output clock avoids endless included-makefile restarts without root/time
    # changes, disabling dependency checks, or altering source contents.
    latest = 315619196
    for _, path, _ in source_files(work, cancel):
        latest = max(latest, int(path.stat().st_mtime))
    epoch = ((latest + 5) // 2) * 2
    if epoch > 2145916800:
        raise ValueError('Source dates exceed the MIPS32 build-clock range')
    source = runtime / 'bin/sm64-build-shell'
    verify_mips_executable(source)
    target = work / 'build/original-mips/sm64-build-shell'
    if target.exists():
        regular(target)
        if digest_file(target) != digest_file(source):
            target.unlink()
    if not target.exists():
        copy_buffered(source, target, cancel)
    if not os.access(target, os.X_OK):
        target.chmod(0o700)
    sound_source = runtime / 'sound_data.S'
    regular(sound_source, 16 * 1024)
    sound_target = work / 'build/original-mips/sound_data.S'
    if sound_target.exists():
        regular(sound_target, 16 * 1024)
        if digest_file(sound_target) != digest_file(sound_source):
            sound_target.unlink()
    if not sound_target.exists():
        copy_buffered(sound_source, sound_target, cancel)
    clock = stage / 'build-clock.mk'
    clock.write_text('''# SPDX-License-Identifier: MPL-2.0
# Generated private build configuration, not a replacement engine Makefile.
SHELL := build/original-mips/sm64-build-shell
.SHELLFLAGS := -c
export LILSMOKE_BUILD_TARGET = $@

# Compile once, generating dependencies in that same pass. The original
# hidden syntax check doubled parsing work and obscured the current task.
$(BUILD_DIR)/%.o: %.c
	$(CC) -c $(CFLAGS) -MMD -MP -MT $@ -MF $(BUILD_DIR)/$*.d -o $@ $<

$(BUILD_DIR)/%.o: $(BUILD_DIR)/%.c
	$(CC) -c $(CFLAGS) -MMD -MP -MT $@ -MF $(BUILD_DIR)/$*.d -o $@ $<

# Avoid millions of GCC initializer nodes for the original audio bytes.
$(BUILD_DIR)/sound/sound_data.o: $(BUILD_DIR)/sound_data.S $(SOUND_BIN_DIR)/bank_sets $(SOUND_BIN_DIR)/sequences.bin $(SOUND_BIN_DIR)/sound_data.tbl $(SOUND_BIN_DIR)/sound_data.ctl
	$(CC) -nostdinc -mips32r2 -mabi=32 -mfp32 -mhard-float -mdspr2 -c -x assembler-with-cpp -o $@ $(BUILD_DIR)/sound_data.S
''', encoding='ascii')
    os.environ['LILSMOKE_BUILD_EPOCH'] = str(epoch)
    stage_log('build_clock epoch=' + str(epoch) + ' wall=' + str(int(time.time())))
    return clock


def prepare_engine_makefile(work, fast_bin2c=False):
    # Only the checksum-verified private source tree is patched. GNU Make
    # still builds the game; the separately verified native helpers stay put.
    for name in ASSET_TOOLS:
        verify_mips_executable(work / 'tools' / name)
    makefile = work / 'Makefile'
    regular(makefile, 256 * 1024)
    text = makefile.read_text(encoding='utf-8')
    lines = text.splitlines(keepends=True)
    matches = [i for i, line in enumerate(lines) if line.rstrip('\r\n') == TOOL_BUILD_CHECK]
    prepared = [i for i, line in enumerate(lines) if line.rstrip('\r\n') == VERIFIED_TOOLS]
    if len(matches) == 1 and not prepared:
        lines[matches[0]] = VERIFIED_TOOLS + '\n'
    elif matches or len(prepared) != 1:
        raise ValueError('Unexpected bundled engine tool-build check; original ROM kept')
    if fast_bin2c:
        original = [i for i, line in enumerate(lines) if line.rstrip('\r\n') == HEXDUMP_RECIPE]
        converted = [i for i, line in enumerate(lines) if line.rstrip('\r\n') == BIN2C_RECIPE]
        if len(original) == 2 and not converted:
            for i in original:
                lines[i] = BIN2C_RECIPE + '\n'
        elif original or len(converted) != 2:
            raise ValueError('Unexpected bundled binary-conversion rules; original ROM kept')
    updated = ''.join(lines)
    if updated != text:
        makefile.write_text(updated, encoding='utf-8')


def prepare_asset_tools(stage, work):
    for name in ASSET_TOOLS:
        verify_mips_executable(work / 'tools' / name)
    # FAT timestamps have two-second granularity; scratch extraction also
    # deliberately omits utimes. Verified helpers must not be rebuilt here.
    # This scoped shim accepts only the upstream extractor's exact tool check;
    # it is removed from PATH before the real GNU Make engine build starts.
    tools = stage / 'prebuilt-tools'
    tools.mkdir()
    wrapper = tools / 'make'
    wrapper.write_text("""#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
[ "$#" -eq 7 ] && [ "$1" = -s ] && [ "$2" = -C ] && [ "$3" = tools/ ] &&
    [ "$4" = n64graphics ] && [ "$5" = skyconv ] && [ "$6" = mio0 ] &&
    [ "$7" = aifc_decode ] || { echo 'Unexpected asset-tool build request' >&2; exit 77; }
exit 0
""", encoding='ascii')
    if not os.access(wrapper, os.X_OK):
        wrapper.chmod(0o700)
    gmake = tools / 'gmake'
    copy_buffered(wrapper, gmake)
    if not os.access(gmake, os.X_OK):
        gmake.chmod(0o700)
    return tools


def stage_log(message):
    print('SM64_STAGE ' + message, file=sys.stderr, flush=True)


def copy_buffered(source, destination, cancel=lambda: False):
    # Vendor kernels do not consistently support sendfile-based shutil copies.
    with Path(source).open('rb') as incoming, Path(destination).open('xb') as outgoing:
        while True:
            if cancel():
                raise Cancelled('Preparation cancelled; original ROM kept')
            block = incoming.read(128 * 1024)
            if not block:
                break
            outgoing.write(block)


def extract_source(archive, work, expected, cancel=lambda: False, progress=lambda *args: None):
    archive_bytes = regular(archive, 32 * 1024 * 1024).st_size
    if digest_file(archive) != expected:
        raise ValueError('Bundled engine source failed checksum')
    work.mkdir()
    total = 0
    seen = set()
    last_report = 0
    directories = {(): work}
    directory_names = {(): ()}

    def ensure_directory(parts):
        if parts in directories:
            return directories[parts]
        folded = tuple(part.casefold() for part in parts)
        if folded in directory_names and directory_names[folded] != parts:
            raise ValueError('Source archive contains case-colliding directories')
        parent = ensure_directory(parts[:-1])
        target = parent / parts[-1]
        target.mkdir(mode=0o700)
        directories[parts] = target
        directory_names[folded] = parts
        return target

    def report(compressed, force=False):
        nonlocal last_report
        now = time.monotonic()
        if force or now - last_report >= 0.25:
            percent = min(19, 10 + compressed.tell() * 10 // max(1, archive_bytes))
            progress('Unpacking engine', percent, total)
            last_report = now

    progress('Unpacking engine', 10, 0)
    stage_log('unpack_begin')
    with archive.open('rb') as compressed, tarfile.open(fileobj=compressed, mode='r|gz') as source:
        for item in source:
            if cancel():
                raise Cancelled('Preparation cancelled; original ROM kept')
            name = PurePosixPath(item.name)
            if name.is_absolute() or '..' in name.parts or '\\' in item.name or not name.parts:
                raise ValueError('Unsafe source archive path')
            normalized = str(name).casefold()
            if normalized in seen or not (item.isdir() or item.isfile()):
                raise ValueError('Source archive contains duplicates or links')
            seen.add(normalized)
            if item.size < 0 or total + item.size > 128 * 1024 * 1024 or len(seen) > 10000:
                raise ValueError('Source archive exceeds the device budget')
            if item.isdir():
                ensure_directory(name.parts)
            else:
                target = ensure_directory(name.parts[:-1]) / name.name
                # The transaction directory is new and links are forbidden.
                # Exclusive creation also rejects an existing final symlink.
                with source.extractfile(item) as incoming, target.open('xb') as output:
                    remaining = item.size
                    while remaining:
                        if cancel():
                            raise Cancelled('Preparation cancelled; original ROM kept')
                        block = incoming.read(min(128 * 1024, remaining))
                        if not block:
                            raise ValueError('Truncated source archive')
                        output.write(block)
                        total += len(block)
                        remaining -= len(block)
                        report(compressed)
                if item.mode & 0o111 and not os.access(target, os.X_OK):
                    target.chmod(0o700)
            # Streaming tarfile still retains member objects on Python 3.11.
            # Links are forbidden, so there is no need for its lookup history.
            source.members.clear()
            report(compressed)
    progress('Unpacking engine', 20, total)
    stage_log('unpack_done files=' + str(len(seen)) + ' bytes=' + str(total))


class NativeBuild:
    def __init__(self, app):
        self.app = Path(app).absolute()
        self.runtime = self.app / 'runtime'
        recipe_path = self.runtime / 'recipe.json'
        regular(recipe_path, 65536)
        self.recipe = json.loads(recipe_path.read_text())
        if self.recipe.get('schema') != 1 or self.recipe.get('version') != 'Pre-Alpha-0.0.1':
            raise ValueError('Unsupported bundled build recipe')
        self.identity = digest_file(recipe_path)
        self.cancelled = False

    def cancel(self, *_):
        self.cancelled = True

    def build(self, stage, progress, cancel):
        work = stage / 'source'
        tools = self.runtime / 'compiler/wrappers'
        python = self.runtime / 'python/usr/bin/python3.11'
        sysroot = self.runtime / 'sysroot'
        env_path = str(tools) + ':' + str(self.runtime / 'bin') + ':/usr/bin:/bin'
        os.environ['LILSMOKE_APP_DIR'] = str(self.app)
        os.environ['PATH'] = env_path
        os.environ['PYTHONHOME'] = str(self.runtime / 'python/usr')
        os.environ['PYTHONDONTWRITEBYTECODE'] = '1'
        os.environ['LC_ALL'] = 'C'
        os.environ['MAKEFLAGS'] = '-j1'
        os.environ['OMP_NUM_THREADS'] = '1'
        os.environ.pop('LD_PRELOAD', None)
        if (stage / MARKER).exists():
            saved = json.loads((stage / MARKER).read_text())
            progress('Resuming engine build', 50, saved['extracted_bytes'])
            stage_log('resume_assets bytes=' + str(saved['extracted_bytes']))
            clean_inflight(stage)
            return self.compile(stage, work, saved['extracted_bytes'], progress, cancel)
        # Engine-source bytes are not ROM asset bytes; keep the UI counter stable.
        extract_source(self.runtime / 'source.tar.gz', work, self.recipe['source_sha256'], cancel,
            lambda phase, percent, _bytes: progress(phase, percent, 0))
        os.replace(stage / 'baserom.us.z64', work / 'baserom.us.z64')
        stage_log('normalized_rom_moved')
        asset_map = json.loads((work / 'assets.json').read_text())
        us_assets = {name: value[-2] for name, value in asset_map.items()
            if not name.startswith('@') and 'us' in value[-1]}
        total_assets = len(us_assets)
        pending = None
        completed = set()
        extracted = 0

        def extracted_line(line):
            nonlocal pending, extracted
            if pending is not None:
                path = work / pending
                if path.is_file() and not path.is_symlink():
                    extracted += path.stat().st_size
                    completed.add(pending)
                    pending = None
            if line.startswith('extracting '):
                name = line[len('extracting '):]
                if name in us_assets and name not in completed:
                    pending = name
            progress('Extracting assets', 20 + 30 * len(completed) // max(1, total_assets), extracted)

        progress('Extracting assets', 20, 0)
        stage_log('assets_begin expected=' + str(total_assets))
        asset_tools = prepare_asset_tools(stage, work)
        os.environ['PATH'] = str(asset_tools) + ':' + env_path
        try:
            run_stage([str(python), '-u', 'extract_assets.py', 'us'], work,
                stage / 'extraction.log', extracted_line, cancel)
        finally:
            os.environ['PATH'] = env_path
        extracted_line('')
        # The byte counter counts actual extracted files, not the input ROM size.
        extracted = sum((work / name).stat().st_size for name in us_assets
            if (work / name).is_file() and not (work / name).is_symlink())
        progress('Preparing engine', 50, extracted)
        stage_log('assets_done files=' + str(len(completed)) + ' bytes=' + str(extracted))
        prepare_engine_makefile(work, (self.runtime / 'bin/sm64-bin2c').is_file())
        checkpoint(stage, self.identity, extracted, progress, cancel)
        stage_log('checkpoint_ready')
        return self.compile(stage,work,extracted,progress,cancel)

    def compile(self,stage,work,extracted,progress,cancel):
        prepare_engine_makefile(work, (self.runtime / 'bin/sm64-bin2c').is_file())
        tools=self.runtime/'compiler/wrappers'
        python=self.runtime/'python/usr/bin/python3.11'
        sysroot=self.runtime/'sysroot'

        flags = ['-O2', '-mips32r2', '-mtune=74kc', '-mabi=32', '-mfp32', '-mhard-float',
            '-mdspr2', '-fno-fast-math', '-fno-strict-aliasing', '-fwrapv', '-fcommon',
            # Macro diagnostic histories dominate RAM for generated graphics data.
            '-ftrack-macro-expansion=0',
            '--param=ggc-min-expand=10', '--param=ggc-min-heapsize=4096',
            '-D_LANGUAGE_C', '-DVERSION_US', '-DNOWUP', '-DNON_MATCHING', '-DAVOID_UB',
            '-DTARGET_LINUX', '-DNO_SEGMENTED_MEMORY', '-DENABLE_SOFTRAST', '-DF3DEX_GBI_2E',
            '-DCONVERT', '-nostdinc', '-isystem', str(self.runtime / 'compiler/usr/lib/gcc/mipsel-linux-gnu/12/include'),
            '-isystem', str(sysroot / 'usr/include'), '-Iinclude', '-Isrc', '-I.',
            '-Ibuild/original-mips/include', '-Ibuild/original-mips', '-Ililsmoke',
            '-I' + str(sysroot / 'usr/include/SDL2')]
        cflags = shlex.join(flags)
        cc = shlex.quote(str(tools / 'cc'))
        build = work / 'build/original-mips'
        build.mkdir(parents=True, exist_ok=True)
        for name in ('lilsmoke-input.o', 'lilsmoke-common-input.o', 'lilsmoke-keymap.o', 'lilsmoke-log.o'):
            source = self.runtime / 'objects' / name
            regular(source, 4 * 1024 * 1024)
            destination = build / name
            if destination.exists():
                regular(destination, 4 * 1024 * 1024)
                if digest_file(source) == digest_file(destination):
                    continue
                destination.unlink()
            copy_buffered(source, destination, cancel)
        rules = build / 'level_rules.mk'
        if rules.exists():
            regular(rules, 256 * 1024)
            rules.unlink()
        progress('Preparing build clock', 50, extracted)
        clock = prepare_build_clock(stage, work, self.runtime, cancel)
        command = [str(tools / 'make'), '-j1', '-f', 'Makefile', '-f', 'lilsmoke/public.mk', '-f', str(clock),
            'VERSION=us', 'COMPILER=gcc', 'TARGET_FUNKEY=0', 'TARGET_OD=0',
            'ENABLE_OPENGL=0', 'ENABLE_OPENGL_LEGACY=0', 'ENABLE_SOFTRAST=1',
            'NOEXTRACT=1', 'BUILD_DIR=build/original-mips', 'CC=' + cc,
            'MAKE=' + shlex.quote(str(tools / 'make')),
            'CPP=' + cc + ' -E -x c -P -Wno-trigraphs', 'CC_CHECK=' + cc + ' -fsyntax-only',
            'AS=' + shlex.quote(str(tools / 'as')), 'OBJCOPY=' + shlex.quote(str(tools / 'objcopy')),
            'CC_CHECK_CFLAGS=' + cflags, 'CFLAGS=' + cflags,
            'PYTHON=' + shlex.quote(str(python)), 'RUNTIME_SYSROOT=' + shlex.quote(str(sysroot)),
            'RUNTIME_LIB=' + shlex.quote(str(self.runtime / 'lib'))]
        if (self.runtime / 'bin/sm64-bin2c').is_file():
            verify_mips_executable(self.runtime / 'bin/sm64-bin2c')
            command.append('LILSMOKE_BIN2C=' + shlex.quote(str(self.runtime / 'bin/sm64-bin2c')))
        plan = BuildPlan()
        progress('Planning engine tasks', 50, extracted)
        stage_log('plan_begin')
        run_stage(command + ['-n'], work, stage / 'plan.log', plan.on_line, cancel,
            timeout=900, on_tick=lambda: progress('Planning engine tasks', 50, extracted))
        plan.finish()
        stage_log('plan_done tasks=' + str(plan.total))
        live = BuildProgress(plan)
        labels = dict(texture='Textures', compile='Compiling', assembly='Assembly',
            link='Linking', audio='Audio data', generate='Generating')
        last_percent = 50
        last_logged = 0

        def report():
            nonlocal last_percent, last_logged
            value = live.snapshot()
            active = value['active']
            label = labels[active.kind] if active else 'Preparing engine'
            phase = label + ' ' + str(value['completed']) + '/' + str(value['planned'])
            if active and active.target and len(phase) < 30:
                phase += ' ' + PurePosixPath(active.target).name[:36-len(phase)]
            fraction = value['percent']
            if fraction is not None:
                last_percent = max(last_percent, min(95, 50 + fraction * 45 // 100))
            progress(phase, last_percent, extracted)
            if time.monotonic() - last_logged > 15:
                stage_log('build_progress completed=' + str(value['completed']) +
                    ' planned=' + str(value['planned']) + ' phase=' + label +
                    ' target=' + (active.target if active else ''))
                last_logged = time.monotonic()

        def compiled_line(line):
            previous = live.active
            live.on_line(line)
            active = live.active
            if active is not None and active is not previous:
                target = work / active.target
                try:
                    if target.is_dir():
                        raise ValueError('Directory producer')
                    inflight(stage, active.target)
                except ValueError:
                    inflight_restart(stage)
                report()

        stage_log('compile_begin')
        try:
            run_stage(command, work, stage / 'build.log', compiled_line, cancel,
                timeout=6 * 3600, on_tick=report)
        except BaseException:
            live.finish(False)
            raise
        live.finish()
        (stage / 'inflight.json').unlink(missing_ok=True)
        progress('Engine build complete', 95, extracted)
        stage_log('compile_done')
        generated = work / 'build/original-mips/sm64.us.f3dex2e'
        regular(generated, 32 * 1024 * 1024)
        verify_mips_executable(generated)
        validation_home=stage/'validation-saves'
        validation_home.mkdir(exist_ok=True)
        validation=dict(os.environ, HOME=str(validation_home),
            SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',LILSMOKE_HCFB='0',SM64_ORIGINAL_PREFLIGHT='1',
            LILSMOKE_M8_AUDIO='0',SM64_ORIGINAL_TEST_FRAMES='30',SM64_ORIGINAL_RENDER_EVERY='1',
            LD_LIBRARY_PATH=str(self.runtime/'lib')+':/usr/lib:/lib')
        for key in ('SM64_ORIGINAL_EVIDENCE','SM64_ORIGINAL_INTERACTIVE_AFTER','SM64_SOFTWARE_WIDTH'):
            validation.pop(key,None)
        completed=False
        def validated_line(line):
            nonlocal completed
            if re.search(r'OriginalEngineTest=FINISHED Frames=30 Rendered=30(?: |$)',line):
                completed=True
        progress('Testing engine', 96, extracted)
        run_stage([str(generated)],work,stage/'validation.log',validated_line,cancel,
            env=validation,timeout=60)
        if not completed:
            raise RuntimeError('Cooked original engine did not finish the render validation; original ROM kept')
        copy_buffered(generated, stage / 'game', cancel)
        progress('Verifying assets', 98, extracted)
        # Only the verified locally linked game and small diagnostic logs persist.
        # Cache removes scratch inputs only after committing/readback, so a
        # power cut while cleaning up cannot destroy the resumable build.
        return stage / 'game', extracted


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--cached-only', action='store_true', help='Verify an existing game without preparing or modifying it')
    args = parser.parse_args()
    builder=None
    try:
        builder = NativeBuild(args.app)
        signal.signal(signal.SIGTERM, builder.cancel)
        signal.signal(signal.SIGINT, builder.cancel)
        cache = Cache(args.app, builder.identity)
        if args.cached_only:
            game = cache.current()
            if game is None:
                return 3
        else:
            game = cache.prepare(builder.build, emit, lambda: builder.cancelled, resumable=True)
        print(json.dumps(dict(ready=str(game))), flush=True)
    except (OSError, ValueError, RuntimeError) as error:
        print('SM64_PREPARATION_ERROR type='+type(error).__name__+' errno='+str(getattr(error,'errno',None))+' message='+str(error),file=sys.stderr,flush=True)
        print(json.dumps(dict(error=str(error))), flush=True)
        return 130 if isinstance(error, Cancelled) else 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
