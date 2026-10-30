#!/usr/bin/env python3
"""Real signed 7z fixtures against the native validator; no hardware writes."""
import ctypes
import ctypes.util
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from nacl.signing import SigningKey

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "test-results/syspck"
OUT.mkdir(parents=True, exist_ok=True)
BIN = Path(os.environ.get("SYSPCK_BINARY", str(OUT / "lilsmoke-syspck-host")))
RUNNER = json.loads(os.environ.get("SYSPCK_RUNNER", "[]"))
checks = []

def check(value, label):
    if not value:
        raise AssertionError(label)
    checks.append(label)
    print("PASS", label, flush=True)

def run(args, env=None):
    result = subprocess.run(list(map(str, args)), env=env, text=True,
                            capture_output=True, timeout=40)
    if result.returncode < 0:
        raise RuntimeError("native validator crashed: " + result.stderr)
    if result.returncode:
        print(result.stderr.strip(), flush=True)
    return result

# Libarchive's 7z writer provides deliberately unsafe archive path fixtures.
lib = ctypes.CDLL(ctypes.util.find_library("archive"))
for name in ["archive_write_new", "archive_entry_new"]:
    getattr(lib, name).restype = ctypes.c_void_p
for name in ["archive_write_set_format_7zip", "archive_write_close", "archive_write_free",
             "archive_entry_free"]:
    getattr(lib, name).argtypes = [ctypes.c_void_p]
lib.archive_write_open_filename.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
lib.archive_write_set_options.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
lib.archive_entry_set_pathname.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
lib.archive_entry_set_filetype.argtypes = [ctypes.c_void_p, ctypes.c_uint]
lib.archive_entry_set_perm.argtypes = [ctypes.c_void_p, ctypes.c_uint]
lib.archive_entry_set_size.argtypes = [ctypes.c_void_p, ctypes.c_longlong]
lib.archive_entry_set_symlink.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
lib.archive_write_header.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
lib.archive_write_data.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
lib.archive_write_data.restype = ctypes.c_ssize_t

def archive(path, members):
    a = lib.archive_write_new()
    assert lib.archive_write_set_format_7zip(a) == 0
    assert lib.archive_write_set_options(a, b"7zip:compression=store") == 0
    assert lib.archive_write_open_filename(a, os.fsencode(path)) == 0
    try:
        for name, data, link in members:
            entry = lib.archive_entry_new()
            try:
                lib.archive_entry_set_pathname(entry, name.encode())
                lib.archive_entry_set_filetype(entry, 0o120000 if link else 0o100000)
                lib.archive_entry_set_perm(entry, 0o644)
                lib.archive_entry_set_size(entry, len(data))
                if link:
                    lib.archive_entry_set_symlink(entry, link.encode())
                assert lib.archive_write_header(a, entry) == 0
                if data:
                    assert lib.archive_write_data(a, data, len(data)) == len(data)
            finally:
                lib.archive_entry_free(entry)
        assert lib.archive_write_close(a) == 0
    finally:
        lib.archive_write_free(a)

