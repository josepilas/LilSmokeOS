# SPDX-License-Identifier: MPL-2.0
import hashlib
import errno
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

import cook


def executable():
    header = bytearray(128)
    header[:7] = b'\x7fELF\x01\x01\x01'
    struct.pack_into('<HH', header, 16, 2, 8)
    struct.pack_into('<I', header, 28, 52)
    struct.pack_into('<HH', header, 42, 32, 1)
    loader = b'/lib/ld-musl-mipsel.so.1\0'
    struct.pack_into('<IIIII', header, 52, 3, 84, 0, 0, len(loader))
    header[84:84+len(loader)] = loader
    return bytes(header)


@unittest.skipUnless(os.name == 'posix', 'device-only transactions require POSIX; run these in WSL or MIPS Linux')
class Tests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.app = Path(self.temporary.name) / 'Test App'
        self.original = self.app / 'original'
        self.original.mkdir(parents=True)
        self.raw = b'\x80\x37\x12\x40' + bytes(range(256)) * ((cook.ROM_BYTES - 4) // 256) + bytes(252)
        assert len(self.raw) == cook.ROM_BYTES
        self.sha1 = hashlib.sha1(self.raw).hexdigest()
        self.rom = self.original / 'any name no extension'
        self.rom.write_bytes(self.raw)

    def tearDown(self):
        self.temporary.cleanup()

    def test_silent_process_ticks_without_inventing_completed_work(self):
        ticks, lines = [], []
        cook.run_stage([sys.executable, '-c', 'import time; time.sleep(1.2)'],
            self.app, self.app / 'silent.log', lines.append, on_tick=lambda: ticks.append(True))
        self.assertGreaterEqual(len(ticks), 2)
        self.assertEqual(lines, [])

    def builder(self, stage, progress, cancel):
        self.assertEqual((stage / 'baserom.us.z64').read_bytes(), self.raw)
        progress('Extracting assets', 50, 1024)
        (stage / 'game').write_bytes(executable())
        return stage / 'game', 1024

    def prepare(self, **kwargs):
        # Synthetic transaction fixtures are not gameplay or ROM-support proofs.
        actual = cook.discover
        def discover(folder, cancel=lambda: False):
            return actual(folder, cancel, expected=self.sha1)
        with patch.object(cook, 'US_SHA1', self.sha1), patch.object(cook, 'discover', discover):
            return cook.Cache(self.app, 'test-recipe').prepare(self.builder, lambda *args: None, **kwargs)

    def test_all_byte_orders_and_arbitrary_name(self):
        for order in (1, 2, 4):
            if order == 1:
                raw = self.raw
            else:
                raw = bytearray(len(self.raw))
                for at in range(order):
                    raw[at::order] = self.raw[order-at-1::order]
            self.rom.write_bytes(raw)
            rom = cook.discover(self.original, expected=self.sha1)
            try:
                target = self.app / ('normalized-' + str(order))
                rom.write_normalized(target, lambda *args: None)
                self.assertEqual(target.read_bytes(), self.raw)
            finally:
                rom.close()

    def test_reject_wrong_rom(self):
        with self.assertRaisesRegex(ValueError, 'Unsupported ROM'):
            cook.discover(self.original)
        self.assertTrue(self.rom.exists())

    def test_reject_ambiguous_roms(self):
        (self.original / 'second').write_bytes(self.raw)
        with self.assertRaisesRegex(ValueError, 'exactly one'):
            cook.discover(self.original, expected=self.sha1)

    @unittest.skipUnless(os.name == 'posix', 'device symlink semantics')
    def test_reject_rom_symlink(self):
        self.rom.rename(self.app / 'outside')
        self.rom.symlink_to(self.app / 'outside')
        with self.assertRaises(ValueError):
            cook.discover(self.original, expected=self.sha1)

    def test_success_deletes_only_app_copy_and_restarts(self):
        outside = self.app.parent / 'Downloads copy.z64'
        outside.write_bytes(self.raw)
        game = self.prepare()
        self.assertTrue(game.is_file())
        self.assertFalse(self.rom.exists())
        self.assertEqual(outside.read_bytes(), self.raw)
        with patch.object(cook, 'US_SHA1', self.sha1):
            cache = cook.Cache(self.app, 'test-recipe')
            self.assertEqual(cache.current(), game)
            self.assertEqual(cache.prepare(lambda *args: self.fail('unexpected rebuild'), lambda *args: None), game)

    def test_failure_keeps_original_and_does_not_commit(self):
        def failure(*args):
            raise RuntimeError('compiler failed')
        self.builder = failure
        with self.assertRaisesRegex(RuntimeError, 'compiler failed'):
            self.prepare()
        self.assertTrue(self.rom.exists())
        self.assertFalse((self.app / '.sm64-cache/current.json').exists())

    def test_builder_can_move_private_normalized_rom(self):
        original_builder = self.builder
        def moved(stage, progress, cancel):
            result = original_builder(stage, progress, cancel)
            source = stage / 'source'
            source.mkdir()
            (stage / 'baserom.us.z64').rename(source / 'baserom.us.z64')
            # NativeBuild removes the scratch source after final linkage.
            (source / 'baserom.us.z64').unlink()
            source.rmdir()
            return result
        self.builder = moved
        game = self.prepare()
        self.assertTrue(game.is_file())
        self.assertFalse(self.rom.exists())

    def test_cancel_keeps_original(self):
        with self.assertRaises(cook.Cancelled):
            self.prepare(cancel=lambda: True)
        self.assertTrue(self.rom.exists())

    def test_corrupt_cache_requires_rebuild(self):
        game = self.prepare(delete_original=False)
        game.write_bytes(b'broken')
        with patch.object(cook, 'US_SHA1', self.sha1):
            self.assertIsNone(cook.Cache(self.app, 'test-recipe').current())
        repaired = self.prepare()
        self.assertNotEqual(game, repaired)

    def test_recipe_change_invalidates_cache(self):
        self.prepare(delete_original=False)
        with patch.object(cook, 'US_SHA1', self.sha1):
            self.assertIsNone(cook.Cache(self.app, 'new-recipe').current())

    def test_interrupted_pointer_keeps_previous_cache(self):
        game = self.prepare(delete_original=False)
        (self.app / '.sm64-cache/working-aborted').mkdir()
        with patch.object(cook, 'US_SHA1', self.sha1):
            self.assertEqual(cook.Cache(self.app, 'test-recipe').current(), game)

    def test_commit_then_crash_can_complete_rom_deletion(self):
        game = self.prepare(delete_original=False)
        self.assertTrue(self.rom.exists())
        self.assertEqual(self.prepare(), game)
        self.assertFalse(self.rom.exists())

    def test_wrong_machine_and_glibc_rejected(self):
        path = self.app / 'game'
        data = bytearray(executable())
        struct.pack_into('<H', data, 18, 62)
        path.write_bytes(data)
        with self.assertRaises(ValueError):
            cook.verify_mips_executable(path)
        data = bytearray(executable())
        data[84:84+len(b'/lib/ld-musl-mipsel.so.1\0')] = b'/lib/ld.so.1\0'.ljust(len(b'/lib/ld-musl-mipsel.so.1\0'), b'\0')
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'musl'):
            cook.verify_mips_executable(path)

    @unittest.skipUnless(os.name == 'posix', 'device process group semantics')
    def test_timeout_terminates_build_and_keeps_rom(self):
        with self.assertRaisesRegex(RuntimeError,'timed out'):
            cook.run_stage([sys.executable,'-c','import time; time.sleep(20)'],self.app,
                self.app/'timeout.log',lambda line:None,timeout=0.1)
        self.assertTrue(self.rom.exists())

    def test_missing_flock_uses_releasable_ram_lock(self):
        import fcntl
        locks=self.app/'ram-locks'
        with (patch.object(fcntl,'flock',side_effect=OSError(errno.ENOSYS,'unsupported')),
              patch.dict(os.environ,{'LILSMOKE_SM64_LOCK_ROOT':str(locks)})):
            game=self.prepare(delete_original=False)
            self.assertTrue(game.is_file())
            self.assertEqual(list(locks.iterdir()),[])

    def test_missing_epoll_falls_back_to_poll(self):
        import selectors
        lines=[]
        with patch.object(selectors,'DefaultSelector',side_effect=OSError(errno.ENOSYS,'unsupported')):
            cook.run_stage([sys.executable,'-c','print("poll fallback")'],self.app,
                self.app/'poll.log',lines.append)
        self.assertEqual(lines,['poll fallback'])

    def test_selector_setup_failure_reaps_started_process(self):
        import selectors
        actual=cook.subprocess.Popen
        children=[]
        def spawn(*args,**kwargs):
            child=actual(*args,**kwargs);children.append(child);return child
        with (patch.object(cook.subprocess,'Popen',side_effect=spawn),
              patch.object(selectors,'DefaultSelector',side_effect=OSError(errno.ENOMEM,'allocation failed'))):
            with self.assertRaises(OSError):
                cook.run_stage([sys.executable,'-c','import time;time.sleep(20)'],self.app,
                    self.app/'broken-selector.log',lambda line:None)
        self.assertEqual(len(children),1)
        self.assertIsNotNone(children[0].poll())

    @unittest.skipUnless(os.name == 'posix', 'device process group semantics')
    def test_cancel_terminates_build_process(self):
        cancel = threading.Event()
        timer = threading.Timer(0.2, cancel.set)
        timer.start()
        started = time.monotonic()
        try:
            with self.assertRaises(cook.Cancelled):
                cook.run_stage([sys.executable, '-c', 'import time; time.sleep(20)'], self.app,
                    self.app / 'build.log', lambda line: None, cancel.is_set)
            self.assertLess(time.monotonic() - started, 4)
        finally:
            timer.cancel()


if __name__ == '__main__':
    unittest.main()
