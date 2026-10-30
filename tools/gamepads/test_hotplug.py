#!/usr/bin/env python3
"""Isolated serial host/sanitizer/real-MIPS input helpers. No SDK/target writes."""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import struct
import tempfile
import time

P = Path(__file__).resolve().parents[2]
B = Path('/root/lilsmoke-os/build/buildroot/output')
S = P / 'package/lilsmoke-ui/src/src'
T = P / 'tools/gamepads'


def run(argv, **kwargs):
    print('+', shlex.join(map(str, argv)), flush=True)
    return subprocess.run(list(map(str, argv)), check=True, timeout=120, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--host-only', action='store_true')
    parser.add_argument('--case', action='append')
    parser.add_argument('--mode', action='append', choices=['host', 'sanitizers', 'mips'])
    args = parser.parse_args()
    q = args.output or Path(tempfile.mkdtemp(prefix='lilsmoke-hotplug-'))
    q.mkdir(parents=True, exist_ok=True)
    assert os.geteuid() == 0, 'The production state reader requires a root-owned file'
    sdk = B / 'staging'
    pocket = next((B / 'build').glob('opendingux-emulators-*'))
    ra = next((B / 'build').glob('retroarch-mips-*'))
    cases = {
        'state': ([T / 'test_state.c', T / 'test_legacy_reader.c'], [], [], ['-Wl,--wrap=clock_gettime'], []),
        'input': ([T / 'test_input_hotplug.cpp', *[S / (n + '.cpp') for n in ('Input', 'Keymap', 'Log')]],
                  [], [], ['-Wl,--wrap=clock_gettime'], []),
        'daemon': ([T / 'test_daemon_hotplug.c'], [P / 'package/lilsmoke-usb-gamepads/src'], [], [], []),
        'rk707': ([T / 'test_rk707.c'], [P / 'package/lilsmoke-usb-gamepads/src'], [], [], []),
        'pocketsnes': ([T / 'test_pocketsnes_hotplug.c'],
                      [pocket / 'sal/linux/include', pocket / 'sal/include'], ['-D__DINGUX__'], ['-Wl,--wrap=clock_gettime'], []),
        'pocketsnes-regression': ([P / 'tools/m8/m8-pocketsnes-test.c'],
                      [P / 'package/opendingux-emulators', pocket / 'sal/linux/include', pocket / 'sal/include'],
                      ['-D__DINGUX__'], [], [P / 'tools/m8/m8-pocketsnes-test.map']),
        'retroarch': ([P / 'tools/m8/test-retroarch-input.c'], [ra, ra / 'libretro-common/include'], [], [], [q / 'maps']),
        'native-input': ([P / 'tools/hwtest/gamepad_smoke.cpp', *[S / (n + '.cpp') for n in ('Input', 'Keymap', 'Log')]], [], [], [], []),
        'cursor': ([P / 'tools/hwtest/ui_cursor_input_smoke.cpp', *[S / (n + '.cpp') for n in ('Input', 'Keymap', 'Log')]], [], [], [], []),
    }
    maps = q / 'maps'
    for name in ('builtin', 'user', 'gamepads'):
        (maps / name).mkdir(parents=True, exist_ok=True)
    (maps / 'builtin/default.map').write_text('evdev 29 a\nevdev 28 b\n')
    (maps / 'user/user.map').write_text('evdev 28 start\n')
    results = []
    for mode in (args.mode or (['host', 'sanitizers'] if args.host_only else ['host', 'sanitizers', 'mips'])):
        for name, (sources, includes, defines, links, command_args) in cases.items():
            if args.case and name not in args.case:
                continue
            cpp = sources[0].suffix == '.cpp'
            compiler = ('g++' if cpp else 'gcc') if mode != 'mips' else B / ('host/bin/mipsel-buildroot-linux-musl-' + ('g++' if cpp else 'gcc'))
            flags = ['-std=' + ('c++17' if cpp else 'gnu11'), '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-I' + str(S)]
            flags += ['-I' + str(d) for d in includes] + defines
            if mode == 'sanitizers':
                flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie']
            if mode == 'mips':
                flags += ['-march=mips32r2', '-mtune=74kc', '-mdspr2', '-mfp32', '-mhard-float']
                if '-Wl,--wrap=clock_gettime' in links:
                    flags += ['-Wl,--wrap=__clock_gettime64']
                if cpp:
                    flags += ['-include', str(P / 'package/lilsmoke-od-sdl/src/input-wire.h')]
            sdl = name in ('input', 'daemon', 'native-input', 'cursor')
            if sdl:
                flags += (['-I' + str(sdk / 'usr/include/SDL2')] if mode == 'mips' else
                          shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl2'], text=True)))
            if name.startswith('pocketsnes'):
                flags += ['-I' + str(sdk / 'usr/include/SDL')]
            binary = q / (name + '-' + mode)
            run([compiler, *flags, *sources, *links, *(['-lSDL2'] if sdl else []), '-o', binary])
            command = [binary, *command_args]
            if mode == 'mips':
                command = ['qemu-mipsel', '-cpu', '74Kf', '-L', sdk, *command]
            run(command)
            results.append({'case': name, 'mode': mode, 'passed': True})
    for mode in (['host'] if args.host_only else ['host', 'mips']):
        cc = 'gcc' if mode == 'host' else B / 'host/bin/mipsel-buildroot-linux-musl-gcc'
        binary = q / ('live-daemon-' + mode)
        include = '/usr/include/SDL2' if mode == 'host' else str(sdk / 'usr/include/SDL2')
        run([cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-I' + str(S), '-I' + include,
             P / 'package/lilsmoke-usb-gamepads/src/usb-gamepads.c', '-lSDL2', '-o', binary])
        command = [binary, '--rk707-fixture']
        if mode == 'mips': command = ['qemu-mipsel', '-cpu', '74Kf', '-L', sdk, *command]
        state = q / ('live-state-' + mode)
        env = dict(os.environ, LILSMOKE_GAMEPAD_STATE=str(state), SDL_VIDEODRIVER='dummy')
        with (q / ('live-daemon-' + mode + '.log')).open('w') as log:
            proc = subprocess.Popen(list(map(str, command)), env=env, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 10
                while not state.exists() and proc.poll() is None and time.monotonic() < deadline:
                    time.sleep(.01)
                assert state.exists(), 'daemon did not publish state'
                first = None
                samples = []
                while time.monotonic() < deadline:
                    data = state.read_bytes()
                    magic, abi, seq, pid, stamp = struct.unpack_from('<IIIIQ', data)
                    if not seq % 2 and struct.unpack_from('<I', data, 24)[0]:
                        assert magic == 0x4c534750 and abi == 1 and len(data) == 424 and pid
                        assert struct.unpack_from('<I', data, 36)[0] & (1 << 2)
                        samples.append(stamp)
                        if first is None: first = stamp
                        if stamp - first >= 1100: break
                    assert proc.poll() is None, 'daemon exited before heartbeat test'
                    time.sleep(.05)
                assert samples and samples[-1] - samples[0] >= 1100
                duplicate = subprocess.run(list(map(str, command)), env=env, capture_output=True, timeout=10)
                assert duplicate.returncode == 1 and b'state_owner_busy' in duplicate.stderr
            finally:
                if proc.poll() is None: proc.terminate()
                proc.wait(timeout=10)
            assert proc.returncode == 0
            assert not any(state.read_bytes()[24:]), 'clean shutdown must publish neutral state'
        results.append({'case': 'live-daemon-heartbeat-legacy-abi-single-owner', 'mode': mode, 'passed': True})
    # Patch only a temporary copy. Exercise the actual changed discovery gate
    # with notifications available (the M8 failure), not a reimplemented gate.
    sdl_source = B / 'build/sdl2-2.28.5'
    dest = q / 'sdl-patch/src/hidapi/SDL_hidapi.c'
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(sdl_source / 'src/hidapi/SDL_hidapi.c', dest)
    driver = q / 'sdl-patch/src/joystick/hidapi/SDL_hidapijoystick.c'
    driver.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(sdl_source / 'src/joystick/hidapi/SDL_hidapijoystick.c', driver)
    patch = P / 'package/lilsmoke-usb-gamepads/sdl-input/0001-sdl-hidapi-usbfs-poll.patch'
    patch_args = ['patch', '--fuzz=0', '-p1', '-i', str(patch)]
    forward = subprocess.run([*patch_args, '--forward', '--dry-run', '--batch'], cwd=q / 'sdl-patch', capture_output=True, text=True, timeout=30)
    if forward.returncode == 0:
        run([*patch_args, '--forward', '--batch'], cwd=q / 'sdl-patch')
    else:
        reverse = subprocess.run([*patch_args, '--reverse', '--force', '--dry-run'], cwd=q / 'sdl-patch', capture_output=True, text=True, timeout=30)
        if reverse.returncode:
            raise RuntimeError('SDL fixture is neither pristine nor the exact applied patch:\n' + forward.stdout + reverse.stdout)
        print('SDL fixture already contains the exact patch; verified reverse dry-run --fuzz=0', flush=True)
    results.append({'case': 'sdl-patch-pristine-or-exact-applied', 'mode': 'patch-dry-run', 'passed': True})
    source = dest.read_text()
    start = source.index('    if (!SDL_HIDAPI_discovery.m_bCanGetNotifications ||')
    end = source.index('\n#if defined(__WIN32__)', start)
    gate = source[start:end]
    harness = q / 'discovery.c'
    harness.write_text('''#include <assert.h>
#include <stdint.h>
typedef uint32_t Uint32;
#define SDL_FALSE 0
#define SDL_TICKS_PASSED(a,b) ((int32_t)((a)-(b)) >= 0)
static struct { int m_bCanGetNotifications; Uint32 m_unLastDetect, m_unDeviceChangeCounter; } SDL_HIDAPI_discovery;
static int enabled;
static Uint32 tick;
static int SDL_GetHintBoolean(const char *name, int fallback) { (void)name; (void)fallback; return enabled; }
static Uint32 SDL_GetTicks(void) { return tick; }
static void discover(void) {
''' + gate + '''}
int main(void) {
 SDL_HIDAPI_discovery.m_bCanGetNotifications=1;
 tick=1000; discover(); assert(!SDL_HIDAPI_discovery.m_unDeviceChangeCounter);
 enabled=1; discover(); assert(SDL_HIDAPI_discovery.m_unDeviceChangeCounter==1);
 tick=1249; discover(); assert(SDL_HIDAPI_discovery.m_unDeviceChangeCounter==1);
 tick=1250; discover(); assert(SDL_HIDAPI_discovery.m_unDeviceChangeCounter==2);
 enabled=0; tick=5000; discover(); assert(SDL_HIDAPI_discovery.m_unDeviceChangeCounter==2);
}
''')
    for mode in (['host'] if args.host_only else ['host', 'mips']):
        binary = q / ('discovery-' + mode)
        cc = 'gcc' if mode == 'host' else B / 'host/bin/mipsel-buildroot-linux-musl-gcc'
        run([cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', harness, '-o', binary])
        run([binary] if mode == 'host' else ['qemu-mipsel', '-cpu', '74Kf', '-L', sdk, binary])
        results.append({'case': 'sdl-usbfs-discovery', 'mode': mode, 'passed': True})
    driver_text = driver.read_text()
    start = driver_text.index('#ifdef __LINUX__', driver_text.index('static void HIDAPI_SetupDeviceDriver('))
    end = driver_text.index('#endif', start) + len('#endif')
    ownership = q / 'ownership.c'
    ownership.write_text('''#define __LINUX__ 1
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#define SDL_FALSE 0
#define SDL_getenv getenv
typedef int SDL_bool;
typedef struct { int driver; } SDL_HIDAPI_Device;
static int daemon_hint, claims, cleanups;
static int SDL_GetHintBoolean(const char *name, int fallback) { (void)name; (void)fallback; return daemon_hint; }
static void HIDAPI_CleanupDeviceDriver(SDL_HIDAPI_Device *device) { device->driver=0; ++cleanups; }
static void setup(SDL_HIDAPI_Device *device, SDL_bool *removed) {
 *removed=SDL_FALSE;
''' + driver_text[start:end] + '''
 ++claims; device->driver=1;
}
int main(int argc, char **argv) {
 assert(argc==2); assert(!setenv("LILSMOKE_GAMEPAD_STATE",argv[1],1)); unlink(argv[1]);
 SDL_HIDAPI_Device device={0}; SDL_bool removed=1;
 setup(&device,&removed); assert(claims==1 && device.driver && !removed);
 FILE *file=fopen(argv[1],"wb"); assert(file); fclose(file);
 setup(&device,&removed); assert(claims==1 && !device.driver && cleanups==1);
 setup(&device,&removed); assert(claims==1 && cleanups==1);
 daemon_hint=1; setup(&device,&removed); assert(claims==2 && device.driver);
 daemon_hint=0; setup(&device,&removed); assert(claims==2 && !device.driver && cleanups==2);
 unlink(argv[1]); setup(&device,&removed); assert(claims==3 && device.driver);
}
''')
    for mode in (['host'] if args.host_only else ['host', 'mips']):
        binary = q / ('ownership-' + mode)
        cc = 'gcc' if mode == 'host' else B / 'host/bin/mipsel-buildroot-linux-musl-gcc'
        run([cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', ownership, '-o', binary])
        command = [binary, q / 'ownership-state']
        run(command if mode == 'host' else ['qemu-mipsel', '-cpu', '74Kf', '-L', sdk, *command])
        results.append({'case': 'sdl-daemon-usb-ownership-legacy-consumers', 'mode': mode, 'passed': True})
    # Verify header-only class-layout changes cannot leave an old adapter
    # allocation size in input.o when only common-input.o gets recompiled.
    dep = q / 'dependency-fixture'
    dep.mkdir(exist_ok=True)
    for name in ('src/input.cpp', 'src/od-sdl.h', 'src/input-wire.h', 'input/Input.cpp', 'input/Keymap.cpp',
                 'input/Input.h', 'input/Keymap.h', 'input/gamepad-state.h',
                 'input.o', 'common-input.o', 'common-keymap.o'):
        path = dep / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch()
        stamp = 1700000000 if name.endswith('.o') else (1800000000 if name.endswith('.h') and name.startswith('input/') else 1600000000)
        os.utime(path, (stamp, stamp))
    dry = run(['make', '-n', '-f', P / 'package/lilsmoke-od-sdl/Makefile', 'input.o', 'common-input.o', 'common-keymap.o',
               'SOURCE=' + str(dep) + '/', 'INPUT=' + str(dep / 'input')], cwd=dep, capture_output=True, text=True)
    assert dry.stdout.count(' -c ') == 3, dry.stdout
    results.append({'case': 'apps-input-header-dependencies', 'mode': 'make-dry-run', 'passed': True})
    for variant in ('canonical', 'staging-alias'):
        base = q / 'metadata' / variant
        stage = base / 'host/mipsel-buildroot-linux-musl/sysroot'
        embedded = str((stage if variant == 'canonical' else base / 'staging') / 'usr/include/directfb')
        for name in ('usr/lib/pkgconfig/sdl2.pc', 'usr/bin/sdl2-config'):
            path = stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            content = (sdk / name).read_text()
            for prefix in (str(B / 'staging'), str((B / 'staging').resolve())):
                content = content.replace(prefix + '/usr/include/directfb', embedded)
            content = content.replace('${includedir}/directfb', embedded).replace('${prefix}/include/directfb', embedded)
            path.write_text(content)
        metadata_make = base / 'metadata.mk'
        metadata_make.write_text('BR2_PACKAGE_LILSMOKE_USB_GAMEPADS=y\nSTAGING_DIR=' + str(stage) + '\nBASE_DIR=' + str(base) +
            '\nSED=sed -i -e\ninclude ' + str(P / 'package/lilsmoke-usb-gamepads/lilsmoke-usb-gamepads.mk') +
            '\ncheck:\n\t$(LILSMOKE_USB_GAMEPADS_FIX_SDL2_STAGING_METADATA)\n')
        run(['make', '-s', '-f', metadata_make, 'check'])
        fixed = (stage / 'usr/lib/pkgconfig/sdl2.pc').read_text()
        assert '${includedir}/directfb' in fixed and str(base) not in fixed
        run(['make', '-s', '-f', metadata_make, 'check'])
        assert fixed == (stage / 'usr/lib/pkgconfig/sdl2.pc').read_text()
        flags = run(['pkg-config', '--cflags', 'sdl2'], capture_output=True, text=True,
                    env=dict(os.environ, PKG_CONFIG_SYSROOT_DIR=str(stage), PKG_CONFIG_LIBDIR=str(stage / 'usr/lib/pkgconfig'))).stdout
        assert '-I' + str(stage / 'usr/include/directfb') in flags
        assert str(stage) + str(stage) not in flags
        flags = run(['sh', stage / 'usr/bin/sdl2-config', '--cflags'], capture_output=True, text=True).stdout
        assert (stage / 'usr/include/directfb').resolve() in [Path(flag[2:]).resolve() for flag in shlex.split(flags) if flag.startswith('-I')]
    results.append({'case': 'sdl2-staging-metadata-sysroot-idempotence', 'mode': 'fixture-make-pkgconfig', 'passed': True})
    if not args.host_only:
        # Compile the complete patched input translation units against the real
        # pinned SDL configuration, without linking/rebuilding the SDL library.
        for source in (dest, driver):
            includes = [sdl_source / 'include', sdl_source / 'src',
                        sdl_source / 'src/hidapi', sdl_source / 'src/joystick/hidapi',
                        sdk / 'usr/include/libusb-1.0']
            run([B / 'host/bin/mipsel-buildroot-linux-musl-gcc', '-std=gnu11', '-O2', '-fPIC',
                 '-D_LARGEFILE_SOURCE', '-D_LARGEFILE64_SOURCE', '-D_FILE_OFFSET_BITS=64',
                 *['-I' + str(path) for path in includes], '-c', source, '-o', q / (source.stem + '.o')])
            results.append({'case': 'patched-' + source.stem + '-translation-unit', 'mode': 'mips-object', 'passed': True})
    (q / 'report.json').write_text(json.dumps({'passed': True, 'abi': 1, 'wire_bytes': 424,
        'single_job': True, 'sdk_or_target_modified': False, 'physical_card_access': False,
        'physical_hotplug_tested': False, 'tests': results}, indent=2) + '\n')
    print('PASS isolated controller hotplug helpers:', q)


if __name__ == '__main__':
    main()
