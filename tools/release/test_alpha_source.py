# SPDX-License-Identifier: MPL-2.0
"""Small, file-only regression tests; no physical media or network access."""
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

import stage_alpha_source as stage


DEFCONFIG_NAMES = (
    "lilsmoke_gb350_defconfig",
    "lilsmoke_qemu_mipsel_defconfig",
    "lilsmoke_sf3000_defconfig",
    "lilsmoke_vbox_test_defconfig",
    "lilsmoke_vbox_x86_64_defconfig",
)


class AlphaSourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        for name, data in {
            "VERSION": stage.VERSION,
            "LICENSE": "Mozilla Public License Version 2.0",
            "LICENSES/MPL-2.0.txt": "Mozilla Public License Version 2.0",
            "Config.in": 'source "$BR2_EXTERNAL_LILSMOKE_PATH/package/ui/Config.in"\n',
            "external.mk": "include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/ui/ui.mk\n",
            "package/ui/Config.in": "config UI\n",
            "package/ui/ui.mk": "UI_LICENSE = MPL-2.0\n",
            "package/lilsmoke-ui/src/src/main.cpp": "int main() { return 0; }\n",
            "tools/lilsmoke-sdk/lilsmoke_sdk.py": "print('sdk')\n",
            "tools/release/stage_alpha_source.py": "# staging fixture\n",
            "README.md": "# Alpha source\n",
            "release.json": json.dumps({"version": stage.VERSION,
                                        "source_publication_status": "published"}),
        }.items():
            self.put(name, data)

    def tearDown(self):
        self.temp.cleanup()

    def put(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data.encode() if isinstance(data, str) else data)
        return path

    def report(self, name):
        return json.loads((self.root / "tools/release/_build" / name).read_text())

    def test_full_code_not_only_docs(self):
        self.put("scripts/service.sh", "#!/bin/sh\necho ok\n")
        self.put("package/ui/Makefile", "all:\n\ttrue\n")
        result = stage.stage(self.root)
        self.assertTrue(result["passed"])
        output = self.root / "tools/release/_build/alpha-source"
        self.assertTrue((output / "package/lilsmoke-ui/src/src/main.cpp").is_file())
        self.assertTrue((output / "scripts/service.sh").is_file())
        self.assertTrue((output / "SOURCE-INVENTORY.json").is_file())
        meta = json.loads((output / "release.json").read_text())
        self.assertEqual(meta["source_publication_status"], "published")

    def test_buildroot_defconfigs_survive_sanitization(self):
        data = b"# Buildroot target\nBR2_PACKAGE_LILSMOKE_OS=y\n"
        for name in DEFCONFIG_NAMES:
            self.put("configs/" + name, data)
        result = stage.stage(self.root)
        self.assertTrue(result["passed"])
        inventory = {r["path"]: r for r in
                     self.report("alpha-source-inventory.json")["files"]}
        excluded = {r["path"] for r in self.report("alpha-source-exclusions.json")["paths"]}
        build = self.root / "tools/release/_build"
        with zipfile.ZipFile(build / result["archive"]) as archive:
            for name in DEFCONFIG_NAMES:
                rel = "configs/" + name
                with self.subTest(name=name):
                    self.assertIn(rel, inventory)
                    self.assertNotIn(rel, excluded)
                    self.assertEqual(inventory[rel]["source_sha256"], stage.digest(data))
                    self.assertEqual(inventory[rel]["sha256"], stage.digest(data))
                    self.assertEqual((build / "alpha-source" / rel).read_bytes(), data)
                    self.assertEqual(archive.read(rel), data)

    def test_current_build_checks_and_release_guides_are_retained(self):
        names = ("tools/build-config/test_build_wrapper.py",
                 "tools/build-config/check_defconfigs.py",
                 "docs/alpha-0.0.1/README.md",
                 "docs/alpha-0.0.1/RELEASE-NOTES.md")
        for name in names:
            self.put(name, "# Public build/source correction fixture\n")
        selected, excluded = stage.collect(self.root)
        paths = {row[0] for row in selected}
        for name in names:
            self.assertIn(name, paths)
        self.assertFalse(any(row["path"] == "docs/alpha-0.0.1/" for row in excluded))

    def test_defconfig_filename_rule_is_exact(self):
        self.assertTrue(stage.allowed_source("configs/legacy.defconfig"))
        for name in ("defconfig", "notdefconfig", "target", "target_defconfigs",
                     "target_defconfig.bak", "target_defconfig_private"):
            with self.subTest(name=name):
                self.assertFalse(stage.allowed_source("configs/" + name))

    def test_buildroot_defconfigs_reject_binary_headers_and_nul_bytes(self):
        payloads = [magic + b"disguised binary\n" for magic in stage.MAGICS]
        payloads.append(b'CONFIG_BROKEN="before\0after"\n')
        paths = []
        for index, data in enumerate(payloads):
            for name in DEFCONFIG_NAMES:
                rel = "configs/payload-%d/%s" % (index, name)
                self.put(rel, data)
                paths.append(rel)
        result = stage.stage(self.root)
        self.assertTrue(result["passed"])
        excluded = {r["path"]: r["reason"] for r in
                    self.report("alpha-source-exclusions.json")["paths"]}
        inventory = {r["path"] for r in self.report("alpha-source-inventory.json")["files"]}
        build = self.root / "tools/release/_build"
        with zipfile.ZipFile(build / result["archive"]) as archive:
            archived = set(archive.namelist())
            for rel in paths:
                with self.subTest(path=rel):
                    self.assertEqual(excluded[rel],
                                     "binary/ROM/container signature or NUL bytes")
                    self.assertNotIn(rel, inventory)
                    self.assertNotIn(rel, archived)
                    self.assertFalse((build / "alpha-source" / rel).exists())

    def test_buildroot_defconfigs_still_scanned_for_credentials(self):
        token = "gh" + "p_" + "D" * 36
        data = ('CONFIG_FIXTURE="' + token + '"\n').encode()
        paths = {"configs/" + name for name in DEFCONFIG_NAMES}
        for rel in paths:
            self.put(rel, data)
        result = stage.stage(self.root)
        self.assertFalse(result["passed"])
        self.assertFalse((self.root / "tools/release/_build/alpha-source").exists())
        findings = self.report("alpha-source-audit.json")["findings"]
        self.assertEqual({r["path"] for r in findings if r["status"] == "blocked"}, paths)
        self.assertTrue(all(r["rule"] == "github-token" for r in findings))
        for rel in paths:
            self.assertEqual((self.root / rel).read_bytes(), data)

    def test_outputs_private_checkpoints_and_sourcekits_pruned(self):
        for name in ("build/private.c", "tools/x/_build/cached.c",
                     "tools/x/sourcekits/extracted.c", "docs/development-checkpoint-private.md",
                     "test-results/controller.json", ".git/config"):
            self.put(name, "private fixture")
        selected, excluded = stage.collect(self.root)
        names = {r[0] for r in selected}
        self.assertNotIn("build/private.c", names)
        self.assertNotIn("tools/x/sourcekits/extracted.c", names)
        self.assertTrue(any(r["path"] == "build/" and r["subtree"] for r in excluded))

    def test_only_named_custom_experiments_removed(self):
        for name in ("tools/daedalus-lilsmoke/main.zig",
                     "tools/mupen-lilsmoke/main.cpp",
                     "package/opendingux-emulators/src/mupen64/main.c",
                     "package/scummvm/rooms/daedalus.cpp"):
            self.put(name, "// source\n")
        names = {r[0] for r in stage.collect(self.root)[0]}
        self.assertNotIn("tools/daedalus-lilsmoke/main.zig", names)
        self.assertNotIn("tools/mupen-lilsmoke/main.cpp", names)
        self.assertIn("package/opendingux-emulators/src/mupen64/main.c", names)
        self.assertIn("package/scummvm/rooms/daedalus.cpp", names)

    def test_support_source_formats_and_owned_cursor(self):
        for name in ("package/ui/font.bdf", "package/ui/lookup.tbl",
                     "package/ui/build.zig", "package/ui/config.dat"):
            self.put(name, "text support source\n")
        cursor = "package/lilsmoke-ui/src/assets/lilsmoke-cursors.webp"
        self.put(cursor, b"RIFF\0owned WEBP")
        self.put("package/ui/unreviewed.png", b"\x89PNG\0private")
        stage.stage(self.root)
        names = {r["path"] for r in self.report("alpha-source-inventory.json")["files"]}
        self.assertIn(cursor, names)
        self.assertIn("package/ui/lookup.tbl", names)
        self.assertIn("package/ui/build.zig", names)
        self.assertNotIn("package/ui/unreviewed.png", names)
        scope = self.report("alpha-source-license-scope.json")
        self.assertIn("owner-provided", scope["approved_assets"][cursor])

    def test_binaries_and_renamed_roms_rejected(self):
        self.put("package/ui/renamed.c", stage.MAGICS[-1] + b"game")
        self.put("package/ui/executable.txt", b"MZ\0binary")
        self.put("package/ui/rom.dat", b"\0ROM")
        stage.stage(self.root)
        paths = {r["path"] for r in self.report("alpha-source-exclusions.json")["paths"]}
        self.assertTrue({"package/ui/renamed.c", "package/ui/executable.txt",
                         "package/ui/rom.dat"} <= paths)

    def test_unknown_credential_blocks_without_stripping(self):
        token = "gh" + "p_" + "A" * 36
        self.put("package/ui/key.cpp", 'const char *key="' + token + '";')
        result = stage.stage(self.root)
        self.assertFalse(result["passed"])
        self.assertFalse((self.root / "tools/release/_build/alpha-source").exists())
        audit = (self.root / "tools/release/_build/alpha-source-audit.json").read_text()
        self.assertNotIn(token, audit)
        self.assertIn("github-token", audit)
        self.assertIn(token, (self.root / "package/ui/key.cpp").read_text())

    def test_review_exact_hash_not_entire_test_directory(self):
        token = "gh" + "p_" + "B" * 36
        rel = "tools/test_fixture.py"
        key = (rel, "github-token", stage.digest(token.encode()))
        try:
            stage.REVIEWED_FINDINGS[key] = "synthetic negative-test token"
            self.assertEqual(stage.scan(rel, token.encode())[0]["status"], "reviewed-fixture")
            other = ("gh" + "p_" + "C" * 36).encode()
            self.assertEqual(stage.scan(rel, other)[0]["status"], "blocked")
        finally:
            stage.REVIEWED_FINDINGS.pop(key, None)

    def test_only_reviewed_owner_path_fixtures_transformed(self):
        private = "/".join(("/mnt/c", "Users", "fixture", "Downloads", "game.z64"))
        rel = "tools/emulators/test-user-games.sh"
        original = ("ROM='" + private + "'\n").encode()
        data, edits = stage.public_copy(rel, original)
        self.assertEqual(data, b"ROM='/path/to/local-input/game.z64'\n")
        self.assertEqual(edits[0]["occurrences"], 1)
        unreviewed = stage.public_copy("package/ui/new.cpp", original)[0]
        self.assertTrue(stage.scan("package/ui/new.cpp", unreviewed))

    def test_config_integration_removed_only_for_custom_roots(self):
        lines = ('source "$BR2_EXTERNAL_LILSMOKE_PATH/package/mupen-lilsmoke/Config.in"\n'
                 'source "$BR2_EXTERNAL_LILSMOKE_PATH/package/upstream-mupen/Config.in"\n')
        data, edits = stage.public_copy("Config.in", lines.encode())
        self.assertNotIn("package/mupen-lilsmoke/", data.decode())
        self.assertIn("package/upstream-mupen/", data.decode())
        self.assertEqual(len(edits), 1)

    def test_missing_active_registration_blocks(self):
        self.put("Config.in", 'source "$BR2_EXTERNAL_LILSMOKE_PATH/package/missing/Config.in"\n')
        with self.assertRaisesRegex(ValueError, "missing active"):
            stage.stage(self.root)

    def test_refresh_is_owned_and_bounded(self):
        output = self.root / "tools/release/_build/alpha-source"
        output.mkdir(parents=True)
        (output / "unrelated").write_text("do not delete")
        with self.assertRaisesRegex(ValueError, "unowned"):
            stage.stage(self.root, refresh=True)
        self.assertTrue((output / "unrelated").exists())
        with self.assertRaisesRegex(ValueError, "output must"):
            stage.stage(self.root, output=self.root / "unsafe")

    def test_repeat_archive_determinism_and_modes(self):
        self.put("scripts/run.sh", "#!/bin/sh\nexit 0\n")
        first = stage.stage(self.root)
        second = stage.stage(self.root, refresh=True)
        self.assertEqual(first["archive_sha256"], second["archive_sha256"])
        with zipfile.ZipFile(self.root / "tools/release/_build" / first["archive"]) as archive:
            entry = archive.getinfo("scripts/run.sh")
            self.assertEqual(entry.date_time, (2026, 1, 1, 0, 0, 0))
            self.assertEqual((entry.external_attr >> 16) & 0o777, 0o755)
            self.assertFalse(any("/_build/" in n for n in archive.namelist()))

    def test_refresh_never_removes_attached_git_history(self):
        stage.stage(self.root)
        output = self.root / "tools/release/_build/alpha-source"
        (output / ".git").mkdir()
        head = output / ".git/HEAD"
        head.write_text("ref: refs/heads/main\n")
        with self.assertRaisesRegex(ValueError, "Git checkout"):
            stage.stage(self.root, refresh=True)
        self.assertEqual(head.read_text(), "ref: refs/heads/main\n")

    def test_prior_public_is_read_only_and_preserves_identity(self):
        prior = self.root / "prior-public"
        (prior / ".github/ISSUE_TEMPLATE").mkdir(parents=True)
        logo = b"RIFF\0existing owner logo"
        (prior / "logo.webp").write_bytes(logo)
        template = prior / ".github/ISSUE_TEMPLATE/bug.yml"
        template.write_text("name: Existing public template\n")
        self.put(".github/ISSUE_TEMPLATE/bug.yml", "name: Different local draft\n")
        before = template.read_bytes()
        stage.stage(self.root, prior_public=prior)
        output = self.root / "tools/release/_build/alpha-source"
        self.assertEqual((output / "logo.webp").read_bytes(), logo)
        self.assertEqual((output / ".github/ISSUE_TEMPLATE/bug.yml").read_bytes(), before)
        self.assertEqual(template.read_bytes(), before)

    def test_source_budget_refuses_truncation(self):
        with self.assertRaisesRegex(ValueError, "exceeds staging budget"):
            stage.stage(self.root, max_bytes=1)

    def test_version_mismatch_blocks(self):
        self.put("VERSION", "Pre-Alpha-0.0.1")
        with self.assertRaisesRegex(ValueError, "VERSION"):
            stage.stage(self.root)

    def test_finalize_only_updates_release_inputs_and_verifies_every_byte(self):
        stage.stage(self.root)
        self.put("release.json", json.dumps({"version": stage.VERSION,
                                             "source_publication_status": "published"}))
        result = stage.finalize_staged(self.root)
        self.assertTrue(result["passed"])
        output = self.root / "tools/release/_build/alpha-source"
        (output / "package/lilsmoke-ui/src/src/main.cpp").write_text("unexpected edit")
        with self.assertRaisesRegex(ValueError, "staged content changed"):
            stage.finalize_staged(self.root)


if __name__ == "__main__":
    unittest.main()
