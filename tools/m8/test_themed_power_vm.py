#!/usr/bin/env python3
"""Bounded production themed UI -> Power Off regression with real MIPS DSOs.

Run under WSL as root. Only a disposable evidence directory is writable via 9p;
the SDK and original theme assets are read-only. No SD device or host input API.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import socket
import subprocess
import sys
import tempfile
import time

P = Path(__file__).resolve().parents[2]
SOURCES = (
    "tools/m8/themed_power_test.cpp",
    *("package/lilsmoke-ui/src/src/" + name + ".cpp" for name in (
        "Log", "Config", "Theme", "ThemeRender", "Renderer", "Font", "Input",
        "Keymap", "Cursor", "SystemView", "SettingsView", "ProcessLaunch")),
)
sys.path.insert(0, str(P / "tools/hwtest"))
from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs, runtime_libraries


def sha256(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


def source_qualification(manifest):
    snapshot = Path(manifest["source_snapshot"])
    built, current, differences = [], [], []
    for item in manifest["inputs"]:
        path = Path(item["path"])
        if not path.is_relative_to(snapshot):
            continue
        relative = path.relative_to(snapshot).as_posix()
        live = P / relative
        digest = sha256(live) if live.is_file() else None
        built.append(dict(path=relative, sha256=item["sha256"]))
        current.append(dict(path=relative, sha256=digest))
        if digest != item["sha256"]:
            differences.append(dict(path=relative, built_sha256=item["sha256"], current_sha256=digest))
    if not built:
        raise RuntimeError("source manifest has no project source fingerprints")
    def fingerprint(inputs):
        ordered = sorted(inputs, key=lambda item: item["path"])
        return hashlib.sha256(json.dumps(ordered, sort_keys=True).encode()).hexdigest()
    return dict(build_fingerprint=fingerprint(built), current_fingerprint=fingerprint(current),
                matches_current=not differences, differences=differences, inputs=built)


def checked(command, **kwargs):
    return subprocess.run(list(map(str, command)), check=True, text=True,
                          capture_output=True, timeout=180, **kwargs).stdout


def vm_diagnostic(path):
    """Read VM state only. Never send keys, continue, reset, or guest commands."""
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(2)
            connection.connect(str(path))
            with connection.makefile("rwb", buffering=0) as stream:
                greeting = json.loads(stream.readline())
                answers = dict(greeting=greeting)
                for command, arguments in (("qmp_capabilities", {}), ("query-status", {}),
                                           ("human-monitor-command", {"command-line": "info registers"})):
                    stream.write((json.dumps(dict(execute=command, arguments=arguments)) + "\n").encode())
                    for _ in range(32):
                        reply = json.loads(stream.readline())
                        if "event" not in reply:
                            answers[command] = reply
                            break
                    else:
                        raise RuntimeError("QMP event limit reached")
                return answers
    except (OSError, ValueError, RuntimeError) as error:
        return dict(error=str(error))


def build(args, out):
    objdir = out / "objects"
    objdir.mkdir()
    snapshot = out / "source-snapshot"
    inputs = {P / name for name in SOURCES}
    for folder in ("package/lilsmoke-ui/src/src", "package/lilsmoke-scripts/src", "package/lilsmoke-media/src"):
        inputs.update((P / folder).rglob("*.h"))
    contents = {path: path.read_bytes() for path in inputs}
    if any(path.read_bytes() != data for path, data in contents.items()):
        raise RuntimeError("production sources changed while taking snapshot; rerun")
    for path, data in contents.items():
        destination = snapshot / path.relative_to(P)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
    compiler = args.sdk / "host/bin/mipsel-buildroot-linux-musl-g++"
    packages = ["sdl2", "SDL2_image", "SDL2_ttf", "libpng", "json-c"]
    pkg = args.sdk / "host/bin/pkg-config"
    flags = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter",
             "-ffunction-sections", "-fdata-sections", "-MMD", "-MP",
             "-I" + str(snapshot / "package/lilsmoke-ui/src/src"),
             "-I" + str(snapshot / "package/lilsmoke-scripts/src"),
             "-I" + str(snapshot / "package/lilsmoke-media/src"),
             *shlex.split(checked([pkg, "--cflags", *packages]))]
    commands, objects = [], []
    with (out / "build.log").open("w") as log:
        for index, name in enumerate(SOURCES):
            obj = objdir / (str(index) + "-" + Path(name).stem + ".o")
            command = [str(compiler), *flags, "-c", str(snapshot / name), "-o", str(obj)]
            commands.append(command)
            print("Building", Path(name).name, flush=True)
            subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=180)
            objects.append(obj)
        binary = out / "themed-power"
        command = [str(compiler), "-Wl,--gc-sections", "-o", str(binary), *map(str, objects),
                   *shlex.split(checked([pkg, "--libs", *packages])), "-lz", "-lwebp", "-ldl"]
        commands.append(command)
        subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=180)
    dependencies = {snapshot / name for name in SOURCES}
    for dep in objdir.glob("*.d"):
        tokens = shlex.split(dep.read_text().replace("\\\n", " ").split(":", 1)[1])
        dependencies.update(Path(token) for token in tokens if not token.endswith(":"))
    manifest = dict(sources=list(SOURCES), main_cpp_linked=False, commands=commands,
                    source_snapshot=str(snapshot),
                    compiler=str(compiler), compiler_version=checked([compiler, "--version"]),
                    inputs=[dict(path=str(path), sha256=sha256(path))
                            for path in sorted(dependencies) if path.is_file()],
                    binary_sha256=sha256(binary))
    write_json(out / "sources-manifest.json", manifest)
    return binary


def cases(args, themes):
    result = []
    for index, theme in enumerate(themes):
        for repeat in range(args.repeats):
            result.append(dict(id=f"theme-{index}-run-{repeat}", theme=theme,
                               mode="poweroff", expected_exit=0, request=True,
                               capture=repeat == 0))
    # Both negative paths must finish without a request and permit a fresh UI run.
    result.extend([
        dict(id="helper-refused", theme="default", mode="refused", expected_exit=75,
             request=False, capture=False),
        dict(id="exec-failure", theme="default", mode="exec-failure", expected_exit=1,
             request=False, capture=False),
        dict(id="relaunch-after-failure", theme="default", mode="poweroff", expected_exit=0,
             request=True, capture=False),
    ])
    return result


def guest_init(args, tests):
    lines = [r'''#!/bin/sh
exec </dev/console >/dev/console 2>&1
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
fail() { echo "QA_GUEST_FAILURE=$*"; /bin/busybox poweroff -f; exit 1; }
mkdir -p /bench /qa-bin /dev/pts /dev/shm /run /persist/config /storage/themes /run/themed-power-input
mount -t devpts devpts /dev/pts || fail devpts
mount -t tmpfs -o size=8m tmpfs /dev/shm || fail shm
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || fail bench
mount -t 9p -o ro,trans=virtio,version=9p2000.L qa-bin /qa-bin || fail binary
mount -t 9p -o ro,trans=virtio,version=9p2000.L themes /storage/themes || fail themes
export HOME=/persist SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force
export LILSMOKE_STOCK_SUPERVISOR=1 LILSMOKE_M8_SAFE_POWEROFF=1 LILSMOKE_HCFB=0
export LILSMOKE_UI_NATIVE_DAMAGE=0 LILSMOKE_SCREEN_WIDTH=856 LILSMOKE_SCREEN_HEIGHT=480
export LILSMOKE_DRAW_CLOCK=0 LILSMOKE_SHOW_BATTERY=off LILSMOKE_INPUT_DIR=/run/themed-power-input
unset LD_PRELOAD LD_LIBRARY_PATH DISPLAY LILSMOKE_UI_HIDDEN LILSMOKE_UI_WINDOWED LILSMOKE_THEME_SUBSETS
cat /proc/meminfo > /bench/memory.txt
cat /proc/mounts > /bench/mounts.txt
uname -a > /bench/uname.txt
sha256sum /usr/lib/libfusion-1.7.so.7 /usr/lib/libdirect-1.7.so.7 /usr/lib/lilsmoke/lilsmoke-power > /bench/guest-sha256.txt
ulimit -c 0
/bin/busybox timeout -k 1 1 /bin/busybox sleep 10
echo "$?" > /bench/timeout-selftest.result
''']
    for test in tests:
        ident = test["id"]
        theme = ("/usr/share/lilsmoke/themes/default" if test["theme"] == "default"
                 else "/storage/themes/" + test["theme"])
        prefix = "/bench/" + ident if test["capture"] else ""
        command = ["/bin/busybox", "timeout", "-k", "5", str(args.child_timeout),
                   "/qa-bin/themed-power", theme, "/qa-font.ttf", str(args.frames), prefix, test["mode"]]
        lines.extend([
            "rm -f /run/lilsmoke-poweroff-request /run/lilsmoke-poweroff-request.new",
            "rmdir /run/lilsmoke-usb/lock 2>/dev/null || true",
            "printf 'SHOW_CLOCK=off\\nSHOW_BATTERY=off\\n' > /persist/config/lilsmoke.conf",
            "export LILSMOKE_M8_SAFE_POWEROFF=" + ("0" if test["mode"] == "refused" else "1"),
            "echo QA_CASE_START=" + ident,
            shlex.join(command) + " > /bench/" + ident + ".log 2>&1",
            "rc=$?",
            "request=0; [ ! -f /run/lilsmoke-poweroff-request ] || request=1",
            'echo "QA_CASE_RESULT=' + ident + ' exit=$rc request=$request" | tee /bench/' + ident + ".result",
            "if [ -f /run/lilsmoke-poweroff-request ]; then cp /run/lilsmoke-poweroff-request /bench/" + ident + ".request; fi",
        ])
    lines.extend(["echo QA_THEMED_POWER_DONE", "sync", "/bin/busybox poweroff -f", "while :; do sleep 60; done"])
    return "\n".join(lines) + "\n"


def fixture(args, tests, pair):
    archive = Newc()
    archive.file("usr/bin/bench-init", guest_init(args, tests), 0o755)
    # Refresh stale rootfs runtime closure, including dlopened DirectFB modules.
    modules = sorted((args.sdk / "target/usr/lib/directfb-1.7-7").rglob("*.so"))
    runtime_libraries(args.sdk, archive, [args.binary, *modules])
    replaced = {"usr/lib/libfusion-1.7.so.7", "usr/lib/libdirect-1.7.so.7"}
    archive.entries = [entry for entry in archive.entries if entry.name not in replaced]
    for name, path in pair.items():
        archive.file("usr/lib/" + name, path.read_bytes(), 0o755)
    for module in modules:
        archive.file(module.relative_to(args.sdk / "target").as_posix(), module.read_bytes(), 0o755)
    archive.file("etc/directfbrc", (P / "board/qemu-mipsel/directfbrc").read_bytes())
    archive.file("usr/lib/lilsmoke/lilsmoke-power",
                 (P / "package/lilsmoke-scripts/src/lilsmoke-power").read_bytes(), 0o755)
    archive.file("usr/share/lilsmoke/cursors/lilsmoke-cursors.webp",
                 (P / "package/lilsmoke-ui/src/assets/lilsmoke-cursors.webp").read_bytes())
    archive.file("qa-font.ttf", args.font.read_bytes())
    # Default assets are small; originals above are shared once, never copied.
    default = args.sdk / "target/usr/share/lilsmoke/themes/default"
    for path in sorted(default.rglob("*")):
        if path.is_file():
            archive.file("usr/share/lilsmoke/themes/default/" + path.relative_to(default).as_posix(), path.read_bytes())
    return gzip.compress(overlay_initramfs(args.sdk, archive, "/usr/bin/bench-init",
                                          overlay_mib=32, rootfs_in_ram=True), compresslevel=1)


def run_vm(args, variant, tests, pair, out):
    out.mkdir()
    initrd = out / "boot.cpio.gz"
    initrd.write_bytes(fixture(args, tests, pair))
    command = [str(args.qemu), "-M", "malta", "-cpu", "74Kf-lilsmoke", "-smp", "1", "-m", "120",
               "-kernel", str(args.sdk / "images/vmlinux"), "-initrd", str(initrd), "-append",
               "console=ttyS0 video=cirrusfb:856x480M-24@60 panic=-1" +
               ("" if args.enable_guest_wait else " nowait"), "-vga", "cirrus",
               "-display", "none", "-serial", "stdio", "-monitor", "none", "-no-reboot", "-nic", "none"]
    qmp_path = out / "qmp.sock"
    command.extend(["-qmp", f"unix:{qmp_path},server=on,wait=off"])
    if args.debugger:
        command.extend(["-gdb", f"unix:{out / 'gdb.sock'},server=on,wait=off"])
    # No block drives, not even an emulated SD: the SquashFS lives in guest RAM.
    shares = [("bench", out, False), ("qa-bin", args.binary.parent, True), ("themes", args.themes_root, True)]
    for index, (tag, path, readonly) in enumerate(shares):
        command.extend(["-fsdev", f"local,id=share{index},path={path},security_model=none" +
                        (",readonly=on" if readonly else ""), "-device",
                        f"virtio-9p-pci,fsdev=share{index},mount_tag={tag},addr=0x14.{index}" +
                        (",multifunction=on" if index == 0 else "")])
    write_json(out / "vm-command.json", command)
    started = time.monotonic()
    timed_out = False
    diagnostics = []
    last_change = started
    previous_text = ""
    next_diagnostic = 30
    logpath = out / "serial.log"
    with logpath.open("wb") as log:
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
        try:
            while process.poll() is None:
                text = logpath.read_text(errors="replace")
                if text != previous_text:
                    previous_text = text
                    last_change = time.monotonic()
                    next_diagnostic = 30
                if "System halted" in text or "Power down" in text or "QA_GUEST_FAILURE=" in text:
                    break
                if time.monotonic() - last_change > next_diagnostic and len(diagnostics) < 8:
                    diagnostics.append(dict(wall_seconds=round(time.monotonic() - started, 3),
                                            state=vm_diagnostic(qmp_path)))
                    write_json(out / "vm-diagnostics.json", diagnostics)
                    next_diagnostic += 60
                if time.monotonic() - started > args.vm_timeout:
                    timed_out = True
                    diagnostics.append(dict(wall_seconds=round(time.monotonic() - started, 3),
                                            state=vm_diagnostic(qmp_path)))
                    write_json(out / "vm-diagnostics.json", diagnostics)
                    break
                time.sleep(0.2)
        finally:
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=10)
    serial = logpath.read_text(errors="replace")
    results = []
    stages = ["renderer-ready", "theme-frames", "settings-open", "cancel-checked", "launch-requested",
              "launch-taken", "ui-destroyed", "execute-launch"]
    for test in tests:
        log = out / (test["id"] + ".log")
        text = log.read_text(errors="replace") if log.exists() else ""
        match = re.search(r"QA_CASE_RESULT=" + re.escape(test["id"]) + r" exit=(\d+) request=([01])", serial)
        exitcode, request = (int(match[1]), bool(int(match[2]))) if match else (None, None)
        request_file = out / (test["id"] + ".request")
        marker = request_file.read_bytes() if request_file.exists() else None
        ordered = all("QA_STAGE=" + stage in text for stage in stages)
        if ordered:
            ordered = [text.index("QA_STAGE=" + stage) for stage in stages] == sorted(
                text.index("QA_STAGE=" + stage) for stage in stages)
        cancelled = "QA_CANCEL default=1 navigated=1 back=1 no_helper_marker=1 no_root_return=1" in text
        threads_clean = "QA_THREADS_AFTER_UI=1\n" in text
        passed = (exitcode == test["expected_exit"] and request == test["request"] and ordered
                  and cancelled and threads_clean
                  and (marker == b"poweroff\n" if test["request"] else marker is None)
                  and "QA_FAILURE=" not in text)
        if test["mode"] == "exec-failure":
            passed = passed and "QA_STAGE=exec-failure-propagated" in text
        results.append(dict(**test, passed=passed, exit=exitcode, request_seen=request,
                            stages_ordered=ordered, cancellation_checked=cancelled,
                            threads_clean_after_ui=threads_clean,
                            last_stage=re.findall(r"QA_STAGE=(\S+)", text)[-1:] or None))
        print(variant, test["id"], "PASS" if passed else "FAIL", "exit=", exitcode,
              "request=", request, flush=True)
    guest_hashes = out / "guest-sha256.txt"
    hash_text = guest_hashes.read_text() if guest_hashes.exists() else ""
    pair_verified = all(sha256(path) in hash_text for path in pair.values())
    mounts = (out / "mounts.txt").read_text() if (out / "mounts.txt").exists() else ""
    themes_readonly = any(line.split()[1:3] == ["/storage/themes", "9p"] and
                          "ro" in line.split()[3].split(",") for line in mounts.splitlines())
    complete = "QA_THEMED_POWER_DONE" in serial and not timed_out
    timeout_test = out / "timeout-selftest.result"
    timeout_verified = timeout_test.exists() and timeout_test.read_text().strip() in ("124", "137", "143")
    wait_disabled = "Wait instruction disabled." in serial
    result = dict(variant=variant, passed=complete and pair_verified and themes_readonly and timeout_verified and
                  (args.enable_guest_wait or wait_disabled) and
                  all(item["passed"] for item in results), complete=complete, vm_timeout=timed_out,
                  pair_verified_in_guest=pair_verified, themes_readonly=themes_readonly,
                  guest_timeout_selftest_passed=timeout_verified, guest_wait_disabled=wait_disabled,
                  wall_seconds=round(time.monotonic() - started, 3), cases=results,
                  libraries={name: dict(path=str(path), sha256=sha256(path)) for name, path in pair.items()})
    write_json(out / "report.json", result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, default=Path("/root/lilsmoke-os/build/buildroot/output"))
    parser.add_argument("--qemu", type=Path, default=Path("/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel"))
    parser.add_argument("--payload", type=Path, default=Path("/root/lilsmoke-directfb-dispatch-20261021-qualified/payload"))
    parser.add_argument("--baseline-dir", type=Path, default=Path("/root/lilsmoke-directfb-dispatch-20261021-qualified"))
    parser.add_argument("--themes-root", type=Path, default=Path("/root/lilsmoke-vm-validation/storage/themes"))
    parser.add_argument("--font", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--variants", choices=("baseline", "qualified", "both"), default="both")
    parser.add_argument("--themes", nargs="+", help="optional bounded subset; default: default + all four originals")
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--frames", type=int, default=3)
    parser.add_argument("--child-timeout", type=int, default=45)
    parser.add_argument("--vm-timeout", type=int, default=600)
    parser.add_argument("--binary", type=Path, help="reuse a previously manifested harness build")
    parser.add_argument("--allow-source-drift", action="store_true",
                        help="explicitly test a saved source snapshot instead of qualifying current sources")
    parser.add_argument("--debugger", action="store_true",
                        help="expose a private guest GDB Unix socket for diagnosing failures")
    parser.add_argument("--enable-guest-wait", action="store_true",
                        help="diagnostic only: retain kernel WAIT idle instead of the bounded VM's nowait mode")
    args = parser.parse_args()
    if not (1 <= args.repeats <= 8 and 1 <= args.frames <= 12 and
            5 <= args.child_timeout <= 120 and 30 <= args.vm_timeout <= 1800):
        parser.error("bounds: repeats 1..8, frames 1..12, child timeout 5..120s, VM timeout 30..1800s")
    if os.name != "posix":
        parser.error("run this tool inside WSL/Linux")
    original = sorted(path.name for path in args.themes_root.iterdir()
                      if path.is_dir() and (path / "theme.xml").is_file())
    if len(original) != 4:
        parser.error("expected exactly four original theme directories")
    themes = args.themes or ["default", *original]
    if not themes or len(set(themes)) != len(themes) or any(name not in ["default", *original] for name in themes):
        parser.error("unknown or duplicated theme")
    args.font = args.font or args.sdk / "target/usr/share/fonts/dejavu/DejaVuSans.ttf"
    out = args.out or Path(tempfile.mkdtemp(prefix="lilsmoke-themed-power-", dir="/root"))
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    print("Evidence:", out, flush=True)
    args.binary = args.binary.resolve() if args.binary else build(args, out)
    build_manifest = args.binary.parent / "sources-manifest.json"
    if not build_manifest.is_file() or json.loads(build_manifest.read_text())["binary_sha256"] != sha256(args.binary):
        parser.error("--binary must have a matching sources-manifest.json beside it")
    manifest = json.loads(build_manifest.read_text())
    if not any(item["path"].endswith("/tools/m8/themed_power_test.cpp") and
               item["sha256"] == sha256(P / "tools/m8/themed_power_test.cpp") for item in manifest["inputs"]):
        parser.error("--binary harness source differs from the current regression; rebuild it")
    source_start = source_qualification(manifest)
    if not source_start["matches_current"] and not args.allow_source_drift:
        parser.error("production source fingerprint differs from the build: " + ", ".join(
            item["path"] for item in source_start["differences"]) + "; rebuild or explicitly use --allow-source-drift")
    tests = cases(args, themes)
    pairs = {
        "baseline": {"libfusion-1.7.so.7": args.baseline_dir / "baseline-libfusion.so",
                     "libdirect-1.7.so.7": args.baseline_dir / "baseline-libdirect.so"},
        "qualified": {name + "-1.7.so.7": args.payload / (name + "-1.7.so.7.0.0")
                      for name in ("libfusion", "libdirect")},
    }
    variants = ["baseline", "qualified"] if args.variants == "both" else [args.variants]
    report = dict(cpu="74Kf-lilsmoke", cpus=1, guest_ram_mib=120, video="real SDL2/DirectFB Cirrus fbdev",
                  rootfs="read-only SDK SquashFS in guest RAM", input="in-process InputButton fixture",
                  physical_poweroff_tested=False, sd_unmount_guard_tested=False, physical_fps_tested=False,
                  guest_wait_disabled=not args.enable_guest_wait,
                  kernel_sha256=sha256(args.sdk / "images/vmlinux"),
                  rootfs_sha256=sha256(args.sdk / "images/rootfs.squashfs"),
                  theme_coverage=themes, full_theme_coverage=themes == ["default", *original],
                  font=dict(path=str(args.font), sha256=sha256(args.font)),
                  power_helper_sha256=sha256(P / "package/lilsmoke-scripts/src/lilsmoke-power"),
                  harness_sha256=sha256(args.binary), variants=[])
    report["harness_sources_manifest"] = str(build_manifest)
    report["harness_manifest_sha256"] = sha256(build_manifest)
    report["runner_sha256"] = sha256(Path(__file__))
    report["qualification_scope"] = "themed UI shutdown request only; not RetroArch or physical poweroff"
    report["source_qualification_at_start"] = source_start
    sdk_pair = {name: sha256(args.sdk / "target/usr/lib" / name) for name in pairs["qualified"]}
    for variant in variants:
        report["variants"].append(run_vm(args, variant, tests, pairs[variant], out / variant))
        write_json(out / "report.json", report)
    qualified = next((item for item in report["variants"] if item["variant"] == "qualified"), None)
    baseline = next((item for item in report["variants"] if item["variant"] == "baseline"), None)
    report["old_failure_reproduced"] = (any(not item["passed"] and item["last_stage"] == ["launch-taken"]
                                            for item in baseline["cases"]) if baseline else None)
    report["passed"] = qualified["passed"] if qualified else baseline["passed"]
    report["sdk_target_pair_unchanged"] = sdk_pair == {
        name: sha256(args.sdk / "target/usr/lib" / name) for name in sdk_pair}
    report["passed"] = report["passed"] and report["sdk_target_pair_unchanged"]
    report["source_qualification_at_end"] = source_qualification(manifest)
    report["current_sources_qualified"] = (report["passed"] and source_start["matches_current"] and
                                            report["source_qualification_at_end"]["matches_current"])
    write_json(out / "report.json", report)
    print("PASS" if report["passed"] else "FAIL", "themed Power Off request path; no physical poweroff tested")
    print("Current source fingerprint:", "MATCH" if report["current_sources_qualified"] else "NOT QUALIFIED")
    print("Evidence:", out)
    return 0 if report["passed"] and (report["current_sources_qualified"] or args.allow_source_drift) else 1


if __name__ == "__main__":
    raise SystemExit(main())