with tempfile.TemporaryDirectory(prefix="syspck-qa-") as directory:
    work = Path(directory)
    key = SigningKey.generate()
    keys = work / "keys"
    keys.mkdir()
    (keys / "qa.pub").write_bytes(bytes(key.verify_key))
    (work / "private.key").write_bytes(bytes(key))
    empty = work / "empty"
    empty.mkdir(mode=0o755)
    persist = work / "persist"
    (persist / "config").mkdir(parents=True)
    original = b"SSH_ENABLED=off\nDEFAULT_THEME=default\nSCALING_MODE=integer\n"
    (persist / "config/lilsmoke.conf").write_bytes(original)
    policy_values = {"PolicyVersion": "1", "Board": "qemu-mipsel",
                     "HardwareRevision": "malta-dev", "SoC": "qemu-malta",
                     "Architecture": "mipsel", "ABI": "o32-hard-float", "Libc": "musl",
                     "KernelID": "qa-kernel-1", "RAMMB": "128",
                     "DTBinding": "linux-hichip-qa", "DTBCompatible": "datafrog,sf3000",
                     "HardwareValidated": "no", "ApprovedDTBSHA256": "none"}
    policy = work / "policy.conf"
    def write_policy():
        policy.write_text("".join(k + "=" + v + "\n" for k, v in policy_values.items()))
    write_policy()
    env = dict(os.environ, LILSMOKE_SYSPCK_KEYS=str(keys), LILSMOKE_SYSPCK_POLICY=str(policy),
               LILSMOKE_SYSPCK_PERSIST=str(persist), LILSMOKE_SYSPCK_EMPTY=str(empty))
    values = {k: policy_values[k] for k in ["Board", "HardwareRevision", "SoC",
                                          "Architecture", "ABI", "Libc", "KernelID"]}
    values.update(FormatVersion="1", PackageID="qa-safe-profile", Version="1.0",
                  DTBinding="none", Kind="userspace", RequiredRAMMB="128", KeyID="qa")
    config = b"CPU_OVERCLOCK=stock\nSCALING_MODE=aspect\nZRAM_ENABLED=on\nZRAM_SIZE_MB=32\n"
    counter = 0
    def make(changes=None, contents=config, dtb=None, additions=None, modify=None):
        global counter
        counter += 1
        fields = dict(values)
        fields.update(changes or {})
        fields["ConfigSHA256"] = hashlib.sha256(contents).hexdigest()
        if dtb is not None:
            fields["DTBSHA256"] = hashlib.sha256(dtb).hexdigest()
        manifest = ("[SystemPackage]\n" + "".join(k+"="+v+"\n" for k,v in sorted(fields.items()))).encode()
        signature = key.sign(manifest).signature
        members = [("manifest.ini", manifest, None), ("manifest.sig", signature, None),
                   ("config.conf", contents, None)]
        if dtb is not None:
            members.append(("dtb.bin", dtb, None))
        members.extend(additions or [])
        if modify:
            members = modify(members)
        path = work / f"fixture-{counter}.syspck"
        archive(path, members)
        return path

    def call(command, path=None, localenv=env):
        return run([*RUNNER, BIN, command, *([path] if path else [])], localenv)
    def reject(path, label, reason=None, command="check"):
        p = call(command, path)
        check(p.returncode == 1 and (not reason or reason in p.stderr),
              label + ": " + p.stderr.strip())

    good = make()
    tv = make(contents=b"DEVICE_MODE=tv\nTV_SAFE_AREA_PERCENT=5\nSHOW_BATTERY=off\n")
    check(call("check", tv).returncode == 0, "signed TV presentation profile accepted")
    for contents in [b"DEVICE_MODE=android\n", b"TV_SAFE_AREA_PERCENT=99\n",
                     b"DISPLAY_WIDTH=3840\n", b"SHOW_BATTERY=fake\n"]:
        reject(make(contents=contents), "unsafe TV/display profile", "unsupported setting")
    check(call("check", good).returncode == 0, "signed real 7z validates")
    check(call("stage", good).returncode == 0, "signed package stages")
    digest = hashlib.sha256(good.read_bytes()).hexdigest()
    staged = persist / "syspck/packages" / (digest + ".syspck")
    check(staged.read_bytes() == good.read_bytes(), "staged archive is byte-identical")
    check(call("apply", staged).returncode == 0, "native userspace apply succeeds")
    applied = (persist / "config/lilsmoke.conf").read_bytes()
    check(b"SSH_ENABLED=off" in applied and b"SCALING_MODE=aspect" in applied and
          b"ZRAM_SIZE_MB=32" in applied, "optimization settings apply without enabling network access")
    check((persist / "syspck/previous.conf").read_bytes() == original, "rollback preserves exact old config")
    check(call("rollback").returncode == 0 and (persist / "config/lilsmoke.conf").read_bytes() == original,
          "rollback restores original bytes")
    check(call("apply", staged).returncode == 0, "package can be reapplied")
    (persist / "syspck/pending").write_text(digest + "\n")
    (persist / "config/lilsmoke.conf.new").write_text("incomplete write")
    check(call("recover").returncode == 0 and (persist / "config/lilsmoke.conf").read_bytes() == original,
          "boot recovery reverts interrupted transaction")
    check(call("recover").returncode == 0, "recovery is idempotent")
    stale = persist / "syspck/active.new"
    stale.write_bytes(b"interrupted metadata write")
    reject(good, "stale transaction fails before modifying config", "unfinished transaction", "apply")
    check((persist / "config/lilsmoke.conf").read_bytes() == original,
          "stale metadata leaves live config unchanged")
    stale.unlink()
    stale.symlink_to(work / "absent")
    reject(good, "dangling transaction symlink fails before modifying config", "unfinished transaction", "apply")
    stale.unlink()
    live = persist / "config/lilsmoke.conf"
    live.unlink()
    reject(good, "uninitialized settings fail closed", "protected file", "apply")
    check(not live.exists(), "failed early install does not create a sparse config")
    live.write_bytes(original)

    for field, wrong in {"Board": "sf3000", "HardwareRevision": "wrong-panel",
                         "SoC": "ingenic-jz4770", "Architecture": "aarch64",
                         "ABI": "o32-soft-float", "Libc": "glibc", "KernelID": "another-kernel"}.items():
        reject(make({field: wrong}), "wrong " + field, "incompatible " + field)
    reject(make({"RequiredRAMMB": "256"}), "over-budget RAM", "RAM requirement")
    reject(make({"KeyID": "../outside"}), "trust-store path traversal", "identity")
    reject(make({"KeyID": "unknown"}), "unknown signing key", "protected file")
    reject(make({"FormatVersion": "2"}), "future unknown version", "unsupported version")
    reject(make({"InstallCommand": "touch /tmp/syspck-executed"}), "unknown manifest directive", "manifest key")
    for contents in [b"CPU_OVERCLOCK=1400MHz\n", b"USB_MODE=network+drive\n",
                     b"SCALING_MODE=$(touch /tmp/syspck-executed)\n",
                     b"ZRAM_SIZE_MB=128\n", b"ZRAM_ENABLED=on\nZRAM_ENABLED=off\n",
                     b"[Settings]\nSCALING_MODE=aspect\n", b"SCALING_MODE=aspect\x00\n"]:
        reject(make(contents=contents), "unsafe/unsupported config")
    for name in ["../escape", "/tmp/syspck-escape", "directory/config.conf", "install.sh", "dtb.bin/../x"]:
        reject(make(additions=[(name, b"x", None)]), "unsafe archive member " + name, "member path")
    reject(make(additions=[("config.conf", b"x", None)]), "duplicate archive member", "member path")
    reject(make(modify=lambda m: [(n, b"", "/etc/passwd") if n == "config.conf" else (n,d,l)
                                 for n,d,l in m]), "symbolic link archive member", "links")
    reject(make(modify=lambda m: [(n, d[:-1]+b"x", l) if n == "manifest.sig" else (n,d,l)
                                 for n,d,l in m]), "signature tampering", "signature")
    reject(make(modify=lambda m: [(n, d+b"#changed\n", l) if n == "config.conf" else (n,d,l)
                                 for n,d,l in m]), "payload tampering", "hash mismatch")
    reject(make(contents=b"x" * 8193), "expanded config limit", "too large")
    reject(make({"Kind": "reference", "DTBinding": "cubegm-vendor"}, dtb=b"not a dtb"),
           "invalid DTB structure", "DTB")
    stock = (OUT / "research/sf3000-stock-dtb.bin").read_bytes()
    reference = make({"Kind": "reference", "DTBinding": "cubegm-vendor"}, dtb=stock)
    check(call("check", reference).returncode == 0, "actual CubeGM DTB accepted as signed reference")
    check(call("stage", reference).returncode == 0, "reference hardware layout can be staged safely")
    reject(reference, "reference cannot alter running system", "cannot be applied", "apply")
    reject(reference, "DTB activation remains gated", "boot/rollback adapter", "activate-dtb")
    reject(make(dtb=stock), "userspace cannot smuggle a boot DTB", "cannot replace")
    reject(make({"Kind": "hardware", "DTBinding": "linux-hichip-qa"}, dtb=stock),
           "unverified physical profile", "unverified")
    policy_values["HardwareValidated"] = "yes"
    policy_values["ApprovedDTBSHA256"] = hashlib.sha256(stock).hexdigest()
    write_policy()
    reject(make({"Kind": "hardware", "DTBinding": "linux-hichip-qa"}, dtb=stock),
           "vendor DTB cannot masquerade as linux-hichip DTB", "root compatible")
    for size, label in [("0x20000000", "DTB memory exceeds hardware"), ("0x08000000", "hardware DTB stages")]:
        dts = work / "qa.dts"
        dtb = work / "qa.dtb"
        dts.write_text('/dts-v1/; / { compatible="datafrog,sf3000"; #address-cells=<1>; #size-cells=<1>; '
                       'memory { device_type="memory"; reg=<0 '+size+'>; }; };')
        p = run(["dtc", "-I", "dts", "-O", "dtb", dts, "-o", dtb])
        assert p.returncode == 0, p.stderr
        raw = dtb.read_bytes()
        policy_values["ApprovedDTBSHA256"] = hashlib.sha256(raw).hexdigest()
        write_policy()
        package = make({"Kind": "hardware", "DTBinding": "linux-hichip-qa"}, dtb=raw)
        if size == "0x20000000":
            reject(package, label, "console RAM")
        else:
            check(call("stage", package).returncode == 0, label + " with approved exact SHA256")
            reject(package, "approved DTB still needs physical boot adapter", "boot/rollback adapter", "activate-dtb")
            policy_values["ApprovedDTBSHA256"] = "0" * 64
            write_policy()
            reject(package, "unapproved exact DTB hash", "not approved")
    policy_values["HardwareValidated"] = "no"
    write_policy()
    link = work / "symlink.syspck"
    link.symlink_to(good)
    reject(link, "symlinked package file", "protected file")
    bad = work / "not7z.syspck"
    bad.write_bytes(b"not a 7z file" * 4)
    reject(bad, "renaming alone is not validity", "renamed 7z")
    for prefix, label in [(b"\x7fELF" + bytes(60), "ELF self-extracting archive"),
                          (b"MZ" + bytes(62), "PE self-extracting archive")]:
        bad = work / "sfx.syspck"
        bad.write_bytes(prefix + good.read_bytes())
        reject(bad, label, "renamed 7z")
    missing_jail = dict(env, LILSMOKE_SYSPCK_EMPTY=str(work / "missing-jail"))
    check(call("check", good, missing_jail).returncode == 1, "missing decoder jail fails closed")
    empty.chmod(0o777)
    check(call("check", good).returncode == 1, "writable decoder jail fails closed")
    empty.chmod(0o755)
    bad = work / "too-large.syspck"
    with bad.open("wb") as f:
        f.truncate(2 * 1024 * 1024 + 1)
    reject(bad, "compressed size limit", "file type/size")
    with (persist / "syspck/lock").open("rb") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        reject(good, "exclusive installation lock", "already running", "apply")
    check(not Path("/tmp/syspck-executed").exists() and not Path("/tmp/syspck-escape").exists(),
          "archive/config validation never executes package code or writes outside staging")
    # Fixed LILOS-root selection: no arguments or discovery on the ROM partition.
    lilos = work / "lilos"
    lilos.mkdir()
    default = work / "board-default.conf"
    default.write_bytes(original)
    boot_env = dict(env, LILSMOKE_SYSPCK_LILOS=str(lilos),
                    LILSMOKE_SYSPCK_DEFAULT=str(default))
    boot = lambda: call("boot-select", localenv=boot_env)
    check(boot().returncode == 0 and not (persist / "syspck/boot-selected").exists(),
          "absent main.syspck preserves existing board defaults")
    (lilos / "other.syspck").write_bytes(good.read_bytes())
    check(boot().returncode == 0 and not (persist / "syspck/boot-selected").exists(),
          "other filenames are not auto-selected")
    main = lilos / "main.syspck"
    main.write_bytes(good.read_bytes())
    check(boot().returncode == 0, "LILOS-root main.syspck applies trusted matching userspace profile")
    applied_once = live.read_bytes()
    previous_once = (persist / "syspck/previous.conf").read_bytes()
    live.write_bytes(applied_once + b"POINTER_SPEED=fast\n")
    check(boot().returncode == 0 and live.read_bytes().endswith(b"POINTER_SPEED=fast\n"),
          "unchanged main.syspck preserves settings edited after selection")
    check((persist / "syspck/previous.conf").read_bytes() == previous_once,
          "unchanged boot does not replace rollback backup")
    marker = persist / "syspck/boot-selected"
    marker.unlink()
    check(boot().returncode == 0 and live.read_bytes().endswith(b"POINTER_SPEED=fast\n"),
          "committed profile missing marker is recorded without duplicate application")
    main.write_bytes(make({"Board": "wrong-board"}).read_bytes())
    check(boot().returncode == 1 and live.read_bytes().endswith(b"POINTER_SPEED=fast\n"),
          "wrong main.syspck board fails closed without changing settings")
    main.write_bytes(reference.read_bytes())
    check(boot().returncode == 1, "reference main.syspck cannot masquerade as bootable hardware")
    main.unlink()
    main.symlink_to(good)
    check(boot().returncode == 1, "symlink main.syspck rejected")
    main.unlink()
    main.write_bytes(good.read_bytes())
    marker.unlink()
    (persist / "syspck/active").unlink()
    live.unlink()
    check(boot().returncode == 0 and live.read_bytes().startswith(original),
          "first boot initializes full board defaults before profile settings")
    check(boot().returncode == 0, "first boot selector is idempotent")
    marker.unlink()
    (persist / "syspck/active").unlink()
    stale_marker = persist / "syspck/boot-selected.new"
    stale_marker.write_bytes(b"incomplete")
    before = live.read_bytes()
    check(boot().returncode == 1 and live.read_bytes() == before,
          "stale marker fails before config modifications")
    stale_marker.unlink()
    (persist / "syspck/pending").write_bytes(b"incomplete")
    check(boot().returncode == 1, "boot selection refuses unrecovered pending installation")
    (persist / "syspck/pending").unlink()

    staged.write_bytes(b"tampered staging")
    reject(good, "existing staging corruption is refused", "staged file changed", "stage")
    staged.unlink()

    # Exercise the author's actual p7zip configuration, not only fixture archives.
    profile = work / "profile.ini"
    profile.write_text("[SystemPackage]\n" + "".join(k+"="+v+"\n" for k,v in values.items()))
    (work / "config.conf").write_bytes(config)
    authored = work / "authored.syspck"
    result = run(["python3", ROOT / "tools/syspck/make_syspck.py", "pack", "--profile", profile,
                  "--config", work/"config.conf", "--private", work/"private.key", "--output", authored])
    check(result.returncode == 0 and call("check", authored).returncode == 0,
          "host author emits compatible non-solid LZMA2 renamed 7z")
    examples = OUT / "examples"
    examples.mkdir(exist_ok=True)
    (examples / "qa-safe-profile.syspck").write_bytes(authored.read_bytes())
    (examples / "qa-reference.syspck").write_bytes(reference.read_bytes())
    (examples / "UNTRUSTED-qa.pub").write_bytes(bytes(key.verify_key))
    (examples / "qa-policy.conf").write_bytes(policy.read_bytes())

(OUT / "checks.json").write_text(json.dumps({"passed": len(checks), "checks": checks}, indent=2) + "\n")
print("SYSPCK_SMOKE_OK checks=" + str(len(checks)))
