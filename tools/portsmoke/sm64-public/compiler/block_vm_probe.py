#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compile a private renderer in a bounded MIPS VM using disposable ext4 inputs.

No host mount, physical disk, ROM, game build, source upload, or public recipe
change is involved. All private inputs and outputs remain under --out.
"""
import argparse
from collections import Counter
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import stat
import subprocess
import sys
import time


HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[3]
BUILD = Path('/root/lilsmoke-os/build/buildroot/output')
COMPILER = Path('/root/lilsmoke-public-compiler/runtime')
SOURCE = Path('/root/lilsmoke-sm64-original-private-iig2ryae')
QEMU = Path('/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel')
RENDERER = Path('src/pc/gfx/gfx_soft.c')
CPU_FLAGS = ['-mips32r2', '-mabi=32', '-mhard-float', '-mfp32', '-mdspr2']
RENDER_FLAGS = [
    '-O3', *CPU_FLAGS, '-fno-fast-math', '-fno-strict-aliasing', '-fwrapv',
    '-fcommon', '-fno-math-errno', '-D_LANGUAGE_C', '-DVERSION_US', '-DNOWUP',
    '-DNON_MATCHING', '-DAVOID_UB', '-DTARGET_LINUX', '-DNO_SEGMENTED_MEMORY',
    '-DENABLE_SOFTRAST', '-DF3DEX_GBI_2E', '-DCONVERT', '-nostdinc',
    '-isystem', '/compiler/usr/lib/gcc/mipsel-linux-gnu/12/include',
    '-isystem', '/sdk/usr/include', '-I/sdk/usr/include/SDL2', '-Iinclude',
    '-Isrc', '-I.', '-Ibuild/original-mips/include', '-Ibuild/original-mips',
]


def sha256(path):
    with path.open('rb') as handle:
        return hashlib.file_digest(handle, 'sha256').hexdigest()


def stage_inputs(out):
    stage = out / 'payload'
    stage.mkdir()
    shutil.copytree(COMPILER, stage / 'compiler', symlinks=True)
    sdk = stage / 'sdk'
    shutil.copytree(BUILD / 'staging/usr/include', sdk / 'usr/include', symlinks=True)
    (sdk / 'lib').mkdir()
    (sdk / 'usr/lib').mkdir()
    for name in ('crt1.o', 'crti.o', 'crtn.o', 'libc.so'):
        shutil.copy2(BUILD / 'staging/lib' / name, sdk / 'lib' / name)
    # Only renderer bytes and headers, never ROMs, textures, binaries, or assets.
    selected = sorted(p for p in SOURCE.rglob('*.h') if '.git' not in p.parts)
    selected.append(SOURCE / RENDERER)
    manifest = {}
    for original in selected:
        relative = original.relative_to(SOURCE)
        target = stage / 'source' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, target)
        manifest[str(relative)] = {'bytes': target.stat().st_size, 'sha256': sha256(target)}
    (stage / 'results').mkdir()
    (stage / 'probe').mkdir()
    shutil.copy2(HERE / 'probe.c', stage / 'probe/probe.c')
    (out / 'source-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    image = out / 'inputs.ext4'
    with image.open('xb') as handle:
        handle.truncate(1024 * 1024 * 1024)
    if not stat.S_ISREG(image.stat().st_mode):
        raise RuntimeError('Disposable image is not a regular file')
    command = ['mke2fs', '-q', '-t', 'ext4', '-F', '-L', 'SM64_BLOCK_PROBE',
               '-O', '^orphan_file,^metadata_csum_seed', '-d', str(stage), str(image)]
    with (out / 'mke2fs.log').open('w') as output:
        subprocess.run(command, check=True, stdout=output, stderr=subprocess.STDOUT)
    return image, manifest[str(RENDERER)]


INIT = r'''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /block /compiler /sdk /source /dev/pts
mount -t devpts devpts /dev/pts
mount -t ext4 -o noatime /dev/vdb /block || { echo QA_STORAGE_FAILURE; poweroff -f; }
mount -o bind /block/compiler /compiler
mount -o bind /block/sdk /sdk
mount -o bind /block/source /source
mkdir -p /block/tmp
export TMPDIR=/block/tmp LC_ALL=C
echo QA_STORAGE_BEGIN
cat /proc/mounts
echo QA_STORAGE_END
echo QA_CPU_BEGIN
cat /proc/cpuinfo
echo QA_CPU_END
echo QA_MEM_BOOT_BEGIN
cat /proc/meminfo
echo QA_MEM_BOOT_END

tree_pids() {
    tree="$1"
    frontier="$1"
    while [ -n "$frontier" ]; do
        next=""
        for parent in $frontier; do
            children=$(awk -v parent="$parent" '
                /^Pid:/ { pid=$2 }
                /^PPid:/ && $2 == parent { print pid }
                ' /proc/[0-9]*/status 2>/dev/null)
            next="$next $children"
        done
        frontier=$(echo "$next" | tr -s ' ')
        [ "$frontier" != " " ] || frontier=""
        tree="$tree $frontier"
    done
    echo "$tree"
}

snapshot() {
    label="$1"
    root="$2"
    read uptime rest < /proc/uptime
    echo "QA_SAMPLE_BEGIN label=$label uptime=$uptime root=$root"
    echo QA_MEM_BEGIN
    cat /proc/meminfo
    echo QA_MEM_END
    rss_total=0
    pss_total=0
    for pid in $(tree_pids "$root"); do
        [ -r /proc/$pid/status ] || continue
        echo "QA_PROCESS_BEGIN pid=$pid"
        printf 'cmdline: '
        tr '\000' ' ' < /proc/$pid/cmdline
        printf '\nwchan: '
        cat /proc/$pid/wchan
        printf '\nstat: '
        cat /proc/$pid/stat
        grep -E '^(Name|State|Pid|PPid|VmPeak|VmSize|VmHWM|VmRSS|RssAnon|RssFile|RssShmem):' /proc/$pid/status
        cat /proc/$pid/io 2>/dev/null
        if [ -r /proc/$pid/smaps_rollup ]; then
            grep -E '^(Rss|Pss|Private_Clean|Private_Dirty|Shared_Clean|Shared_Dirty|Swap):' /proc/$pid/smaps_rollup
        fi
        rss=$(awk '/^VmRSS:/ {print $2}' /proc/$pid/status)
        pss=$(awk '/^Pss:/ {print $2}' /proc/$pid/smaps_rollup 2>/dev/null)
        rss_total=$((rss_total + ${rss:-0}))
        pss_total=$((pss_total + ${pss:-0}))
        echo QA_PROCESS_END
    done
    echo "QA_TREE_SUM rss_kib=$rss_total pss_kib=$pss_total"
    echo QA_PROGRESS_LOG_BEGIN
    tail -c 1024 /block/results/$label.log
    printf '\nQA_PROGRESS_LOG_END\n'
    echo QA_SAMPLE_END
}

run_compile() {
    label="$1"
    shift
    echo "QA_COMPILE_BEGIN label=$label"
    read start rest < /proc/uptime
    start=${start%%.*}
    (
        ulimit -v 92160 || exit 125
        echo QA_LIMITS_BEGIN
        cat /proc/self/limits
        echo QA_LIMITS_END
        exec "$@"
    ) > /block/results/$label.log 2>&1 &
    driver=$!
    timeout=0
    while kill -0 "$driver" 2>/dev/null; do
        snapshot "$label" "$driver"
        read now rest < /proc/uptime
        now=${now%%.*}
        if [ $((now-start)) -ge @TIMEOUT@ ]; then
            echo "QA_GUEST_TIMEOUT label=$label"
            timeout=1
            children=$(tree_pids "$driver")
            for pid in $children; do kill -TERM "$pid" 2>/dev/null; done
            sleep 1
            for pid in $children; do kill -KILL "$pid" 2>/dev/null; done
            break
        fi
        sleep @SAMPLE@
    done
    wait "$driver"
    code=$?
    [ "$timeout" = 0 ] || code=124
    echo "QA_COMPILE_END label=$label code=$code"
    cat /block/results/$label.log
    return "$code"
}

run_compile tiny /compiler/wrappers/cc -O2 @CPU_FLAGS@ \
    -nostdinc -isystem /compiler/usr/lib/gcc/mipsel-linux-gnu/12/include \
    -isystem /sdk/usr/include -c /block/probe/probe.c -o /block/results/probe.o
tiny=$?
link=125
run=125
gfx=125
if [ "$tiny" = 0 ]; then
    /compiler/wrappers/ld -m elf32ltsmip --dynamic-linker /lib/ld-musl-mipsel.so.1 \
        -o /block/results/probe /sdk/lib/crt1.o /sdk/lib/crti.o /block/results/probe.o \
        -L/sdk/usr/lib -L/sdk/lib -lc /sdk/lib/crtn.o > /block/results/link.log 2>&1
    link=$?
    echo "QA_NATIVE_LINK=$link"
    cat /block/results/link.log
    if [ "$link" = 0 ]; then
        /block/results/probe
        run=$?
        echo "QA_NATIVE_RUN=$run"
    fi
    cd /source
    run_compile graphics /compiler/wrappers/cc @GFX_FLAGS@ \
        @DIAGNOSTIC_FLAGS@ /block/results/graphics.d \
        -c src/pc/gfx/gfx_soft.c -o /block/results/private-gfx-probe.o
    gfx=$?
    echo "QA_NATIVE_GRAPHICS_COMPILE=$gfx"
fi
echo QA_MEM_FINAL_BEGIN
cat /proc/meminfo
echo QA_MEM_FINAL_END
echo QA_DMESG_BEGIN
dmesg | tail -n 60
echo QA_DMESG_END
echo "QA_DONE tiny=$tiny link=$link run=$run graphics=$gfx"
sync
poweroff -f
'''


def parse_telemetry(serial):
    serial = serial.replace('\r', '')
    samples = []
    for match in re.finditer(r'QA_SAMPLE_BEGIN label=(\w+) uptime=([\d.]+) root=(\d+)\n(.*?)QA_SAMPLE_END', serial, re.S):
        label, uptime, root, body = match.groups()
        memory = re.search(r'QA_MEM_BEGIN\n(.*?)QA_MEM_END', body, re.S)
        values = {k: int(v) for k, v in re.findall(r'^(\w+):\s+(\d+)', memory[1], re.M)} if memory else {}
        processes = []
        for process in re.finditer(r'QA_PROCESS_BEGIN pid=(\d+)\n(.*?)QA_PROCESS_END', body, re.S):
            pid, detail = process.groups()
            metrics = {k: int(v) for k, v in re.findall(r'^(Vm\w+|Rss\w*|Pss|PPid):\s+(\d+)', detail, re.M)}
            def field(name):
                found = re.search(r'^' + name + r': (.*)$', detail, re.M)
                return found[1] if found else None
            processes.append(dict(pid=int(pid), cmdline=field('cmdline'), wchan=field('wchan'),
                                  stat=field('stat'), **metrics))
        total = re.search(r'QA_TREE_SUM rss_kib=(\d+) pss_kib=(\d+)', body)
        samples.append(dict(label=label, uptime_seconds=float(uptime), root_pid=int(root),
                            meminfo_kib=values, processes=processes,
                            tree_rss_sum_kib=int(total[1]) if total else None,
                            tree_pss_sum_kib=int(total[2]) if total else None))
    return samples


def storage_verified(serial):
    table = re.search(r'QA_STORAGE_BEGIN\r?\n(.*?)QA_STORAGE_END', serial, re.S)
    if table is None:
        return False
    mounts = [line.split() for line in table[1].splitlines()]
    return (any(row[:3] == ['/dev/vdb', '/block', 'ext4'] for row in mounts)
            and not any(len(row) >= 3 and row[2] == '9p' for row in mounts))


def extract_results(image, out):
    extracted = out / 'results'
    extracted.mkdir()
    with (out / 'debugfs.log').open('w') as output:
        for name in ('tiny.log', 'link.log', 'graphics.log', 'graphics.d', 'probe.o',
                     'probe', 'private-gfx-probe.o'):
            subprocess.run(['debugfs', '-R', f'dump /results/{name} {extracted / name}', str(image)],
                           stdout=output, stderr=subprocess.STDOUT, check=False)
    return extracted


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--compile-timeout', type=int, default=600)
    parser.add_argument('--sample-seconds', type=int, default=5)
    parser.add_argument('--ggc-min-expand', type=int)
    parser.add_argument('--ggc-min-heapsize', type=int)
    parser.add_argument('--verbose-gcc', action='store_true',
                        help='Include GCC phase/function progress without changing codegen options')
    parser.add_argument('--block-rootfs', action='store_true',
                        help='Use the existing squashfs block root instead of the reference RAM root')
    args = parser.parse_args()
    out = args.out.absolute()
    if out.parent != Path('/root') or not re.fullmatch(r'sm64-public-block-[A-Za-z0-9_.-]+', out.name):
        parser.error('--out must be a new /root/sm64-public-block-* directory')
    if not 1 <= args.compile_timeout <= 1800 or not 1 <= args.sample_seconds <= 30:
        parser.error('timeout must be 1..1800 seconds; sample interval 1..30 seconds')
    flags = list(RENDER_FLAGS)
    diagnostics = (['-Q'] if args.verbose_gcc else []) + ['-ftime-report', '-fmem-report', '-MD', '-MF']
    for name in ('ggc_min_expand', 'ggc_min_heapsize'):
        value = getattr(args, name)
        if value is not None:
            if value < 0:
                parser.error('GGC parameters must be nonnegative')
            flags += ['--param', name.replace('_', '-') + '=' + str(value)]
    out.mkdir(exist_ok=False, mode=0o700)
    image, source_identity = stage_inputs(out)
    sys.path.insert(0, str(PROJECT / 'tools/hwtest'))
    from mips_boot_smoke import Newc
    from mips_disk_fixture import overlay_initramfs, disk_devices
    init = INIT.replace('@TIMEOUT@', str(args.compile_timeout)).replace('@SAMPLE@', str(args.sample_seconds))
    init = init.replace('@CPU_FLAGS@', shlex.join(CPU_FLAGS)).replace('@GFX_FLAGS@', shlex.join(flags))
    init = init.replace('@DIAGNOSTIC_FLAGS@', shlex.join(diagnostics))
    overlay = Newc()
    overlay.file('usr/bin/bench-init', init, 0o755)
    initrd = out / 'boot.cpio.gz'
    initrd.write_bytes(gzip.compress(overlay_initramfs(BUILD, overlay,
                       '/usr/bin/bench-init', overlay_mib=16, rootfs_in_ram=not args.block_rootfs)))
    command = [str(QEMU), '-M', 'malta', '-cpu', '74Kf-lilsmoke', '-smp', '1', '-m', '120M',
               '-kernel', str(BUILD / 'images/vmlinux'), '-initrd', str(initrd),
               '-append', 'console=ttyS0 rdinit=/init panic=1', '-display', 'none',
               '-monitor', 'none', '-serial', 'file:' + str(out / 'serial.log'), '-no-reboot']
    command += disk_devices(BUILD, [])
    command += ['-drive', f'file={image},if=none,id=probeinputs,format=raw',
                '-device', 'virtio-blk-pci,drive=probeinputs,addr=0x14.1']
    (out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    started = time.monotonic()
    timed_out = False
    interrupted = False
    graphics_wall_started = None
    graphics_wall_finished = None
    with (out / 'qemu.log').open('w') as output:
        process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
        (out / 'host-processes.json').write_text(json.dumps(
            dict(probe_pid=os.getpid(), qemu_pid=process.pid), indent=2) + '\n')
        try:
            deadline = started + 2 * args.compile_timeout + 120
            while process.poll() is None:
                serial = (out / 'serial.log').read_text(errors='replace') if (out / 'serial.log').exists() else ''
                now = time.monotonic()
                if graphics_wall_started is None and 'QA_COMPILE_BEGIN label=graphics' in serial:
                    graphics_wall_started = now
                if graphics_wall_finished is None and 'QA_COMPILE_END label=graphics' in serial:
                    graphics_wall_finished = now
                if 'reboot: System halted' in serial or 'reboot: Power down' in serial:
                    break
                if (now >= deadline or (graphics_wall_started is not None
                        and graphics_wall_finished is None
                        and now - graphics_wall_started >= args.compile_timeout)):
                    timed_out = True
                    break
                time.sleep(1)
        except KeyboardInterrupt:
            interrupted = True
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    serial = (out / 'serial.log').read_text(errors='replace') if (out / 'serial.log').exists() else ''
    serial = serial.replace('\r', '')
    extracted = extract_results(image, out)
    samples = parse_telemetry(serial)
    (out / 'samples.json').write_text(json.dumps(samples, indent=2) + '\n')
    graphics = [s for s in samples if s['label'] == 'graphics']
    objects = [p for s in graphics for p in s['processes']]
    elf = extracted / 'private-gfx-probe.o'
    elf_header = ''
    if elf.is_file():
        inspection = subprocess.run([str(BUILD / 'host/bin/mipsel-buildroot-linux-musl-readelf'),
                                     '-h', str(elf)], text=True, capture_output=True, check=False)
        (out / 'renderer-elf-header.txt').write_text(inspection.stdout + inspection.stderr)
        if inspection.returncode == 0:
            elf_header = inspection.stdout
    codes = {}
    for label, code in re.findall(r'QA_COMPILE_END label=(tiny|graphics) code=(\d+)', serial):
        codes[label] = int(code)
    for label, code in re.findall(r'QA_NATIVE_(LINK|RUN)=(\d+)', serial):
        codes[label.lower()] = int(code)
    verified_storage = storage_verified(serial)
    passed = (codes == dict(tiny=0, link=0, run=0, graphics=0)
              and 'PASS_NATIVE_MIPS_MUSL_COMPILER' in serial and verified_storage
              and 'MIPS' in elf_header and 'REL (Relocatable file)' in elf_header)
    reasons = []
    if interrupted:
        reasons.append('Probe interrupted; no completed renderer claim')
    if timed_out or 'QA_GUEST_TIMEOUT' in serial:
        reasons.append('Compiler exceeded the bounded timeout')
    if not verified_storage:
        reasons.append('Ext4-only input storage was not verified')
    if codes.get('graphics') is None:
        reasons.append('No renderer compiler exit code was observed')
    elif codes['graphics'] != 0:
        reasons.append('Renderer compiler exit code ' + str(codes['graphics']))
    if not elf_header:
        reasons.append('No readable renderer ELF object was produced')
    for name in ('tiny', 'link', 'run'):
        if codes.get(name) != 0:
            reasons.append(name + ' probe did not pass')
    compiler_objects = [p for p in objects if '/12/cc1' in (p['cmdline'] or '')]
    memory_failures = re.findall(r'^.*(?:Out of memory|oom-kill|Killed process|'
                                 r'virtual memory exhausted|cannot allocate memory).*$', serial, re.M | re.I)
    memory_values = [s['meminfo_kib'] for s in graphics]
    memtotal = re.search(r'MemTotal:\s+(\d+)', serial)
    report = dict(
        passed=passed, failure_reasons=reasons, exit_codes=codes, guest_cpu='74Kf-lilsmoke', guest_cpus=1,
        configured_guest_ram_mib=120, guest_memtotal_kib=int(memtotal[1]) if memtotal else None,
        process_virtual_limit_kib=92160, rootfs_in_ram=not args.block_rootfs,
        compiler_inputs='disposable regular 1 GiB ext4 virtio-blk image; no 9p',
        storage_verified=verified_storage, compiler_and_gameplay_distinct=True,
        native_mips_compiler=True, tiny_musl_run_passed=codes.get('run') == 0,
        public_gameplay_tested=False, public_game_package_ready=False,
        source_relative_path=str(RENDERER), source_bytes=source_identity['bytes'],
        source_sha256=source_identity['sha256'], live_source_sha256_after=sha256(SOURCE / RENDERER),
        renderer_flags=flags, diagnostic_flags=diagnostics,
        renderer_object_sha256=sha256(elf) if elf.is_file() else None,
        renderer_object_bytes=elf.stat().st_size if elf.is_file() else None,
        max_sampled_graphics_process_vmpeak_kib=max((p.get('VmPeak', 0) for p in objects), default=0),
        max_sampled_graphics_process_rss_kib=max((p.get('VmRSS', 0) for p in objects), default=0),
        cc1_sample_count=len(compiler_objects),
        max_sampled_cc1_vmpeak_kib=max((p.get('VmPeak', 0) for p in compiler_objects), default=0),
        max_sampled_cc1_rss_kib=max((p.get('VmRSS', 0) for p in compiler_objects), default=0),
        max_observed_cc1_vmhwm_kib=max((p.get('VmHWM', 0) for p in compiler_objects), default=0),
        cc1_wchan_sample_counts=dict(Counter(p['wchan'] for p in compiler_objects)),
        memory_failure_evidence=memory_failures,
        max_sampled_graphics_tree_rss_sum_kib=max((s['tree_rss_sum_kib'] or 0 for s in graphics), default=0),
        max_sampled_graphics_tree_pss_sum_kib=max((s['tree_pss_sum_kib'] or 0 for s in graphics), default=0),
        min_sampled_graphics_memavailable_kib=min((v.get('MemAvailable', 0) for v in memory_values), default=None),
        max_sampled_graphics_guest_used_including_cache_kib=max(
            (v.get('MemTotal', 0) - v.get('MemFree', 0) for v in memory_values), default=None),
        graphics_sample_count=len(graphics), sample_interval_seconds=args.sample_seconds,
        rss_sum_note='Guest driver plus compiler descendants; shared mappings may be counted twice. Sampled, not guaranteed peak.',
        target_ram_evidence='Only guest /proc/meminfo and guest process status/smaps; no host QEMU rusage.',
        host_deadline_reached=timed_out, interrupted=interrupted,
        guest_timeout='QA_GUEST_TIMEOUT' in serial,
        qemu_process_reaped=process.poll() is not None,
        qemu_exit_code=process.returncode,
        graphics_elapsed_wall_seconds=round(
            (graphics_wall_finished or time.monotonic()) - graphics_wall_started, 2)
            if graphics_wall_started is not None else None,
        timeout_clock='Guest uptime plus host monotonic renderer deadline',
        elapsed_seconds=round(time.monotonic() - started, 2),
    )
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2), flush=True)
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
