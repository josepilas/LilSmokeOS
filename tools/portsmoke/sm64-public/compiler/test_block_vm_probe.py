#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Narrow regression tests for the block compiler probe's evidence parser."""
import unittest
import shutil
import subprocess

from block_vm_probe import CPU_FLAGS, INIT, RENDER_FLAGS, parse_telemetry, storage_verified


class BlockEvidenceTests(unittest.TestCase):
    def test_kernel_9p_support_does_not_mean_9p_mount(self):
        serial = ('9p: Installing v9fs 9p2000 file system support\n'
                  'QA_STORAGE_BEGIN\n/dev/vdb /block ext4 rw,noatime 0 0\n'
                  'QA_STORAGE_END\n')
        self.assertTrue(storage_verified(serial))

    def test_active_9p_or_missing_ext4_fails(self):
        self.assertFalse(storage_verified('QA_STORAGE_BEGIN\nQA_STORAGE_END'))
        self.assertFalse(storage_verified('QA_STORAGE_BEGIN\n'
                         '/dev/vdb /block ext4 rw 0 0\n'
                         'compiler /compiler 9p ro 0 0\nQA_STORAGE_END'))

    def test_complete_crlf_samples_only_and_guest_tree_aggregate(self):
        sample = '''QA_SAMPLE_BEGIN label=graphics uptime=42.25 root=100
QA_MEM_BEGIN
MemTotal: 109956 kB
MemAvailable: 60000 kB
QA_MEM_END
QA_PROCESS_BEGIN pid=100
cmdline: /compiler/usr/bin/mipsel-linux-gnu-gcc-12 -O3
wchan: do_wait
stat: 100 (ld.so.1) S 1 0 0
PPid: 1
VmPeak: 3664 kB
VmRSS: 2300 kB
Pss: 2100 kB
QA_PROCESS_END
QA_PROCESS_BEGIN pid=101
cmdline: /compiler/usr/lib/gcc/mipsel-linux-gnu/12/cc1
wchan: 0
stat: 101 (ld.so.1) R 100 0 0
PPid: 100
VmPeak: 40000 kB
VmRSS: 12000 kB
Pss: 11800 kB
QA_PROCESS_END
QA_TREE_SUM rss_kib=14300 pss_kib=13900
QA_SAMPLE_END
QA_SAMPLE_BEGIN label=graphics uptime=43.25 root=100
QA_MEM_BEGIN
'''
        values = parse_telemetry(sample.replace('\n', '\r\n'))
        self.assertEqual(len(values), 1)
        self.assertEqual(values[0]['tree_rss_sum_kib'], 14300)
        self.assertEqual(values[0]['tree_pss_sum_kib'], 13900)
        self.assertEqual(values[0]['meminfo_kib']['MemTotal'], 109956)
        self.assertEqual(values[0]['processes'][1]['VmRSS'], 12000)
        self.assertEqual(values[0]['processes'][0]['wchan'], 'do_wait')

    def test_original_codegen_flags_preserved(self):
        for flag in CPU_FLAGS + ['-O3', '-fno-fast-math', '-fno-strict-aliasing', '-fwrapv']:
            self.assertIn(flag, RENDER_FLAGS)
        self.assertNotIn('--param', RENDER_FLAGS)

    @unittest.skipUnless(shutil.which('sh'), 'POSIX shell unavailable')
    def test_guest_init_shell_syntax(self):
        subprocess.run(['sh', '-n'], input=INIT, text=True, check=True)


if __name__ == '__main__':
    unittest.main()
