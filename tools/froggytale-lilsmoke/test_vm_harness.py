#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Asset-free VM harness checks; does not start QEMU or create disk images."""
from pathlib import Path
import errno
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import test_vm


class HarnessTests(unittest.TestCase):
    def test_fat32_init(self):
        init=test_vm.guest_init('data.win',120,True,'fat32')
        subprocess.run(['sh','-n'],input=init,text=True,check=True)
        self.assertEqual(init.count('mount -t vfat '),2)
        self.assertIn('UNDERTALE_VM_FAT32_REMOUNTED',init)
        self.assertIn('FROGGY_CACHE_DIR=/cache-disk/cache FROGGY_SAVE_DIR=/cache-disk/saves',init)
        self.assertIn('export FROGGY_HEADLESS_AUDIO=1',init)
        self.assertIn('SDL_AUDIODRIVER=dummy FROGGY_AUDIO_MANUAL=0',init)
        self.assertIn('ulimit -v 114688',init)
        self.assertIn('--exit-at-frame 120',init)
        self.assertNotIn('CACHE_',init.replace('FROGGY_CACHE_DIR',''))
        self.assertLess(init.index('run_case first'),init.index('umount /cache-disk'))
        self.assertLess(init.index('UNDERTALE_VM_FAT32_REMOUNTED'),init.index('run_case cached'))
        for tag,target in (('owner','original'),('program','program')):
            self.assertIn(f'version=9p2000.L,ro {tag} /{target}',init)

    def test_9p_compatibility(self):
        init=test_vm.guest_init('data.win',60,False,'9p')
        subprocess.run(['sh','-n'],input=init,text=True,check=True)
        self.assertNotIn('mount -t vfat',init)
        self.assertIn('FROGGY_CACHE_DIR=/bench/cache',init)
        self.assertIn('unset FROGGY_HEADLESS_AUDIO',init)

    def test_rejects_shell_filename(self):
        with self.assertRaises(ValueError):
            test_vm.guest_init('data.win; poweroff',60,True,'fat32')

    def test_rejects_ntfs_scratch_before_creation(self):
        with patch('test_vm.subprocess.check_output',return_value='9p\n'),patch.object(Path,'open') as opened:
            with self.assertRaisesRegex(RuntimeError,'Linux disk filesystem'):
                test_vm.create_cache_disk('/mnt/c')
            opened.assert_not_called()

    def test_sparse_fat32_creation(self):
        with patch('test_vm.subprocess.check_output',return_value='ext4\n'),patch.object(Path,'open') as opened,patch('test_vm.subprocess.run') as run:
            image,fs=test_vm.create_cache_disk('/tmp/example')
            self.assertEqual(fs,'ext4')
            self.assertEqual(image,Path('/tmp/example/cache-fat32.img'))
            opened.assert_called_once_with('xb')
            opened.return_value.__enter__.return_value.truncate.assert_called_once_with(256*1024*1024)
            self.assertEqual(run.call_args.args[0][:5],['mkfs.fat','-F','32','-n','FTCACHE'])

    def test_completed_case_survives_later_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            out=Path(tmp)
            for name,text in {'first.exit':'0\n','first-start.txt':'1.00 0\n',
                'first-end.txt':'4.25 0\n','first-memory.log':'VmPeak:\t8000 kB\nVmHWM:\t5000 kB\nVmPeak:\t9000 kB\n'}.items():
                (out/name).write_text(text)
            self.assertEqual(test_vm.collect_cases(out),[dict(name='first',exit_code=0,
                elapsed_seconds=3.25,peak_as_kib=9000,peak_rss_kib=5000)])

    def test_transient_live_serial_read(self):
        report={}
        with patch.object(Path,'read_text',side_effect=OSError(errno.ENODATA,'No data available')):
            self.assertEqual(test_vm.active_serial(Path('serial.log'),report),'')
        self.assertEqual(report,dict(serial_poll_retries=1))
        with patch.object(Path,'read_text',side_effect=OSError(errno.EIO,'I/O error')):
            with self.assertRaises(OSError):test_vm.active_serial(Path('serial.log'),report)


if __name__=='__main__':unittest.main()
