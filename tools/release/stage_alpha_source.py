#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas
"""Stage the authored Alpha external tree, retained dependencies and audit reports.

No network, Git operations, device access, binary execution or credential removal.
Unknown secrets fail closed. Public-copy edits are listed with before/after hashes.
"""
import argparse
from collections import Counter, deque
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import zipfile

ROOT = Path(__file__).resolve().parents[2]
VERSION = "Alpha-0.0.1"
ROOT_FILES = {
    ".gitignore", "ARCHITECTURE.md", "CHANGELOG.md", "Config.in",
    "CONTRIBUTING.md", "CREDITS.md", "external.desc", "external.mk", "LICENSE",
    "LICENSE_POLICY.md", "README.md", "release.json", "THIRD_PARTY_NOTICES.md",
    "VERSION",
}
ROOT_TREES = {".github", "LICENSES", "assets", "artwork", "board", "configs",
              "lrhost-system", "package", "patches", "scripts", "tools", "docs"}
# Whole private/generated trees are pruned before traversal, including github-os.
DENIED_DIRS = {
    ".git", ".hg", ".svn", "_build", "build", "work", "output", "outputs",
    "test-results", "__pycache__", "node_modules", "artifacts", "evidence",
    "captures", "screenshots", "sourcekits", "source-kits", "sourcekit",
    "source-kit", "cache", "caches", "dl", "prebuilt", "stock-export",
    "private-local-apps", "sd_recuperado", ".portsmoke",
}
EXPERIMENT_ROOTS = (
    "tools/daedalus-lilsmoke", "tools/mupen-lilsmoke",
    "package/daedalus-lilsmoke", "package/mupen-lilsmoke",
)
RELEASE_TOOLS = {
    "stage_alpha_source.py", "test_alpha_source.py", "check_release.py",
    "test_release.py", "check_license_text.py", "check_license_scope.py",
    "validate_docs.py", "os-release-notes.md", "build_alpha_image.py",
    "freeze_image.py", "test_freeze_image.py", "stage_documentation.py", "name_private_image.py",
    "test_build_alpha_image.py", "snapshot_alpha_card.py",
}
PUBLIC_DOCS = {
    "README.md", "build.md", "frontend.md", "hardware.md", "input.md", "cores.md",
    "performance.md", "PORTING-GUIDE.md", "THEME-GUIDE.md", "USER-GUIDE.md",
    "lilsmoke-applications-sdk.md", "lilsmoke-applications.md",
    "lilsmoke-explorer.md", "media-stack.md", "opendingux-integration.md",
    "ota.md", "overclocking.md", "portsmoke.md", "remote-access.md",
    "storage-layout.md", "syspck.md", "theme-compat.md", "vbox-guide.md",
    "wifi-ota.md", "bluetooth-and-mapping.md", "treefrog-port-converter.md",
    "treefrog-ports-compatibility-20261027.md", "release-preparation.md",
    "alpha-0.0.1-source.md",
}
SOURCE_SUFFIXES = {
    ".c", ".h", ".cpp", ".hpp", ".cc", ".cxx", ".hxx", ".hh", ".ipp", ".inl",
    ".inc", ".s", ".asm", ".m", ".mm", ".cs", ".java", ".py", ".sh", ".bash",
    ".ps1", ".psm1", ".cmd", ".bat", ".command", ".pl", ".pm", ".rb", ".lua",
    ".js", ".mjs", ".cjs", ".ts", ".tsx", ".jsx", ".html", ".htm", ".css",
    ".mk", ".mak", ".make", ".in", ".am", ".ac", ".m4", ".cmake", ".meson",
    ".patch", ".diff", ".def", ".fdh", ".y", ".l", ".yy", ".ll",
    ".zig", ".zon", ".rs", ".go", ".d", ".pas", ".f", ".f90", ".tcl",
    ".tbl", ".table", ".dat", ".bdf", ".xpm", ".xbm", ".csv", ".tsv",
    ".md", ".rst", ".txt", ".json", ".yml", ".yaml", ".toml", ".xml", ".ini",
    ".cfg", ".conf", ".config", ".fragment", ".defconfig", ".map", ".lock",
    ".hash", ".info", ".desktop", ".pc", ".version", ".rules", ".policy",
    ".service", ".socket", ".cmakein", ".tmpl", ".template", ".proto",
    ".frag", ".vert", ".geom", ".comp", ".tesc", ".tese", ".glsl", ".shader",
    ".cg", ".slang", ".metal", ".hlsl", ".rc", ".manifest", ".plist",
    ".vcxproj", ".vcproj", ".sln", ".filters", ".dsp", ".dsw", ".pro", ".pri",
    ".po", ".pot", ".strings", ".1", ".5", ".6", ".7", ".8", ".engine",
    ".common", ".build", ".opts", ".properties", ".gradle", ".spec", ".lds",
}
SOURCE_NAMES = {
    "makefile", "gnumakefile", "cmakelists.txt", "meson.build", "meson_options.txt",
    "configure", "configure.ac", "config.in", "config.sub", "config.guess",
    "inittab", "od-launch", "lilsmoke-media-open", ".gitignore",
    ".gitattributes", ".gitmodules", ".editorconfig", "dockerfile",
}
ASSET_ALLOWLIST = {
    "package/lilsmoke-ui/src/assets/lilsmoke-cursors.webp": "owner-provided cursor artwork; MPL-2.0 by release request",
    # Owner explicitly approved the project's own supplied logos.
    "assets/logo/lilsmoke-logo.webp": "owner-provided project logo",
    "assets/logo/lilsmoke-logo.png": "owner-provided project logo",
    "board/segam-m8-v6-stock/boot-logo.webp": "owner-provided project logo",
    "artwork/pcsx4all-icon.png": "original generated art; artwork/pcsx4all-icon.md",
    "package/pcsx4all/app/icon.webp": "original generated art; artwork/pcsx4all-icon.md",
    "tools/apps/demo/lucide-gamepad-2.svg": "ISC; adjacent LICENSE.lucide",
    "tools/apps/demo/icon.png": "Lucide-derived; adjacent LICENSE.lucide",
}
# These specific source fixtures were inspected: they reference owner-local ROM,
# BIOS/theme or build-tool inputs, not bundled assets. Replace only those paths,
# not arbitrary code, user identifiers, secrets or upstream copyright notices.
PATH_FIXTURES = {
    "tools/emulator/LilSmokeOSEmulatorLauncher.cs",
    "tools/release/freeze_image.py",
    "package/pcsx4all/src/README_RKGAME_RECONSTRUCTION.md",
    "tools/verify_repaired_apps_20261027.ps1", "tools/verify_card_20261026.ps1",
    "tools/apps/clean_pcsx_test_roms.py", "tools/emulators/test-user-games.sh",
    "tools/emulators/test-regba-user-game.py", "tools/emulators/test-recovered-ui.sh",
    "tools/ui/test_theme_motion.py", "tools/apps/test_full_ui.sh",
    "tools/apps/test_style_full_ui.sh", "tools/hwtest/ui_functional_smoke.py",
    "tools/treefrog-compat/update_return_card.ps1",
    "tools/treefrog-compat/update_native04_card.ps1",
    "tools/portsmoke/sm64/run_progress_tests.sh",
    "tools/portsmoke/sm64/run_private_tests.sh", "tools/m8/reinstall_sm64.ps1",
    "tools/m8/install_sm64_clock_fix.ps1", "tools/m8/install_private_gba_bios15.ps1",
    "tools/m8/test_candidate15_ui.sh", "tools/m8/test_candidate14_ui.sh",
    "tools/m8/test_candidate08.sh", "tools/portsmoke/sm64-public/run_candidate_cook.sh",
    "tools/portsmoke/sm64-public/test_device_resume_native.sh",
    "tools/portsmoke/sm64-public/test_preparation_mips.sh",
    "tools/portsmoke/sm64-original/collect_detail_results.py",
    "tools/portsmoke/sm64-original/collect_prealpha.py",
    "tools/portsmoke/sm64-original/test_quality_artifacts.py",
}
PERSONAL_PATH = re.compile(
    r"""(?i)(?:[a-z]:[\\/]+Users[\\/]+[^"'\x60\r\n]+|/mnt/[a-z]/Users/[^"'\x60\r\n]+)""")
