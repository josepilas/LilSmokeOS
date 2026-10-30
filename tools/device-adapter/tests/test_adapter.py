import copy
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import adapter as a


class Fixture:
    def __init__(self, base):
        self.base = Path(base)
        self.source, self.bundle, self.target = (self.base / v for v in ("original", "private-export", "LILOS"))
        self.source.mkdir()
        self.target.mkdir()
        self.data = {name: ("synthetic private stock: " + name).encode() for name in a.GATE_FILES}
        self.data["usr/lib/libdisplay.so"] = b"synthetic display dependency"
        self.profile = dict(schema=1, id="synthetic-tested-profile", reference_image_sha256="0" * 64,
                            launch_sha256=[a.digest_bytes(b"synthetic reviewed launcher")],
                            files={k: dict(bytes=len(v), sha256=a.digest_bytes(v)) for k, v in self.data.items()})
        for name, data in self.data.items():
            path = self.source / "cubegm" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        (self.source / "roms").mkdir()
        (self.source / "roms/do-not-read-or-export.sfc").write_bytes(b"private game")
        self.make_target()

    def provider(self, path):
        path = Path(path)
        target = path == self.target or self.target in path.parents
        return dict(root=str(self.target if target else self.base), disk_id="external-card" if target else "pc-source-disk",
                    volume_id="card-partition" if target else "pc-partition", external=target, system=False,
                    label="LILOS" if target else "PC", filesystem="fat32" if target else "ntfs", bytes=1610612736)

    def make_target(self):
        for name, data in self.data.items():
            path = self.target / "cubegm" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        # Deliberately stale vendor file tests a real overwrite/backup.
        (self.target / "cubegm/driver.so").write_bytes(b"old target driver")
        lil = self.target / "lilsmoke"
        (lil / "files").mkdir(parents=True)
        (lil / "files/000000.bin").write_bytes(b"preserved runtime object")
        (lil / "runtime.tsv").write_text("F\t0755\tusr/bin/lilsmoke-ui\t000000.bin\n", encoding="ascii")
        (lil / "layout.conf").write_text("LayoutVersion=2\nBoard=segam-m8-v6-stock\n", encoding="ascii")
        (lil / "m8-launch.sh").write_bytes(b"synthetic reviewed launcher")
        a.write_json(lil / "candidate.json", dict(source_img_sha256=self.profile["reference_image_sha256"],
                     stock_hashes={k: self.profile["files"][k]["sha256"] for k in a.BOOT_FILES}))
        with (self.target / "cubegm/pagefile.sys").open("wb") as stream:
            stream.truncate(128 * 1024 * 1024)
        (lil / "persist").mkdir()
        (lil / "persist/save.dat").write_bytes(b"preserve my save")

    def export(self):
        # Actual mounted disk probing is not bypassed by production CLI.
        original = a.identity
        a.identity = self.provider
        try:
            return a.export_bundle(self.source, self.bundle, self.profile)
        finally:
            a.identity = original

    def plan(self):
        return a.plan_apply(self.bundle, self.target, self.profile, self.provider)

    def apply(self, boundary=None):
        plan = self.plan()
        return a.apply_bundle(self.bundle, self.target, plan["plan_sha256"], self.profile, self.provider, boundary=boundary)

    def snapshot(self):
        # Skip the sparse 128MiB pagefile; all operations must leave it alone.
        return {p.relative_to(self.target).as_posix(): a.hash_file(p)[0] for p in self.target.rglob("*")
                if p.is_file() and p.name != "pagefile.sys" and a.CONTROL not in p.as_posix()}


class AdapterTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.f = Fixture(self.temp.name)

    def tearDown(self):
        if os.name == "nt":
            # tempfile's normal-path rmtree cannot remove the MAX_PATH fixture.
            # This exact directory was allocated by this test, never the global temp root.
            path = a.safe_absolute(self.temp.name)
            self.assertEqual(path.parent, a.safe_absolute(tempfile.gettempdir()))
            a.shutil.rmtree(a.io_path(path))
        self.temp.cleanup()

    def test_inspect_export_source_readonly(self):
        before = {k: a.hash_file(self.f.source / "cubegm" / k) for k in self.f.data}
        result = self.f.export()
        self.assertTrue(result["complete"])
        self.assertEqual(result["file_count"], len(self.f.data))
        self.assertFalse((self.f.bundle / "roms").exists())
        self.assertEqual(before, {k: a.hash_file(self.f.source / "cubegm" / k) for k in self.f.data})

    def test_apply_rollback_and_idempotence(self):
        self.f.export()
        before = self.f.snapshot()
        result = self.f.apply()
        self.assertEqual(result["changed_files"], 3)
        self.assertEqual((self.f.target / "cubegm/icube").read_bytes(), a.HOOK)
        self.assertEqual((self.f.target / "cubegm/icube.stock").read_bytes(), self.f.data["icube"])
        self.assertFalse(result["physical_boot_verified"])
        self.assertEqual(self.f.apply()["status"], "already-adapted")
        a.rollback(self.f.target, result["transaction"], self.f.profile, self.f.provider)
        self.assertEqual(before, self.f.snapshot())
        self.assertEqual((self.f.target / "cubegm/pagefile.sys").stat().st_size, 128 * 1024 * 1024)

    def test_unknown_export_only(self):
        (self.f.source / "cubegm/driver.so").write_bytes(b"unknown variant")
        result = self.f.export()
        self.assertIsNone(result["profile_id"])
        self.assertEqual(result["file_count"], len(a.GATE_FILES))
        with self.assertRaisesRegex(a.Refusal, "unknown"):
            self.f.plan()

    def test_incomplete_export(self):
        self.f.export()
        (self.f.bundle / "complete.json").unlink()
        with self.assertRaises((OSError, a.Refusal)):
            self.f.plan()

    def test_tampered_bundle(self):
        self.f.export()
        (self.f.bundle / "files/cubegm/driver.so").write_bytes(b"tampered")
        with self.assertRaisesRegex(a.Refusal, "Modified exported"):
            self.f.plan()

    def test_source_destination_ancestors(self):
        with self.assertRaises(a.Refusal):
            a.export_bundle(self.f.source, self.f.source / "export", self.f.profile)
        with self.assertRaises(a.Refusal):
            a.separate(self.f.source, self.f.base)
        with self.assertRaises(a.Refusal):
            a.separate(self.f.source, self.f.source)

    def test_same_physical_disk(self):
        self.f.export()
        def same(path):
            value = self.f.provider(path)
            value["disk_id"] = "pc-source-disk"
            return value
        with self.assertRaisesRegex(a.Refusal, "same physical"):
            a.plan_apply(self.f.bundle, self.f.target, self.f.profile, same)

    def test_external_label_and_system_guards(self):
        self.f.export()
        for key, value in (("external", False), ("system", True), ("label", "WRONG"), ("filesystem", "ntfs"), ("disk_id", None)):
            def bad(path):
                result = self.f.provider(path)
                if Path(path) == self.f.target:
                    result[key] = value
                return result
            with self.subTest(key=key), self.assertRaises(a.Refusal):
                a.plan_apply(self.f.bundle, self.f.target, self.f.profile, bad)

    def test_bad_target_provenance(self):
        self.f.export()
        a.write_json(self.f.target / "lilsmoke/candidate.json", dict(source_img_sha256="9" * 64))
        with self.assertRaisesRegex(a.Refusal, "provenance"):
            self.f.plan()

    def test_unreviewed_launcher(self):
        self.f.export()
        (self.f.target / "lilsmoke/m8-launch.sh").write_bytes(b"modified")
        with self.assertRaisesRegex(a.Refusal, "Unreviewed"):
            self.f.plan()

    def test_updater_refused(self):
        self.f.export()
        (self.f.target / "cubegm/HCFOTA.bin").write_bytes(b"flash updater")
        with self.assertRaisesRegex(a.Refusal, "updater"):
            self.f.plan()

    def test_review_hash_and_target_change(self):
        self.f.export()
        with self.assertRaisesRegex(a.Refusal, "reviewed plan"):
            a.apply_bundle(self.f.bundle, self.f.target, "1" * 64, self.f.profile, self.f.provider)
        plan = self.f.plan()
        (self.f.target / "cubegm/driver.so").write_bytes(b"later change")
        with self.assertRaisesRegex(a.Refusal, "reviewed plan"):
            a.apply_bundle(self.f.bundle, self.f.target, plan["plan_sha256"], self.f.profile, self.f.provider)

    def test_recovery_refuses_external_changes(self):
        self.f.export()
        result = self.f.apply()
        (self.f.target / "cubegm/driver.so").write_bytes(b"changed after install")
        with self.assertRaisesRegex(a.Refusal, "changed after"):
            a.rollback(self.f.target, result["transaction"], self.f.profile, self.f.provider)
        self.assertEqual((self.f.target / "cubegm/icube").read_bytes(), a.HOOK)

    def test_corrupt_backup_preflight(self):
        self.f.export()
        result = self.f.apply()
        backup = self.f.target / a.CONTROL / "transactions" / result["transaction"] / "backup/cubegm/driver.so"
        backup.write_bytes(b"corrupt backup")
        before = self.f.snapshot()
        with self.assertRaisesRegex(a.Refusal, "backup"):
            a.rollback(self.f.target, result["transaction"], self.f.profile, self.f.provider)
        self.assertEqual(before, self.f.snapshot())

    def test_interruptions_at_every_boundary(self):
        states = [("preparing", name) for name in ("cubegm/driver.so", "cubegm/icube.stock", "cubegm/icube")]
        states += [("prepared", "")]
        states += [(state, name) for state in ("committing", "committed-file")
                   for name in ("cubegm/driver.so", "cubegm/icube.stock", "cubegm/icube")]
        states += [("complete", "")]
        states += [(state, "") for state in ("lock-released", "cleanup-owner-removed", "cleanup-complete")]
        for state, path in states:
            with self.subTest(state=state, path=path), tempfile.TemporaryDirectory() as base:
                fixture = Fixture(base)
                fixture.export()
                before = fixture.snapshot()
                def crash(s, p):
                    if (s, p) == (state, path):
                        raise RuntimeError("simulated power loss")
                with self.assertRaisesRegex(RuntimeError, "power loss"):
                    fixture.apply(crash)
                if state in ("lock-released", "cleanup-owner-removed", "cleanup-complete"):
                    self.assertFalse((fixture.target / a.CONTROL / "active").exists())
                else:
                    with self.assertRaisesRegex(a.Refusal, "Unfinished"):
                        # A no-change plan must not bypass an unfinished lock.
                        a.apply_bundle(fixture.bundle, fixture.target, fixture.plan()["plan_sha256"],
                                       fixture.profile, fixture.provider)
                a.rollback(fixture.target, profile=fixture.profile, provider=fixture.provider)
                self.assertEqual(before, fixture.snapshot())

    def test_path_traversal_and_json_budget(self):
        for name in ("../etc/passwd", "/file", "C:/windows", "a\\b", "a//b", "a/../b", "NUL", "bad.", "a:b"):
            with self.subTest(name=name), self.assertRaises(a.Refusal):
                a.relative(name)
        path = self.f.base / "invalid.json"
        path.write_text('{"duplicate": 1, "duplicate": 2}')
        with self.assertRaisesRegex(a.Refusal, "Duplicate"):
            a.read_json(path)
        path.write_bytes(b" " * (a.MAX_JSON + 1))
        with self.assertRaisesRegex(a.Refusal, "budget"):
            a.read_json(path)

    def test_links_refused(self):
        link = self.f.base / "link"
        try:
            link.symlink_to(self.f.source, target_is_directory=True)
        except OSError:
            self.skipTest("OS does not permit unprivileged symlinks")
        with self.assertRaisesRegex(a.Refusal, "Links"):
            a.inspect(link, self.f.profile)

    def test_source_changes_interrupt_export(self):
        original_identity = a.identity
        a.identity = self.f.provider
        def change(amount, total, title):
            (self.f.source / "cubegm/driver.so").write_bytes(b"changed during export")
        try:
            with self.assertRaises(a.Refusal):
                a.export_bundle(self.f.source, self.f.bundle, self.f.profile, change)
        finally:
            a.identity = original_identity
        self.assertFalse((self.f.bundle / "complete.json").exists())

    def test_out_of_space_preflight(self):
        self.f.export()
        from types import SimpleNamespace
        with patch.object(a.shutil, "disk_usage", return_value=SimpleNamespace(free=1)):
            with self.assertRaisesRegex(a.Refusal, "Insufficient space"):
                self.f.plan()
        self.assertFalse((self.f.target / a.CONTROL).exists())

    def test_device_substitution_stops_commit(self):
        self.f.export()
        before = self.f.snapshot()
        switched = False
        def device(path):
            value = self.f.provider(path)
            if switched and Path(path) == self.f.target:
                value["disk_id"] = "different-card"
            return value
        def replace(state, path):
            nonlocal switched
            if state == "prepared":
                switched = True
        plan = a.plan_apply(self.f.bundle, self.f.target, self.f.profile, device)
        with self.assertRaisesRegex(a.Refusal, "changed during"):
            a.apply_bundle(self.f.bundle, self.f.target, plan["plan_sha256"], self.f.profile, device, boundary=replace)
        self.assertEqual(before, self.f.snapshot())
        with self.assertRaisesRegex(a.Refusal, "changed during"):
            a.rollback(self.f.target, profile=self.f.profile, provider=device)
        switched = False
        a.rollback(self.f.target, profile=self.f.profile, provider=device)

    def test_long_windows_export_path(self):
        if os.name != "nt":
            self.skipTest("Windows MAX_PATH regression")
        source = self.f.source / "cubegm/long/" / ("a" * 80) / ("b" * 80) / ("c" * 60 + ".so")
        a.io_path(source.parent).mkdir(parents=True)
        a.io_path(source).write_bytes(b"long nested library")
        name = source.relative_to(self.f.source / "cubegm").as_posix()
        self.f.profile["files"][name] = dict(sha256=a.digest_bytes(b"long nested library"), bytes=19)
        self.f.export()
        self.assertEqual(a.hash_file(self.f.bundle / "files/cubegm" / name)[0], a.digest_bytes(b"long nested library"))

    def test_recovery_can_resume_partial_restore(self):
        self.f.export()
        before = self.f.snapshot()
        result = self.f.apply()
        partial = self.f.target / ("cubegm/driver.so.restore-" + result["transaction"])
        partial.write_bytes(b"partial restore interrupted")
        a.rollback(self.f.target, result["transaction"], self.f.profile, self.f.provider)
        self.assertEqual(before, self.f.snapshot())

    def test_equivalent_gui_review_paths(self):
        self.f.export()
        plan = self.f.plan()
        bundle = str(self.f.bundle).replace("\\", "/")
        target = str(self.f.target).replace("\\", "/")
        self.assertTrue(a.reviewed_paths_equal(bundle, target, plan))
        if os.name == "nt":
            self.assertTrue(a.reviewed_paths_equal(bundle.lower(), target.lower(), plan))

    def test_rollback_interruptions(self):
        states = [("rolling-back", "")]
        states += [(state, name) for state in ("restoring-file", "restored-file")
                   for name in ("cubegm/icube", "cubegm/driver.so", "cubegm/icube.stock")]
        states += [(state, "") for state in ("rolled-back", "lock-released", "cleanup-owner-removed", "cleanup-complete")]
        for state, path in states:
            with self.subTest(state=state, path=path), tempfile.TemporaryDirectory() as base:
                fixture = Fixture(base)
                fixture.export()
                before = fixture.snapshot()
                fixture.apply()
                def crash(s, p):
                    if (s, p) == (state, path):
                        raise RuntimeError("simulated recovery interruption")
                with self.assertRaisesRegex(RuntimeError, "recovery interruption"):
                    a.rollback(fixture.target, profile=fixture.profile, provider=fixture.provider, boundary=crash)
                a.rollback(fixture.target, profile=fixture.profile, provider=fixture.provider)
                self.assertEqual(before, fixture.snapshot())

    def test_legacy_empty_lock_exact_journal(self):
        self.f.export()
        before = self.f.snapshot()
        result = self.f.apply()
        (self.f.target / a.CONTROL / "active").mkdir()
        with self.assertRaisesRegex(a.Refusal, "Legacy empty"):
            a.rollback(self.f.target, profile=self.f.profile, provider=self.f.provider)
        with self.assertRaises((OSError, a.Refusal)):
            a.rollback(self.f.target, "0" * 32, self.f.profile, self.f.provider)
        a.rollback(self.f.target, result["transaction"], self.f.profile, self.f.provider)
        self.assertEqual(before, self.f.snapshot())


if __name__ == "__main__":
    unittest.main()
