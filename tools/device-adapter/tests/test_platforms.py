import copy
import json
import os
from pathlib import Path
import plistlib
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import adapter as a


class PlatformIdentityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.target = Path(self.temp.name) / "LILOS"
        self.target.mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def mac_probe(self, whole_updates=None, part_updates=None):
        part = dict(MountPoint=str(self.target), VolumeName="LILOS", FilesystemType="msdos",
                    VolumeUUID="part-uuid", DeviceIdentifier="disk5s1", ParentWholeDisk="disk5",
                    TotalSize=1610612736, PartitionMapPartitionOffset=1048576)
        whole = dict(DeviceIdentifier="disk5", TotalSize=64000000000, MediaUUID="whole-media-uuid", Internal=False)
        part.update(part_updates or {})
        whole.update(whole_updates or {})
        listing = dict(AllDisksAndPartitions=[dict(Partitions=[dict(DeviceIdentifier="disk5s1", VolumeName="LILOS"),
                                                               dict(DeviceIdentifier="disk5s2", VolumeName="LILROMS")])])
        def run(args):
            if args[1] == "list":
                return plistlib.dumps(listing)
            if args[-1] == "disk5":
                return plistlib.dumps(whole)
            if args[-1] == "disk5s2":
                return plistlib.dumps(dict(FilesystemType="exfat"))
            return plistlib.dumps(part)
        return a.macos_identity(self.target, run)

    def gate(self, value):
        probe = lambda p: value
        with patch.object(a, "identity", probe):
            return a.target_identity(self.target, dict(disk_id="pc-stock-disk"), probe)

    def test_macos_complete_identity(self):
        result = self.mac_probe()
        self.assertEqual(result["disk_id"], "mac:whole-media-uuid")
        self.assertEqual(result["offset"], 1048576)
        self.assertEqual(result["bytes"], 1610612736)
        self.assertEqual(result["whole_bytes"], 64000000000)
        self.assertEqual(result["partition_count"], 2)
        self.gate(result)

    def test_macos_incomplete_identity_export_only(self):
        for whole, part in ((dict(MediaUUID=""), {}), ({}, dict(PartitionMapPartitionOffset=0)),
                            ({}, dict(TotalSize=0)), (dict(TotalSize=0), {}), (dict(Internal=True), {})):
            result = self.mac_probe(whole, part)
            with self.subTest(whole=whole, part=part), self.assertRaises(a.Refusal):
                self.gate(result)

    def test_macos_card_replacement_detected(self):
        wanted = self.mac_probe()
        for key, value in (("disk_id", "mac:other"), ("hardware_id", "other"), ("offset", 2 * 1048576),
                           ("bytes", 2 * 1610612736), ("whole_bytes", 32000000000)):
            changed = copy.deepcopy(wanted)
            changed[key] = value
            with self.subTest(key=key), self.assertRaisesRegex(a.Refusal, "changed during"):
                a.stable_disk(self.target, wanted, lambda p: changed)

    def linux_probe(self, serial="card-serial", start=2048):
        root = Path(self.temp.name) / "mock-sys"
        device_id = "8_1" if os.name == "nt" else "8:1"
        node = root / "dev/block" / device_id
        node.mkdir(parents=True, exist_ok=True)
        part = root / "devices/usb1/host/target/block/sdb/sdb1"
        part.mkdir(parents=True, exist_ok=True)
        for name, value in (("partition", "1"), ("start", str(start)), ("size", "3145728")):
            (part / name).write_text(value)
        disk = part.parent
        for name, value in (("size", "125000000"), ("ro", "0"), ("removable", "1")):
            (disk / name).write_text(value)
        (disk / "device").mkdir(exist_ok=True)
        (disk / "device/serial").write_text(serial)
        mount = str(self.target).replace("\\", "/").replace(" ", "\\040")
        mounts = "25 1 " + device_id + " / " + mount + " rw - vfat /dev/sdb1 rw\n"
        def run(args):
            if args[-1] == "/dev/sdb1":
                return json.dumps(dict(blockdevices=[dict(path="/dev/sdb1", label="LILOS", fstype="vfat")])).encode()
            return json.dumps(dict(blockdevices=[dict(path="/dev/sdb", children=[dict(label="LILOS", fstype="vfat"),
                                                                                   dict(label="LILROMS", fstype="exfat")])])).encode()
        with patch.object(Path, "resolve", return_value=part):
            return a.linux_identity(self.target, mounts, root, run)

    def test_linux_sysfs_identity(self):
        value = self.linux_probe()
        self.assertEqual(value["disk_id"], "linux:card-serial")
        self.assertEqual(value["offset"], 1048576)
        self.assertEqual(value["bytes"], 1610612736)
        self.assertEqual(value["whole_bytes"], 64000000000)
        self.gate(value)

    def test_linux_unknown_serial_or_geometry_refused(self):
        for serial, start in (("", 2048), ("known", 0)):
            with self.subTest(serial=serial, start=start), self.assertRaises(a.Refusal):
                self.gate(self.linux_probe(serial, start))


if __name__ == "__main__":
    unittest.main()