TOKEN_SHAPES = {
    "github-token": re.compile(r"\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})\b"),
    "openai-token": re.compile(r"\bsk-(?:proj-)?[A-Za-z0-9_-]{35,}\b"),
    "aws-access-key": re.compile(r"\b(?:AKIA|ASIA)[A-Z0-9]{16}\b"),
    "private-key-block": re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----\s+[A-Za-z0-9+/=\r\n]{80,}"),
    "credential-in-url": re.compile(r"https?://[^\s/:@]+:[^\s/@]{8,}@"),
    "ssh-public-key": re.compile(r"ssh-(?:rsa|ed25519) [A-Za-z0-9+/]{45,}={0,3}"),
}
SHAPE_HINTS = {
    "github-token": ("ghp_", "gho_", "ghu_", "ghs_", "ghr_", "github_pat_"),
    "openai-token": ("sk-",), "aws-access-key": ("AKIA", "ASIA"),
    "private-key-block": ("PRIVATE KEY-----",), "credential-in-url": ("http://", "https://"),
    "ssh-public-key": ("ssh-rsa ", "ssh-ed25519 "),
}
# Reviewed source fixtures are keyed by exact matched-byte hash, never by a
# blanket "tests are safe" exception. Populated only after inspecting a finding.
REVIEWED_FINDINGS = {
    ("package/opendingux-emulators/src/atari800/libretro/libretro-common/vfs/vfs_implementation_uwp.cpp", "personal-workstation-path",
     "78f97d2d314af65453063faf57cbc3d32591f987294c56af872e987b848e8dd5"):
        "Retained upstream UWP comment uses the synthetic somebody path; not owner data.",
    ("package/opendingux-emulators/src/cannonball/deps/libretro-common/vfs/vfs_implementation_uwp.cpp", "personal-workstation-path",
     "78f97d2d314af65453063faf57cbc3d32591f987294c56af872e987b848e8dd5"):
        "Retained upstream UWP comment uses the synthetic somebody path; not owner data.",
    ("package/opendingux-emulators/src/dgen/libretro/libretro-common/vfs/vfs_implementation_uwp.cpp", "personal-workstation-path",
     "78f97d2d314af65453063faf57cbc3d32591f987294c56af872e987b848e8dd5"):
        "Retained upstream UWP comment uses the synthetic somebody path; not owner data.",
    ("package/opendingux-emulators/src/exult/FAQ", "personal-workstation-path",
     "7218479c3d1ea15bcdc4e214496ff293cea07f0f482be98ebb3f45fdc281a335"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/README", "personal-workstation-path",
     "7218479c3d1ea15bcdc4e214496ff293cea07f0f482be98ebb3f45fdc281a335"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/README", "personal-workstation-path",
     "26352c25589cb01ea734c0d6652da7ee9d90c8534b890ef5e7c998d466a70f0c"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/README", "personal-workstation-path",
     "72677400f713376ab680fca7e942552b2e04f5ab23075b4c1ed3bb493599dff2"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/README", "personal-workstation-path",
     "dbf73f8f1fcb63bf45b08008686a5b59bf9ccce782b5b66f0aff27453bd4ff86"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/README", "personal-workstation-path",
     "c1eb005b50e2d04a48ec921945e6afc92e08c89f282e295a9462112c42e8a67e"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/README.windows", "personal-workstation-path",
     "fb8cb1f53a8f08ffc025b1d1669598cd5e5497f2a9f4adc5506596bb8c2221c0"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/README.windows", "personal-workstation-path",
     "3f976d6882ea58990097d48f5cabda351c63c5070bfd09ab355c6d27b90304f1"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/README.windows", "personal-workstation-path",
     "782f219a8552688376d6e6ddef39243f84e1cb9a6a09ca261e5662574da48f9a"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/docs/ReadMe.html", "personal-workstation-path",
     "63556e7474ea5958377e371f228d2a6827eb1bf511222d26e81888e26905cc16"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/docs/ReadMe.html", "personal-workstation-path",
     "6885421483bdb99fddd3d5cfbf421d9efe5abb2fc1d79138750a98b895fa0f63"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/docs/ReadMe.html", "personal-workstation-path",
     "90af12841827a98e02d98c0c3f9738f72f55cfa484b699898cc86e410d0aa23d"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/docs/ReadMe.html", "personal-workstation-path",
     "cf328d450b47d7d0ca7c813ed3ffa494c8124bc6b7d9e402411c122e175c8a8c"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/docs/ReadMe.html", "personal-workstation-path",
     "d67b483bb8a157d70c5d4ebad77b92e00b99d853e3d01190ca05bb0db3fd4308"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/docs/ReadMe.html", "personal-workstation-path",
     "e154185dc8369d29780be6af24ad39e3a64d7962f793da0c18a867aa9cf322b4"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/docs/faq.html", "personal-workstation-path",
     "63556e7474ea5958377e371f228d2a6827eb1bf511222d26e81888e26905cc16"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/docs/xml/docs.xml", "personal-workstation-path",
     "dd611baa41ff68bbf1aa004088f824646ea96eac1c7838e2c980395760604ee0"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/docs/xml/docs.xml", "personal-workstation-path",
     "946999bd67354ed45e8feac31c2e80c8003d4d6e6caf66742fee45917932692a"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/docs/xml/docs.xml", "personal-workstation-path",
     "ddd58a52e6a476c002777943043b4c6b85c928d28d3e753c8cafe6ac6bd8835a"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/docs/xml/docs.xml", "personal-workstation-path",
     "d20c88fd1b1b1d383d2c97e1f4b29b1a2b78a4c0a8714a622c5379241e7d6a3a"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/docs/xml/docs.xml", "personal-workstation-path",
     "cefe92a1e693dc289964698cc0ad1ead335ffe9b2cc6dedea763c095b6dbc8ae"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/docs/xml/docs.xml", "personal-workstation-path",
     "931ec0d755a423402eaf4757ac44a75dd2a373322820676f59450b73c2a3c525"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/exult/docs/xml/faq.xml", "personal-workstation-path",
     "dd611baa41ff68bbf1aa004088f824646ea96eac1c7838e2c980395760604ee0"):
        "Retained upstream installation docs use YourUsername, username or FooBar example paths; not owner data.",
    ("package/opendingux-emulators/src/mupen64/GLideN64/projects/msvc12/libGLideNHQ.vcxproj", "personal-workstation-path",
     "ba36b68d7728b46293a13bd7552114ef90c70003d0264d4481e80718e3be23f5"):
        "Unmodified upstream MSVC dependency path, not owner data; unused by MIPS recipes.",
    ("package/opendingux-emulators/src/scummvm/engines/wintermute/base/file/base_disk_file.cpp", "personal-workstation-path",
     "08279df629a80b8f93bccdbc3879b9539ac452eb00693ecd61908e127d9abe26"):
        "Upstream functional game-compatibility path matching/comment, not owner data; must not rewrite.",
    ("package/opendingux-emulators/src/scummvm/engines/wintermute/base/file/base_disk_file.cpp", "personal-workstation-path",
     "e9e3b86d0a788f49a036a6007fb84842fcfe2bf2ed453beb4cd0f0b0433a40c7"):
        "Upstream functional game-compatibility path matching/comment, not owner data; must not rewrite.",
    ("package/opendingux-emulators/src/scummvm/engines/wintermute/base/file/base_disk_file.cpp", "personal-workstation-path",
     "ca8579f898cd879ad2facf560c90b907d7d7f949c3112109b75a57e9d812b425"):
        "Upstream functional game-compatibility path matching/comment, not owner data; must not rewrite.",
    ("package/opendingux-emulators/src/scummvm/engines/wintermute/base/file/base_disk_file.cpp", "personal-workstation-path",
     "bb1c66601a8c5c9518b251458b795cd356402826f15c892de863e7468a348f9e"):
        "Upstream functional game-compatibility path matching/comment, not owner data; must not rewrite.",
    ("package/opendingux-emulators/src/smsplus/libretro/libretro-common/vfs/vfs_implementation_uwp.cpp", "personal-workstation-path",
     "78f97d2d314af65453063faf57cbc3d32591f987294c56af872e987b848e8dd5"):
        "Retained upstream UWP comment uses the synthetic somebody path; not owner data.",
    ("package/opendingux-emulators/src/smssdl/libretro/libretro-common/vfs/vfs_implementation_uwp.cpp", "personal-workstation-path",
     "78f97d2d314af65453063faf57cbc3d32591f987294c56af872e987b848e8dd5"):
        "Retained upstream UWP comment uses the synthetic somebody path; not owner data.",
    ("package/retroarch-mips/cores/genesis_plus_gx/libretro/libretro-common/vfs/vfs_implementation_uwp.cpp", "personal-workstation-path",
     "78f97d2d314af65453063faf57cbc3d32591f987294c56af872e987b848e8dd5"):
        "Retained upstream UWP comment uses the synthetic somebody path; not owner data.",
}
MAGICS = (b"\x7fELF", b"MZ", b"PK\x03\x04", b"7z\xbc\xaf\x27\x1c",
          b"\x1f\x8b", b"\xfd7zXZ\0", b"Rar!", b"\x80\x37\x12\x40",
          b"\x37\x80\x40\x12", b"\x40\x12\x37\x80")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def io_path(path):
    path = Path(path).absolute()
    if os.name == "nt" and not str(path).startswith("\\\\?\\"):
        return Path("\\\\?\\" + str(path))
    return path


