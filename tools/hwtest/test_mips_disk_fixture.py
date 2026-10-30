"""Check generated initramfs headers and isolation of the RAM-root QA option."""
from pathlib import Path
import unittest
from unittest.mock import patch

from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs


def unpack(data):
    entries = {}
    offset = 0
    while True:
        assert data[offset:offset + 6] == b'070701'
        fields = [int(data[offset + 6 + i * 8:offset + 14 + i * 8], 16) for i in range(13)]
        size, namesize = fields[6], fields[11]
        offset += 110
        name = data[offset:offset + namesize - 1].decode()
        assert data[offset + namesize - 1] == 0
        offset = (offset + namesize + 3) & ~3
        payload = data[offset:offset + size]
        assert len(payload) == size
        offset = (offset + size + 3) & ~3
        if name == 'TRAILER!!!':
            return entries
        entries[name] = payload


class FixtureTests(unittest.TestCase):
    def build(self, **options):
        fixture = Newc()
        fixture.file('usr/bin/bench-init', '#!/bin/sh\necho complete\n', 0o755)
        with patch.object(Path, 'read_bytes', return_value=b'unchanged SDK image'):
            return unpack(overlay_initramfs(Path('/sdk'), fixture, **options))

    def test_entrypoint_and_budget_lengths(self):
        for budget in (8, 16, 32):
            with self.subTest(budget=budget):
                entries = self.build(entrypoint='/usr/bin/bench-init', overlay_mib=budget, trace=True)
                self.assertIn(f'size={budget}M,mode=0755'.encode(), entries['init'])
                self.assertTrue(entries['init'].endswith(b'/usr/bin/bench-init\n'))
                self.assertEqual(entries['qa/usr/bin/bench-init'], b'#!/bin/sh\necho complete\n')
                self.assertNotIn('rootfs.squashfs', entries)

    def test_ram_root_uses_same_image_without_waiting_for_vda(self):
        entries = self.build(rootfs_in_ram=True)
        self.assertEqual(entries['rootfs.squashfs'], b'unchanged SDK image')
        self.assertIn(b'-o loop,ro /rootfs.squashfs /os', entries['init'])
        self.assertNotIn(b'while [ ! -b "$rootdev" ]', entries['init'])

    def test_sd_contract_remains_a_real_partition_mount(self):
        entries = self.build(sdcard=True)
        self.assertIn(b'rootdev=/dev/vda\n', entries['init'])
        self.assertIn(b'"${rootdev}1" /lilos', entries['init'])
        self.assertIn(b'/lilos/lilsmoke/slots/A/rootfs.squashfs', entries['init'])
        self.assertNotIn('rootfs.squashfs', entries)

    def test_invalid_combinations_are_rejected(self):
        for options in ({'rootfs_in_ram': True, 'sdcard': True}, {'overlay_mib': 128},
                        {'entrypoint': '/bin/sh; reboot'}):
            with self.subTest(options=options), self.assertRaises(ValueError):
                self.build(**options)


if __name__ == '__main__':
    unittest.main()
