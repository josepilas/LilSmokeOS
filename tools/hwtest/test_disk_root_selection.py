import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from mips_disk_fixture import overlay_initramfs


class RecordingArchive:
    def __init__(self): self.entries = []; self.files = {}
    def dir(self, *_): pass
    def symlink(self, *_): pass
    def file(self, name, data, *_): self.files[name] = data
    def build(self): return self.files['init']


class RootSelection(unittest.TestCase):
    def source(self, **kwargs):
        with patch('mips_disk_fixture.Newc', RecordingArchive), patch.object(Path, 'read_bytes', return_value=b'fixture'):
            return overlay_initramfs(Path('/fixture'), SimpleNamespace(entries=[]), **kwargs)

    def test_default_virtio_root_is_preserved(self):
        source = self.source()
        self.assertIn('rootdev=/dev/vda', source)
        self.assertIn('mount -t squashfs -o ro "$rootdev" /os', source)

    def test_ide_root_uses_block_storage_not_ram(self):
        source = self.source(root_device='/dev/sda')
        self.assertIn('rootdev=/dev/sda', source)
        self.assertIn('while [ ! -b "$rootdev" ]', source)
        self.assertNotIn('mount -t squashfs -o loop,ro /rootfs.squashfs', source)

    def test_partition_image_tracks_selected_root(self):
        source = self.source(root_device='/dev/sda', sdcard=True)
        self.assertIn('"${rootdev}1" /lilos', source)

    def test_arbitrary_shell_device_text_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'root device'):
            self.source(root_device='/dev/sda; reboot')

    def test_ext2_root_remains_readonly_block_backed(self):
        source = self.source(root_device='/dev/sda', root_fstype='ext2')
        self.assertIn('mount -t ext2 -o ro "$rootdev" /os', source)
        self.assertNotIn('rootfs.squashfs', source)

    def test_alternate_root_cannot_silently_change_sd_or_ram_tests(self):
        for option in ('sdcard', 'rootfs_in_ram'):
            with self.assertRaisesRegex(ValueError, 'standalone block image'):
                self.source(root_fstype='ext2', **{option: True})
        with self.assertRaisesRegex(ValueError, 'root filesystem'):
            self.source(root_fstype='ext2; reboot')


if __name__ == '__main__': unittest.main()