def plain(path):
    info = path.lstat()
    return not (stat.S_ISLNK(info.st_mode) or
                getattr(info, "st_file_attributes", 0) &
                getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0))


def exclusion(relative, directory=False):
    parts = PurePosixPath(relative).parts
    folded = [part.casefold() for part in parts]
    if any(p in DENIED_DIRS for p in folded):
        return "private/generated/cache subtree"
    if any(relative == p or relative.startswith(p + "/") for p in EXPERIMENT_ROOTS):
        return "excluded custom N64 experiment (not generic upstream N64)"
    if any(p.startswith("performance-20") for p in folded):
        return "private performance capture"
    if len(parts) == 1 and parts[0] not in ROOT_FILES | ROOT_TREES:
        return "outside explicit external-tree roots"
    if parts[:2] == ("tools", "cleanup") or (parts[0] == "tools" and
            len(parts) == 2 and parts[1].startswith("cleanup_")):
        return "owner workspace cleanup automation, not OS source"
    if parts[:2] == ("tools", "release") and len(parts) > 2 and parts[2] not in RELEASE_TOOLS:
        return "historical/private release automation; Alpha source tool replaces it"
    if parts[0] == "docs" and len(parts) > 1:
        if parts[1] not in {"developer", "pre-alpha-0.0.1"} | PUBLIC_DOCS:
            return "private development checkpoint/capture document"
    if parts[0] == "board" and not directory and Path(relative).suffix == ".md":
        return "owner-specific board bring-up notes; implementation source retained"
    if not directory and (parts[-1].startswith("PRIVATE-") or
            re.match(r"hotplug-validation-.*\.json$", parts[-1])):
        return "private capture or derived experiment output"
    if relative == "tools/m8/m8-pocketsnes-handoff.md":
        return "private owner handoff note; implementation and tests retained"
    if relative == "tools/emulator/LilSmokeOSEmulatorLauncher.cs":
        return "retired PC-emulator launcher explicitly excluded by owner; Malta/QEMU QA retained"
    return None


