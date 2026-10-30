# SPDX-License-Identifier: MPL-2.0
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

from cook import Cancelled
from native_build import prepare_build_clock

SDK = Path('/root/lilsmoke-os/build/buildroot/output')
HOST = Path(os.environ.get('SM64_BUILD_SHELL_HOST', '/root/sm64-clock-tools-20261016/build-shell-host'))
MIPS = Path(os.environ.get('SM64_BUILD_SHELL_MIPS', '/root/sm64-clock-tools-20261016/sm64-build-shell'))
FUTURE = 1893456000  # 2030, beyond the host's clock and all M8 FAT inputs.


class BuildClockTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='clock space ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'build/original-mips').mkdir(parents=True)
        self.output = self.root / 'build/original-mips/result'

    def runners(self):
        self.assertTrue(HOST.is_file(), 'Build build_shell.sh before these tests')
        self.assertTrue(MIPS.is_file(), 'Build build_shell.sh before these tests')
        return ((str(HOST),), ('qemu-mipsel', '-cpu', '74Kf', '-L', str(SDK / 'staging'), str(MIPS)))

    def run_shell(self, runner, command, target='build/original-mips/result', epoch=str(FUTURE + 4), legacy=False):
        env = dict(os.environ, LILSMOKE_BUILD_TARGET=target, LILSMOKE_BUILD_EPOCH=epoch)
        if legacy:
            env['LD_PRELOAD'] = str(HOST.parent / ('legacy-mips.so' if runner[0] == 'qemu-mipsel' else 'legacy-host.so'))
        return subprocess.run([*runner, '-c', command], cwd=self.root, env=env,
                              capture_output=True, text=True, timeout=5)

    def test_native_host_and_mips_success_stamp(self):
        for runner in self.runners():
            result = self.run_shell(runner, 'printf verified > build/original-mips/result')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(self.output.read_text(), 'verified')
            self.assertEqual(int(self.output.stat().st_mtime), FUTURE + 4)

    def test_failed_command_keeps_status_and_timestamp(self):
        self.output.write_text('previous')
        os.utime(self.output, (100, 100))
        for runner in self.runners():
            result = self.run_shell(runner, 'exit 23')
            self.assertEqual(result.returncode, 23)
            self.assertEqual(int(self.output.stat().st_mtime), 100)

    def test_legacy_kernel_syscall_uses_the_o32_time_layout(self):
        for runner in self.runners():
            result = self.run_shell(runner, 'printf legacy > build/original-mips/result', legacy=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('SM64_TEST_FUTIMENS_ENOSYS', result.stderr)
            self.assertIn('SM64_TEST_FUTIMES_ENOSYS', result.stderr)
            self.assertEqual(int(self.output.stat().st_mtime), FUTURE + 4)
            self.assertEqual(self.output.read_text(), 'legacy')

    def test_intermediate_commands_and_make_probes_need_no_output(self):
        for runner in self.runners():
            self.assertEqual(self.run_shell(runner, 'true').returncode, 0)
            result = self.run_shell(runner, 'printf probe', target='')
            self.assertEqual((result.returncode, result.stdout), (0, 'probe'))

    def test_unsafe_links_and_target_traversal_are_rejected(self):
        outside = self.root / 'protected'
        outside.write_text('keep')
        os.utime(outside, (100, 100))
        for runner in self.runners():
            for kind in ('file-link', 'hard-link', 'directory-link', 'traversal'):
                with self.subTest(runner=runner, kind=kind):
                    if kind == 'file-link':
                        self.output.symlink_to(outside)
                        target = 'build/original-mips/result'
                    elif kind == 'hard-link':
                        os.link(outside, self.output)
                        target = 'build/original-mips/result'
                    elif kind == 'directory-link':
                        self.output.symlink_to(self.root, target_is_directory=True)
                        target = 'build/original-mips/result/protected'
                    else:
                        target = 'build/original-mips/../../protected'
                    self.assertEqual(self.run_shell(runner, 'true', target).returncode, 74)
                    self.assertEqual(int(outside.stat().st_mtime), 100)
                    self.output.unlink(missing_ok=True)

    def test_invalid_epoch_fails_before_command_execution(self):
        for runner in self.runners():
            for value in ('', '0', '-1', '99999999999999', '2030x'):
                result = self.run_shell(runner, 'touch build/original-mips/result', epoch=value)
                self.assertEqual(result.returncode, 77)
                self.assertFalse(self.output.exists())

    def test_future_include_restarts_before_fix_but_not_after(self):
        (self.root / 'rules.in').write_text('QA_INCLUDED := yes\n')
        os.utime(self.root / 'rules.in', (FUTURE, FUTURE))
        (self.root / 'Makefile').write_text('''all: build/original-mips/result
build/original-mips/level_rules.mk: rules.in
\tcat $< > $@
include build/original-mips/level_rules.mk
build/original-mips/result:
\tprintf '$(QA_INCLUDED)' > $@
''')
        with self.assertRaises(subprocess.TimeoutExpired):
            subprocess.run(['make', '-n'], cwd=self.root, capture_output=True, timeout=0.2)
        helper = self.root / 'build/original-mips/sm64-build-shell'
        shutil.copyfile(HOST, helper)
        helper.chmod(0o755)
        (self.root / 'clock.mk').write_text('''SHELL := build/original-mips/sm64-build-shell
.SHELLFLAGS := -c
export LILSMOKE_BUILD_TARGET = $@
''')
        env = dict(os.environ, LILSMOKE_BUILD_EPOCH=str(FUTURE + 4))
        for dry_run in (True, False):
            command = ['make', '-f', 'Makefile', '-f', 'clock.mk'] + (['-n'] if dry_run else [])
            result = subprocess.run(command, cwd=self.root, env=env, capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertLessEqual(result.stdout.count('cat rules.in'), 1)
        self.assertEqual(self.output.read_text(), 'yes')
        result = subprocess.run(['make', '-f', 'Makefile', '-f', 'clock.mk'], cwd=self.root,
                                env=env, capture_output=True, text=True, timeout=5)
        self.assertNotIn('cat rules.in', result.stdout)
        self.assertIn('Nothing to be done', result.stdout)

    def test_clock_setup_uses_source_dates_not_current_clock(self):
        work = self.root / 'source'
        (work / 'build/original-mips').mkdir(parents=True)
        (work / 'input').write_text('source')
        os.utime(work / 'input', (FUTURE, FUTURE))
        runtime = self.root / 'runtime'
        (runtime / 'bin').mkdir(parents=True)
        shutil.copyfile(MIPS, runtime / 'bin/sm64-build-shell')
        shutil.copyfile(Path(__file__).with_name('sound_data.S'), runtime / 'sound_data.S')
        before = (work / 'input').read_bytes()
        clock = prepare_build_clock(self.root, work, runtime)
        self.assertEqual(int(os.environ['LILSMOKE_BUILD_EPOCH']), FUTURE + 4)
        self.assertIn('export LILSMOKE_BUILD_TARGET = $@', clock.read_text())
        self.assertIn('$(CC) -nostdinc -mips32r2', clock.read_text())
        self.assertEqual(before, (work / 'input').read_bytes())
        self.assertEqual(int((work / 'input').stat().st_mtime), FUTURE)

    def test_clock_setup_cancellation_and_unrepresentable_dates(self):
        work = self.root / 'source'
        work.mkdir()
        (work / 'input').write_text('source')
        with self.assertRaises(Cancelled):
            prepare_build_clock(self.root, work, self.root / 'unused', lambda: True)
        os.utime(work / 'input', (2200000000, 2200000000))
        with self.assertRaisesRegex(ValueError, 'range'):
            prepare_build_clock(self.root, work, self.root / 'unused')


if __name__ == '__main__':
    unittest.main()
