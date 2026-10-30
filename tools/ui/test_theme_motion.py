#!/usr/bin/env python3
"""Bounded, deterministic production-view QA; never reads or writes an SD device."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile
import gzip
import stat
import time
import shlex

THEMES = {
    "art-book-next": "es-theme-art-book-next-arkos-main",
    "nes-box": "es-theme-nes-box-master",
    "switch": "es-theme-switch-master",
    "woody-cody": "es-theme-WOODY-CODY-RG351V-main",
}
ARCHIVES = {
    "art-book-next": "es-theme-art-book-next-arkos-main (1).zip",
    "nes-box": "es-theme-nes-box-master.zip",
    "switch": "es-theme-switch-master.zip",
    "woody-cody": "es-theme-WOODY-CODY-RG351V-main.zip",
}
SIZES = ((320, 240), (856, 480), (1280, 720))


def snapshot(args):
    source = Path(args.source)
    dest = Path(args.output)
    dest.mkdir(parents=True, exist_ok=True)
    paths = sorted(source.glob("*.h")) + sorted(source.glob("*.cpp"))
    for attempt in range(3):
        contents = {p.name: p.read_bytes() for p in paths}
        if all(p.read_bytes() == contents[p.name] for p in paths):
            for name, data in contents.items():
                (dest / name).write_bytes(data)
            write_json(args.manifest, {name: hashlib.sha256(data).hexdigest()
                                      for name, data in contents.items()})
            print(f"SOURCE_SNAPSHOT_OK files={len(contents)} bytes={sum(map(len, contents.values()))}", flush=True)
            return 0
        time.sleep(1)
    raise RuntimeError("Production files still changing; cannot capture consistent QA snapshot")


def build(args):
    source = Path(args.source)
    work = Path(args.work)
    objects = work / "objects"
    objects.mkdir(parents=True, exist_ok=True)
    cflags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "sdl2", "SDL2_image", "SDL2_ttf"], text=True))
    libs = shlex.split(subprocess.check_output(["pkg-config", "--libs", "sdl2", "SDL2_image", "SDL2_ttf"], text=True))
    flags = ["-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
             "-fno-omit-frame-pointer", "-fno-pie", "-I" + str(source)] + cflags
    headers = b"".join(p.name.encode() + p.read_bytes() for p in sorted(source.glob("*.h")))
    helper = Path(args.harness).with_name("theme-background-qa.h")
    if helper.is_file():
        headers += helper.read_bytes()
    paths = [Path(args.harness)] + [source / (name + ".cpp") for name in
            ("SystemView", "GridView", "Renderer", "Font", "Theme", "ThemeRender", "Log")]
    compiled = []
    with Path(args.output).open("w") as log:
        for path in paths:
            digest = hashlib.sha256(headers + path.read_bytes() + repr(flags).encode()).hexdigest()
            target = objects / f"{path.stem}-{digest}.o"
            if not target.exists():
                subprocess.run(["g++", *flags, "-c", str(path), "-o", str(target)],
                               stdout=log, stderr=subprocess.STDOUT, check=True)
            compiled.append(str(target))
        subprocess.run(["g++", "-fsanitize=address,undefined", "-no-pie", "-Wl,--wrap=SDL_GetTicks",
                        *compiled, *libs, "-ldl", "-o", str(work / "theme-motion-qa")],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    print(f"HOST_ASAN_BUILD_OK objects={len(compiled)}", flush=True)
    return 0


def verify(args):
    expected = json.loads(Path(args.manifest).read_text())
    changed = [name for name, digest in expected.items()
               if hashlib.sha256((Path(args.source) / name).read_bytes()).hexdigest() != digest]
    write_json(args.output, {"matches_current_production": not changed, "changed_files": changed})
    if changed:
        raise RuntimeError(f"QA source snapshot no longer matches production: {changed}")
    print("QA_SNAPSHOT_MATCHES_PRODUCTION", flush=True)
    return 0


def write_json(path, data):
    Path(path).write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def provenance(args):
    # ZIP metadata only: no nested archives, no extraction, no giant duplicates.
    result = {"extracted_bytes": 0, "source": "existing extracted SSD directories",
              "physical_ssd_free_bytes": shutil.disk_usage("/mnt/c").free,
              "minimum_free_for_new_extraction_bytes": 15 * 1024**3, "themes": {}}
    archives = Path("/path/to/local-input/themes-for-test")
    for slug, name in THEMES.items():
        root = Path(args.themes) / name
        if not root.is_dir():
            raise RuntimeError(f"Missing existing theme {root}; extraction disabled below 15 GiB")
        archive = archives / ARCHIVES[slug]
        entry = {"path": str(root), "archive": str(archive), "samples": []}
        if archive.exists():
            with zipfile.ZipFile(archive) as z:
                info = z.infolist()
                entry["archive_files"] = len(info)
                entry["archive_uncompressed_bytes"] = sum(i.file_size for i in info)
                candidates = [i for i in info if not i.is_dir() and
                              i.file_size <= 1024 * 1024 and not i.filename.endswith(".zip")]
                # Prefer real system XML plus common resources, at most 12 MiB read.
                candidates.sort(key=lambda i: (not i.filename.endswith("theme.xml"), i.filename))
                for item in candidates:
                    parts = Path(item.filename).parts
                    if len(parts) < 2 or ".." in parts:
                        continue
                    local = root.joinpath(*parts[1:])
                    if not local.is_file() or local.stat().st_size != item.file_size:
                        continue
                    digest = hashlib.sha256(local.read_bytes()).hexdigest()
                    matches = digest == hashlib.sha256(z.read(item)).hexdigest()
                    entry["samples"].append({"relative_path": str(Path(*parts[1:])),
                                             "sha256": digest, "archive_match": matches})
                    if not matches:
                        raise RuntimeError(f"Existing theme differs from supplied archive: {local}")
                    if len(entry["samples"]) == 12:
                        break
            if not entry["samples"]:
                raise RuntimeError(f"Cannot establish archive provenance: {root}")
        result["themes"][slug] = entry
    write_json(args.output, result)
    print("THEME_PROVENANCE_OK existing_dirs=4 extraction_bytes=0", flush=True)


def diff(a, b):
    from PIL import ImageChops
    d = ImageChops.difference(a.convert("RGB"), b.convert("RGB"))
    histogram = d.histogram()
    mean = sum((i % 256) * count for i, count in enumerate(histogram)) / (a.width * a.height * 3)
    return {"bbox": d.getbbox(), "mean_absolute_channel_difference": round(mean, 6)}


def image(path):
    from PIL import Image
    with Image.open(path) as source:
        return source.convert("RGB")


def sequence_paths(directory, prefix="system"):
    with (directory / "frames.tsv").open() as f:
        return [(directory / (row["frame"] + ".png"), int(row["ticks_ms"]))
                for row in csv.DictReader(f, delimiter="\t")
                if row["frame"].rsplit("-", 2)[0] == prefix and "-t" in row["frame"]]


def color_components(im, channel):
    # Count solid-color fixture logos, ignoring blended edge pixels.
    data = list(im.getdata())
    selected = {i for i, rgb in enumerate(data)
                if rgb[channel] >= 120 and all(rgb[j] <= 5 for j in range(3) if j != channel)}
    count = 0
    while selected:
        seed = selected.pop()
        pending = [seed]
        area = 0
        while pending:
            p = pending.pop()
            area += 1
            x, y = p % im.width, p // im.width
            for q in ((p - 1 if x else -1), (p + 1 if x + 1 < im.width else -1),
                      (p - im.width if y else -1),
                      (p + im.width if y + 1 < im.height else -1)):
                if q in selected:
                    selected.remove(q)
                    pending.append(q)
        if area >= 4:
            count += 1
    return count


def analyze(directory, fixtures=False):
    result = {"directory": str(directory), "sequences": {}}
    prefixes = ["fixture-" + t for t in
                ("horizontal", "vertical", "horizontal_wheel", "vertical_wheel", "horizontal-single")] if fixtures else ["system"]
    for prefix in prefixes:
        paths = sequence_paths(directory, prefix)
        selected = {}
        for path, ticks in paths:
            elapsed = int(path.stem.split("-t")[-1])
            if elapsed in (0, 100, 250, 500):
                selected[elapsed] = image(path)
        if set(selected) != {0, 100, 250, 500}:
            raise RuntimeError(f"Missing deterministic keyframes in {directory}/{prefix}")
        hashes = {t: hashlib.sha256(im.tobytes()).hexdigest() for t, im in selected.items()}
        transitions = {f"{a}-{b}": diff(selected[a], selected[b])
                       for a, b in ((0, 100), (100, 250), (250, 500))}
        if not prefix.endswith("-single") and len(set(hashes.values())) < 3:
            raise RuntimeError(f"No multi-timeframe motion proof: {directory}/{prefix}")
        detail = {"sha256_pixels": hashes, "differences": transitions}
        if fixtures:
            counts = {t: [color_components(im, c) for c in (0, 1)] for t, im in selected.items()}
            detail["logo_components_red_green_by_time"] = counts
            for t, count in counts.items():
                if max(count) > 1 or sum(count) < 1:
                    write_json(directory / "pixel-analysis-failure.json", detail)
                    raise RuntimeError(f"Duplicate/blank 2-entry carousel: {directory}/{prefix} t={t}: {count}")
        result["sequences"][prefix] = detail
    if not fixtures:
        grid_before, grid_zero, grid_end = [image(directory / n) for n in
                                           ("grid-before.png", "grid-t0.png", "grid-t500.png")]
        result["grid_selection_change"] = diff(grid_before, grid_end)
        result["grid_media_fade"] = diff(grid_zero, grid_end)
        if result["grid_media_fade"]["bbox"] is None:
            raise RuntimeError(f"Grid metadata/media has no timed change: {directory}")
        # Require actual synthetic artwork pixels, not only list text changes.
        counts = []
        for im in (grid_before, grid_end):
            count = sum(1 for rgb in im.getdata() if rgb in ((36, 136, 204), (217, 70, 88)))
            counts.append(count)
        result["synthetic_media_pixels_before_after"] = counts
        if min(counts) < 4:
            raise RuntimeError(f"Actual media pixels absent: {directory}: {counts}")
    write_json(directory / "pixel-analysis.json", result)
    return result


def previews(output):
    from PIL import Image, ImageDraw, ImageFont
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 16)
    thumb_w, thumb_h = 428, 240
    selected = []
    for slug in THEMES:
        directory = output / slug / "856x480"
        paths = sequence_paths(directory)
        selected.append((slug, [(p, int(p.stem.split("-t")[-1])) for p, _ in paths]))
        sheet = Image.new("RGB", (thumb_w * 4, thumb_h + 64), "#111111")
        draw = ImageDraw.Draw(sheet)
        draw.text((10, 6), f"{slug}: HOST SOFTWARE QA, NOT AN M8 RECORDING", font=font, fill="white")
        for col, t in enumerate((0, 100, 250, 500)):
            path = next(p for p, elapsed in selected[-1][1] if elapsed == t)
            sheet.paste(image(path).resize((thumb_w, thumb_h)), (col * thumb_w, 64))
            draw.text((col * thumb_w + 10, 36), f"t={t} ms", font=font, fill="white")
        sheet.save(output / f"{slug}-keyframes.png")
    preview_dir = output / "preview-frames"
    preview_dir.mkdir(exist_ok=True)
    # A 3-second comparison loop at authored time. It is not a performance measurement.
    for frame in range(75):
        elapsed = (frame % 25) * 40
        canvas = Image.new("RGB", (thumb_w * 2, (thumb_h + 28) * 2 + 42), "#101010")
        draw = ImageDraw.Draw(canvas)
        draw.text((10, 8), "HOST SOFTWARE QA | NOT M8 FOOTAGE | deterministic motion", font=font, fill="white")
        for i, (slug, paths) in enumerate(selected):
            path, time = min(paths, key=lambda pair: abs(pair[1] - elapsed))
            x, y = (i % 2) * thumb_w, 42 + (i // 2) * (thumb_h + 28)
            draw.text((x + 8, y + 4), f"{slug}   t={time} ms", font=font, fill="white")
            canvas.paste(image(path).resize((thumb_w, thumb_h)), (x, y + 28))
        canvas.save(preview_dir / f"{frame:04d}.png")
    command = ["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-framerate", "25",
               "-i", str(preview_dir / "%04d.png"), "-an", "-c:v", "libvpx-vp9", "-threads", "1",
               "-b:v", "0", "-crf", "34", "-pix_fmt", "yuv420p", str(output / "host-software-motion-preview.webm")]
    subprocess.run(command, check=True)


def audit(args):
    output = Path(args.output)
    report = {"kind": "host software/offscreen QA, not M8 recording or FPS",
              "clock": "linker --wrap=SDL_GetTicks; explicit timestamps",
              "results": [], "failures": [], "audited_existing_frames": True}
    for w, h in SIZES:
        for slug in ("fixtures", *THEMES):
            dest = output / slug / f"{w}x{h}"
            try:
                log = (dest / "sanitizers.log").read_text()
                if "THEME_MOTION_RENDER_OK" not in log or any(marker in log for marker in
                    ("THEME_MOTION_QA_FAIL", "ERROR: AddressSanitizer", "runtime error:", "LeakSanitizer")):
                    raise RuntimeError("C++ render/sanitizer job did not pass")
                report["results"].append(analyze(dest, slug == "fixtures"))
            except Exception as error:
                report["failures"].append({"job": str(dest), "error": str(error)})
    previews(output)
    report["pass"] = not report["failures"]
    write_json(output / "report.json", report)
    print(json.dumps({"pass": report["pass"], "jobs": len(report["results"]), "failures": report["failures"]}), flush=True)
    return 0 if report["pass"] else 1


def run(args):
    output = Path(args.output)
    report = {"kind": "host software/offscreen QA, not M8 recording or FPS",
              "clock": "linker --wrap=SDL_GetTicks; explicit timestamps", "results": [], "failures": []}
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    sizes = [tuple(map(int, size.split("x"))) for size in args.sizes.split(",")]
    for w, h in sizes:
        jobs = [] if args.only == "real" else [("fixtures", Path(args.themes), True)]
        if args.only != "fixtures":
            jobs += [(slug, Path(args.themes) / name, False) for slug, name in THEMES.items()]
        for slug, root, fixtures in jobs:
            dest = output / slug / f"{w}x{h}"
            dest.mkdir(parents=True, exist_ok=True)
            print(f"MOTION_QA {slug} {w}x{h}", flush=True)
            command = [args.binary, str(root), str(dest), args.font, str(w), str(h),
                       "fixtures" if fixtures else "real"]
            with (dest / "sanitizers.log").open("w") as log:
                completed = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                                           timeout=240, check=False)
            if completed.returncode:
                report["failures"].append({"job": str(dest), "exit": completed.returncode,
                                           "log": str(dest / "sanitizers.log")})
            if (dest / "frames.tsv").exists():
                try:
                    report["results"].append(analyze(dest, fixtures))
                except Exception as error:
                    report["failures"].append({"job": str(dest), "analysis": str(error)})
            write_json(output / "report.json", report)
    if args.only != "fixtures" and all((output / slug / "856x480" / "system-000-t0000.png").exists() for slug in THEMES):
        previews(output)
    report["pass"] = not report["failures"]
    write_json(output / "report.json", report)
    print(json.dumps({"pass": report["pass"], "jobs": len(report["results"]), "failures": report["failures"]}), flush=True)
    return 0 if report["pass"] else 1


def kernel(args):
    # Tiny in-memory newc archive, no rootfs image clone and no SDK/target writes.
    output = Path(args.output)
    work = Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    archive = bytearray()

    def entry(name, mode, data=b"", major=0, minor=0):
        name_bytes = name.encode() + b"\0"
        fields = (1, mode, 0, 0, 1, 0, len(data), 0, 0, major, minor, len(name_bytes), 0)
        archive.extend(b"070701" + b"".join(f"{v:08x}".encode() for v in fields))
        archive.extend(name_bytes)
        archive.extend(b"\0" * (-len(archive) % 4))
        archive.extend(data)
        archive.extend(b"\0" * (-len(archive) % 4))

    for directory in ("bin", "dev", "proc", "sys", "lib"):
        entry(directory, stat.S_IFDIR | 0o755)
    entry("dev/console", stat.S_IFCHR | 0o600, major=5, minor=1)
    entry("bin/busybox", stat.S_IFREG | 0o755, Path(args.busybox).read_bytes())
    libc = Path(args.busybox).parent.parent / "lib" / "libc.so"
    entry("lib/libc.so", stat.S_IFREG | 0o755, libc.read_bytes())
    entry("lib/ld-musl-mipsel.so.1", stat.S_IFLNK | 0o777, b"libc.so")
    entry("qa", stat.S_IFREG | 0o755, Path(args.binary).read_bytes())
    entry("init", stat.S_IFREG | 0o755, Path(__file__).with_name("theme-motion-init.sh").read_bytes())
    entry("TRAILER!!!", 0)
    initramfs = work / "motion-numeric.cpio.gz"
    initramfs.write_bytes(gzip.compress(bytes(archive), mtime=0))
    command = ["qemu-system-mipsel", "-M", "malta", "-cpu", "74Kf", "-accel", "tcg,thread=single",
               "-smp", "1", "-m", "120M", "-kernel", args.kernel, "-initrd", str(initramfs),
               "-append", "console=ttyS0,115200 rdinit=/init panic=-1", "-nographic",
               "-monitor", "none", "-nic", "none", "-no-reboot"]
    serial_path = work / "mips-kernel-serial.log"
    with serial_path.open("w") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 120
        try:
            while process.poll() is None and time.monotonic() < deadline:
                serial = serial_path.read_text()
                if "THEME_MOTION_KERNEL_EXIT=" in serial:
                    break
                time.sleep(0.2)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    serial = serial_path.read_text()
    shutil.copyfile(serial_path, output / "mips-kernel-serial.log")
    ok = "THEME_MOTION_KERNEL_EXIT=0" in serial and "THEME_MOTION_NUMERIC_OK" in serial
    write_json(output / "mips-kernel.json", {"pass": ok, "command": command,
               "initramfs_bytes": initramfs.stat().st_size,
               "scope": "static numeric regression only; actual Linux kernel, one CPU, 120 MiB; no FPS claim"})
    if not ok:
        raise RuntimeError("MIPS kernel numeric QA did not finish successfully")
    print("THEME_MOTION_KERNEL_OK cpu=1 memory=120MiB numeric_only=1", flush=True)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="mode", required=True)
    p = sub.add_parser("provenance")
    p.add_argument("--themes", required=True)
    p.add_argument("--output", required=True)
    p = sub.add_parser("run")
    p.add_argument("--binary", required=True)
    p.add_argument("--themes", required=True)
    p.add_argument("--font", required=True)
    p.add_argument("--output", required=True)
    p.add_argument("--only", choices=("all", "fixtures", "real"), default=os.environ.get("QA_MOTION_ONLY", "all"))
    p.add_argument("--sizes", default=os.environ.get("QA_MOTION_SIZES", ",".join(f"{w}x{h}" for w, h in SIZES)))
    p = sub.add_parser("kernel")
    p.add_argument("--binary", required=True)
    p.add_argument("--busybox", required=True)
    p.add_argument("--kernel", required=True)
    p.add_argument("--work", required=True)
    p.add_argument("--output", required=True)
    p = sub.add_parser("snapshot")
    p.add_argument("--source", required=True)
    p.add_argument("--output", required=True)
    p.add_argument("--manifest", required=True)
    p = sub.add_parser("build")
    p.add_argument("--source", required=True)
    p.add_argument("--harness", required=True)
    p.add_argument("--output", required=True)
    p.add_argument("--work", required=True)
    p = sub.add_parser("verify")
    p.add_argument("--source", required=True)
    p.add_argument("--manifest", required=True)
    p.add_argument("--output", required=True)
    p = sub.add_parser("audit")
    p.add_argument("--output", required=True)
    args = parser.parse_args()
    return {"provenance": provenance, "run": run, "kernel": kernel, "snapshot": snapshot,
            "build": build, "verify": verify, "audit": audit}[args.mode](args)


if __name__ == "__main__":
    sys.exit(main())