def allowed_source(relative):
    path = PurePosixPath(relative)
    name = path.name.casefold()
    if relative in ASSET_ALLOWLIST:
        return True
    if name.startswith(("license", "copying", "copyright", "notice", "readme",
                        "makefile.", "config.", "sconstruct", "sconscript")):
        return True
    if name in SOURCE_NAMES or path.suffix.casefold() in SOURCE_SUFFIXES:
        return True
    # Installed init/service/launcher scripts have no suffix.
    return not path.suffix and (relative.startswith(("board/", "package/")) or
                                name in {"version", "authors", "changes", "changelog"})


def public_copy(relative, data):
    edits = []
    if relative in ASSET_ALLOWLIST:
        return data, edits
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return data, edits
    if (relative in PATH_FIXTURES or relative.startswith(("docs/", "assets/"))) and PERSONAL_PATH.search(text):
        def portable(match):
            value = match.group().replace("\\", "/")
            name = value.rstrip("/").rsplit("/", 1)[-1]
            prefix = "C:/path/to/local-input/" if re.match(r"^[a-z]:", value, re.I) else "/path/to/local-input/"
            return prefix + name
        text, count = PERSONAL_PATH.subn(portable, text)
        edits.append({"kind": "reviewed-owner-input-path-placeholder", "occurrences": count,
                      "rationale": "inspected fixture/example; user supplies local inputs; no game bytes copied"})
    if relative in {"Config.in", "external.mk"} or relative.startswith("configs/"):
        lines = text.splitlines(keepends=True)
        kept = [line for line in lines if not any(p in line for p in EXPERIMENT_ROOTS)]
        if kept != lines:
            text = "".join(kept)
            edits.append({"kind": "omit-custom-experiment-registration"})
    # Historical runner templates retain MIT/AGPL boundaries; only their old
    # description of this OS's private license is obsolete.
    if relative.startswith("tools/froggytale-lilsmoke/"):
        changed = text.replace("personal-use UI", "MPL-covered UI").replace(
            "personal-use OS/UI source", "MPL-covered OS/UI source").replace(
            "private OS/UI source", "separately licensed OS/UI source").replace(
            "proprietary LilSmoke UI/driver source", "separately licensed LilSmoke UI/driver source")
        if changed != text:
            text = changed
            edits.append({"kind": "update-Alpha-license-description-not-upstream-license"})
    return text.encode("utf-8"), edits


def scan(relative, data):
    if relative in ASSET_ALLOWLIST or relative == "logo.webp":
        return []
    text = data.decode("utf-8", errors="replace")
    patterns = dict(TOKEN_SHAPES, **{"personal-workstation-path": PERSONAL_PATH})
    rows = []
    for rule, pattern in patterns.items():
        if rule == "personal-workstation-path":
            if "users" not in text.lower():
                continue
        elif not any(hint in text for hint in SHAPE_HINTS[rule]):
            continue
        for match in pattern.finditer(text):
            value_hash = digest(match.group().encode("utf-8"))
            reason = REVIEWED_FINDINGS.get((relative, rule, value_hash))
            rows.append({"path": relative, "line": text.count("\n", 0, match.start()) + 1,
                         "rule": rule, "match_sha256": value_hash,
                         "status": "reviewed-fixture" if reason else "blocked",
                         "rationale": reason or "unreviewed sensitive shape; content omitted from report"})
    return rows


def collect(root):
    root = io_path(root)
    selected, excluded = [], []
    def visit(base, prefix=""):
        # DirEntry metadata is cached by Windows enumeration; avoid a second
        # stat syscall for every file in the large retained upstream trees.
        with os.scandir(base) as listing:
            entries = sorted(listing, key=lambda e: e.name)
        for entry in entries:
            rel = prefix + entry.name
            info = entry.stat(follow_symlinks=False)
            directory = stat.S_ISDIR(info.st_mode)
            linked = (stat.S_ISLNK(info.st_mode) or
                      getattr(info, "st_file_attributes", 0) &
                      getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0))
            reason = "link/reparse point" if linked else exclusion(rel, directory)
            if not reason and not directory and not stat.S_ISREG(info.st_mode):
                reason = "not a regular file"
            if not reason and not directory and not allowed_source(rel):
                reason = "not allowlisted source or provenance-reviewed art (no opaque assets/binaries)"
            if reason:
                row = {"path": rel + ("/" if directory else ""), "reason": reason}
                if directory:
                    row["subtree"] = True
                excluded.append(row)
            elif directory:
                visit(Path(entry.path), rel + "/")
            else:
                selected.append((rel, Path(entry.path)))
    visit(root)
    return sorted(selected), sorted(excluded, key=lambda r: r["path"])


def ordered_io(function, items, workers=8):
    """Overlap bounded small-file I/O without changing inventory/archive order."""
    source = iter(items)
    with ThreadPoolExecutor(max_workers=workers) as pool:
        pending = deque()
        for _ in range(workers):
            item = next(source, None)
            if item is not None:
                pending.append(pool.submit(function, item))
        while pending:
            yield pending.popleft().result()
            item = next(source, None)
            if item is not None:
                pending.append(pool.submit(function, item))


def inspect_source(item):
    rel, path = item
    original = path.read_bytes()
    if rel not in ASSET_ALLOWLIST and rel != "logo.webp" and (
            original.startswith(MAGICS) or b"\0" in original):
        return None, [], {"path": rel, "reason": "binary/ROM/container signature or NUL bytes"}
    data, edits = public_copy(rel, original)
    mode = 0o755 if data.startswith(b"#!") or Path(rel).suffix in {".sh", ".command"} else 0o644
    return ({"path": rel, "bytes": len(data), "sha256": digest(data),
             "source_sha256": digest(original), "mode": oct(mode), "edits": edits},
            scan(rel, data), None)


def write_json(path, data):
    path = io_path(path)
    if path.exists() and not plain(path):
        raise ValueError("refusing report write through a link/reparse point")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=True, sort_keys=True) + "\n",
                    encoding="utf-8")


def safe_refresh(root, output):
    expected = (root / "tools/release/_build/alpha-source").resolve()
    if output.resolve() != expected or not plain(output):
        raise ValueError("refresh allowed only for this tree's exact alpha-source directory")
    if (output / ".git").exists():
        raise ValueError("refusing refresh of a Git checkout; preserve attached repository history")
    marker = output / ".alpha-source-staging.json"
    if not marker.is_file() or json.loads(marker.read_text()) != {"tool": "stage_alpha_source.py"}:
        raise ValueError("refusing to remove an unowned staging directory")
    for base, dirs, files in os.walk(io_path(output), followlinks=False):
        if any(not plain(Path(base) / n) for n in dirs + files):
            raise ValueError("refusing refresh through a link/reparse point")
    shutil.rmtree(io_path(output))


def stage(root, output=None, prior_public=None, audit_only=False, refresh=False,
          max_bytes=1536 * 1024**2, progress=None):
    root = Path(root).resolve()
    output = Path(output or root / "tools/release/_build/alpha-source").resolve()
    build = (root / "tools/release/_build").resolve()
    if build != root / "tools/release/_build":
        raise ValueError("build directory must not resolve outside the selected tree")
    if output.parent != build or output.name != "alpha-source":
        raise ValueError("output must be tools/release/_build/alpha-source")
    if output.exists() and not (audit_only or refresh):
        raise ValueError("checkout exists; review then use --refresh")
    if (root / "VERSION").read_text(encoding="utf-8").strip() != VERSION:
        raise ValueError("source VERSION does not match Alpha")
    build.mkdir(parents=True, exist_ok=True)
    selected, excluded = collect(root)
    if progress:
        progress("Selected %d source/support candidates; scanning contents" % len(selected))
    overrides = {}
    if prior_public:
        prior = io_path(Path(prior_public).resolve())
        prior_readme = prior / "README.md"
        if prior_readme.is_file():
            banner = prior_readme.read_text(encoding="utf-8").splitlines()[:6]
            if banner and banner[0].startswith('<p align="center">'):
                current = (root / "README.md").read_text(encoding="utf-8").splitlines()[:6]
                if current != banner:
                    raise ValueError("preserve the existing public README banner verbatim before staging")
        logo = prior / "logo.webp"
        if not plain(logo):
            raise ValueError("public logo must be a regular file")
        overrides["logo.webp"] = logo
        templates = prior / ".github/ISSUE_TEMPLATE"
        if templates.is_dir():
            for path in templates.rglob("*"):
                if path.is_file():
                    if not plain(path):
                        raise ValueError("public template cannot be a link")
                    overrides[path.relative_to(prior).as_posix()] = path
    selected = sorted(dict(selected, **overrides).items())
    if len({rel.casefold() for rel, _ in selected}) != len(selected):
        raise ValueError("source contains case-colliding output paths")
    inventory, findings, total = [], [], 0
    for row, matches, rejected in ordered_io(inspect_source, selected):
        if rejected:
            excluded.append(rejected)
            continue
        total += row["bytes"]
        if total > max_bytes:
            raise ValueError("source exceeds staging budget; review dependencies, never truncate silently")
        findings.extend(matches)
        inventory.append(row)
        if progress and len(inventory) % 10000 == 0:
            progress("Audited %d files (%d MiB)" % (len(inventory), total // 1024**2))
    audit = {"version": VERSION, "findings": findings,
             "passed": not any(r["status"] == "blocked" for r in findings),
             "scan_scope": "every selected source/text file; no credential text in reports",
             "limits": "shape/path checks are not exhaustive secret detection or legal clearance",
             "rationale": [
                 "No arbitrary regex removal of credentials; unknown findings block the checkout.",
                 "Finite inspected authored fixture paths use public input placeholders.",
                 "Generic /root build examples and target /home paths are not owner identities.",
                 "Private checkpoints and controller captures are excluded before scanning.",
                 "Upstream fixtures require exact matched-hash review, never directory-wide exceptions.",
             ]}
    reports = {
        "SOURCE-AUDIT.json": audit,
        "SOURCE-EXCLUSIONS.json": {
            "version": VERSION, "paths": sorted(excluded, key=lambda r: r["path"]),
            "pruned_subtree_descendants_not_enumerated": True},
        "SOURCE-INVENTORY.json": {
            "version": VERSION, "files": inventory, "bytes": total,
            "generated_reports_in_inventory": False,
            "upload_performed": False, "exact_old_image_corresponding_source": False},
        "SOURCE-LICENSE-SCOPE.json": {
            "authored_license": "MPL-2.0", "creator": "Jos\u00e9 Pilas",
            "retained_component_notices": [r["path"] for r in inventory
                if PurePosixPath(r["path"]).name.lower().startswith(
                    ("license", "copying", "copyright", "notice"))],
            "approved_assets": {r["path"]: ASSET_ALLOWLIST.get(
                r["path"], "owner-provided existing public project logo")
                for r in inventory if r["path"] in ASSET_ALLOWLIST or r["path"] == "logo.webp"},
            "no_blanket_relicensing": True,
            "dependency_support_data": "textual tables, BDF/XPM fonts, shaders and build inputs retain component terms",
            "limits": "unreviewed opaque assets omitted; fonts/libraries also acquired by pinned Buildroot recipes"},
    }
    for name, report in reports.items():
        write_json(build / ("alpha-source-" + name[7:].lower()), report)
    if audit_only or not audit["passed"]:
        result = {"passed": audit["passed"], "audit_only": True, "files": len(inventory),
                  "bytes": total, "blocked": sum(r["status"] == "blocked" for r in findings),
                  "source_snapshot_created": False, "upload_performed": False}
        write_json(build / "alpha-source-summary.json", result)
        return result
    required = {"Config.in", "external.mk", "LICENSE", "LICENSES/MPL-2.0.txt",
                "package/lilsmoke-ui/src/src/main.cpp",
                "tools/lilsmoke-sdk/lilsmoke_sdk.py", "tools/release/stage_alpha_source.py"}
    paths = {r["path"] for r in inventory}
    if not required <= paths:
        raise ValueError("not a complete authored external tree: " + repr(sorted(required - paths)))
    for rel in ("Config.in", "external.mk"):
        text = public_copy(rel, dict(selected)[rel].read_bytes())[0].decode("utf-8")
        for name in re.findall(r"package/[^\s\"']+/(?:Config\.in|[^\s/]+\.mk)", text):
            if name not in paths:
                raise ValueError("missing active external-tree registration: " + name)
    if output.exists():
        safe_refresh(root, output)
    io_path(output).mkdir(parents=True)
    write_json(output / ".alpha-source-staging.json", {"tool": "stage_alpha_source.py"})
    source_paths = dict(selected)
    def copy_source(row):
        rel = row["path"]
        data, _ = public_copy(rel, source_paths[rel].read_bytes())
        if digest(data) != row["sha256"]:
            raise ValueError("source changed during staging: " + rel)
        target = io_path(output / rel)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        target.chmod(int(row["mode"], 8))
    for _ in ordered_io(copy_source, inventory):
        pass
    for name, report in reports.items():
        write_json(output / name, report)
    archive = build / ("LilSmokeOS-" + VERSION + "-source.zip")
    # Explicit ZipInfo, order, modes and timestamp make identical inputs repeatable.
    with zipfile.ZipFile(io_path(archive), "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        archive_rows = inventory + [{"path": name, "mode": "0o644"} for name in reports]
        for row in sorted(archive_rows, key=lambda r: r["path"]):
            entry = zipfile.ZipInfo(row["path"], (2026, 1, 1, 0, 0, 0))
            entry.create_system = 3
            entry.external_attr = (stat.S_IFREG | int(row["mode"], 8)) << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(entry, io_path(output / row["path"]).read_bytes(), compresslevel=6)
    h = hashlib.sha256()
    with io_path(archive).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    result = {"version": VERSION, "passed": True, "files": len(inventory), "bytes": total,
              "archive": archive.name, "archive_bytes": archive.stat().st_size,
              "archive_sha256": h.hexdigest(), "upload_performed": False,
              "excluded_entries": len(excluded), "reviewed_findings": len(findings),
              "areas": dict(sorted(Counter(r["path"].split("/")[0] for r in inventory).items()))}
    write_json(build / "alpha-source-summary.json", result)
    return result


def finalize_staged(root, progress=None):
    """Finish a verified copy and synchronize only frozen release/QA metadata.

    Reuse the completed content audit, verify every retained staged byte while
    packing, and re-audit changed release inputs. Never rescan/copy another
    full source tree or touch Git metadata.
    """
    root = Path(root).resolve()
    build = root / "tools/release/_build"
    output = build / "alpha-source"
    if build.resolve() != build or not plain(output):
        raise ValueError("unsafe staging path")
    marker = json.loads((output / ".alpha-source-staging.json").read_text())
    if marker != {"tool": "stage_alpha_source.py"}:
        raise ValueError("not an owned source staging checkout")
    names = ("SOURCE-AUDIT.json", "SOURCE-EXCLUSIONS.json",
             "SOURCE-INVENTORY.json", "SOURCE-LICENSE-SCOPE.json")
    reports = {name: json.loads((build / ("alpha-source-" + name[7:].lower())).read_text())
               for name in names}
    audit = reports["SOURCE-AUDIT.json"]
    if not audit["passed"] or any(r["status"] == "blocked" for r in audit["findings"]):
        raise ValueError("a complete passed audit is required before finalization")
    old_rows = reports["SOURCE-INVENTORY.json"]["files"]
    rows = {r["path"]: r for r in old_rows}
    candidates = ROOT_FILES | {"tools/release/" + name for name in RELEASE_TOOLS}
    candidates |= {"tools/m8/candidate_vm_smoke.py"}
    changed, removed, restored = [], [], 0
    def reconcile(rel):
        parts = PurePosixPath(rel).parts
        if not parts or ".." in parts or PurePosixPath(rel).is_absolute() or "\\" in rel:
            raise ValueError("unsafe inventory path")
        target = io_path(output / rel)
        original = io_path(root / rel)
        old = rows.get(rel)
        reason = exclusion(rel)
        if reason and old:
            if target.exists():
                if not plain(target) or digest(target.read_bytes()) != old["sha256"]:
                    raise ValueError("refusing to remove modified staged source: " + rel)
                target.unlink()
            return rel, None, None, "removed", reason
        if reason:
            return rel, old, None, "unchanged", None
        update = rel in candidates and original.is_file()
        source_changed = update and (old is None or digest(original.read_bytes()) != old["source_sha256"])
        if source_changed or (old and not target.exists()):
            if not original.is_file() or not plain(original):
                raise ValueError("missing regular source input: " + rel)
            if source_changed:
                row, matches, rejected = inspect_source((rel, original))
                if rejected or any(r["status"] == "blocked" for r in matches):
                    raise ValueError("changed source requires review: " + rel)
            else:
                row, matches = old, None
            raw = original.read_bytes()
            if not source_changed and digest(raw) != old["source_sha256"]:
                raise ValueError("non-release source changed after audit: " + rel)
            data, _ = public_copy(rel, raw)
            if digest(data) != row["sha256"]:
                raise ValueError("release input changed during finalization: " + rel)
            if old and target.exists() and digest(target.read_bytes()) not in {old["sha256"], row["sha256"]}:
                raise ValueError("staged source modified outside finalization: " + rel)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            target.chmod(int(row["mode"], 8))
            return rel, row, matches, "changed" if source_changed else "restored", None
        return rel, old, None, "unchanged", None
    for index, (rel, row, matches, action, reason) in enumerate(ordered_io(
            reconcile, sorted(set(rows) | candidates), workers=32)):
        if action == "removed":
            rows.pop(rel)
            reports["SOURCE-EXCLUSIONS.json"]["paths"].append({"path": rel, "reason": reason})
            removed.append(rel)
        elif row is not None:
            rows[rel] = row
            if matches is not None:
                audit["findings"] = [r for r in audit["findings"] if r["path"] != rel] + matches
            if action == "changed":
                changed.append(rel)
            if action == "restored":
                restored += 1
        if progress and index and index % 10000 == 0:
            progress("Reconciled %d audited files" % index)
    inventory = sorted(rows.values(), key=lambda r: r["path"])
    audit["findings"].sort(key=lambda r: (r["path"], r["line"], r["rule"]))
    total = sum(r["bytes"] for r in inventory)
    reports["SOURCE-INVENTORY.json"].update(files=inventory, bytes=total)
    excluded = reports["SOURCE-EXCLUSIONS.json"]["paths"]
    reports["SOURCE-EXCLUSIONS.json"]["paths"] = sorted(
        {r["path"]: r for r in excluded}.values(), key=lambda r: r["path"])
    for name, report in reports.items():
        write_json(output / name, report)
        write_json(build / ("alpha-source-" + name[7:].lower()), report)
    archive = build / ("LilSmokeOS-" + VERSION + "-source.zip")
    temporary = archive.with_suffix(".partial.zip")
    if temporary.exists() and not plain(temporary):
        raise ValueError("unsafe partial archive")
    with zipfile.ZipFile(io_path(temporary), "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        archive_rows = inventory + [{"path": name, "mode": "0o644"} for name in reports]
        for index, row in enumerate(sorted(archive_rows, key=lambda r: r["path"])):
            target = io_path(output / row["path"])
            if not plain(target):
                raise ValueError("non-regular staged source: " + row["path"])
            data = target.read_bytes()
            if "sha256" in row and (len(data) != row["bytes"] or digest(data) != row["sha256"]):
                raise ValueError("staged content changed: " + row["path"])
            entry = zipfile.ZipInfo(row["path"], (2026, 1, 1, 0, 0, 0))
            entry.create_system = 3
            entry.external_attr = (stat.S_IFREG | int(row["mode"], 8)) << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(entry, data, compresslevel=6)
            if progress and index and index % 10000 == 0:
                progress("Verified and archived %d files" % index)
    os.replace(io_path(temporary), io_path(archive))
    h = hashlib.sha256()
    with io_path(archive).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    result = {"version": VERSION, "passed": True, "files": len(inventory), "bytes": total,
              "archive": archive.name, "archive_bytes": archive.stat().st_size,
              "archive_sha256": h.hexdigest(), "upload_performed": False,
              "reviewed_findings": len(audit["findings"]), "blocked": 0,
              "excluded_entries": len(reports["SOURCE-EXCLUSIONS.json"]["paths"]),
              "updated_release_inputs": changed, "removed_retired_source": removed,
              "resumed_copy_files": restored,
              "areas": dict(sorted(Counter(r["path"].split("/")[0] for r in inventory).items()))}
    write_json(build / "alpha-source-summary.json", result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--prior-public", type=Path,
                        help="read-only existing public checkout, for approved logo/templates only")
    parser.add_argument("--audit-only", action="store_true")
    parser.add_argument("--refresh", action="store_true")
    parser.add_argument("--finalize-staged", action="store_true",
                        help="synchronize frozen release inputs and verify/pack the existing audited checkout")
    args = parser.parse_args()
    if args.finalize_staged:
        result = finalize_staged(args.root, progress=print)
    else:
        result = stage(args.root, prior_public=args.prior_public,
                       audit_only=args.audit_only, refresh=args.refresh, progress=print)
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
