#!/usr/bin/env python3
"""Collect small, source-qualified UI evidence without duplicating SDKs or themes."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--work", required=True)
    p.add_argument("--build", required=True)
    p.add_argument("--project", required=True)
    p.add_argument("--output", required=True)
    p.add_argument("--sdk", default="/root/lilsmoke-os/build/buildroot/output")
    a = p.parse_args()
    work, build, project, out = map(Path, (a.work, a.build, a.project, a.output))
    out.mkdir(parents=True, exist_ok=True)
    reports = {}
    for name in ("before", "after", "final", "timing-before", "timing-after", "damage", "motion"):
        report = work / name / "report.json"
        reports[name] = json.loads(report.read_text())
        dest = out / name; dest.mkdir(exist_ok=True)
        shutil.copy2(report, dest / "report.json")
        for pattern in ("*.tsv", "*.csv", "*-frames.json", "*-command.json", "sources.json", "harness.sha256", "build-command.json"):
            for path in (work / name).glob(pattern):
                shutil.copy2(path, dest / path.name)
    for name in ("before", "final"):
        dest = out / name / "source"; dest.mkdir(exist_ok=True)
        for path in (work / name / "source").glob("*"):
            shutil.copy2(path, dest / path.name)
    before = json.loads((work / "before/sources.json").read_text())
    final = json.loads((work / "final/sources.json").read_text())
    changed = [n for n in final if before.get(n) != final[n]]
    current = project / "package/lilsmoke-ui/src/src"
    assert all(sha(current / n) == digest for n, digest in final.items()), "Production source changed"
    assert (build / "source-inputs.before").read_bytes() == (build / "source-inputs.after").read_bytes(), "Mixed UI objects"
    assert reports["motion"]["pass"] and len(reports["motion"]["results"]) == 15
    assert reports["final"]["pixel_identical_to"] and reports["timing-after"]["pixel_identical_to"]
    payload = out / "payload"; payload.mkdir(exist_ok=True)
    shutil.copy2(build / "payload/lilsmoke-ui", payload / "lilsmoke-ui")
    for name in ("source-inputs.before", "source-inputs.after"):
        shutil.copy2(build / name, out / name)
    for name in ("build", "motion", "navigation", "regression-suite", "damage"):
        log = work.parent / ("ui-perf-" + name + ".log")
        if log.exists():
            shutil.copy2(log, out / log.name)
    for path in (work / "motion").glob("*-keyframes.png"):
        shutil.copy2(path, out / path.name)
    for name in ("source-verification.json", "provenance.json"):
        shutil.copy2(work / "motion" / name, out / "motion" / name)
    for path in (work / "before").glob("perf-*.txt"):
        shutil.copy2(path, out / "before" / path.name)
    hardware_log = project / "build/readonly-ui-followup-20261021/bringup-before.log"
    if hardware_log.exists():
        shutil.copy2(hardware_log, out / "bringup-before.log")
    sdk = Path(a.sdk)
    sdl = sdk / "build/sdl2-2.28.5/src"
    sdk_library = sdk / "target/usr/lib/libSDL2-2.0.so.0.2800.5"
    staged_library = project / "build/theme-state-update-20261021/runtime/files/000098.bin"
    excerpts = []
    for name, ranges in (
        ("render/software/SDL_render_sw.c", ((54, 66), (101, 115), (964, 972), (1043, 1072))),
        ("video/SDL_video.c", ((314, 328), (339, 367), (2639, 2678))),
        ("video/directfb/SDL_DirectFB_video.c", ((84, 132),)),
        ("stdlib/SDL_malloc.c", ((5195, 5217), (5287, 5299))),
    ):
        path = sdl / name
        lines = path.read_text().splitlines()
        excerpts.append({"path": str(path), "sha256": sha(path), "excerpts": [
            {"start": start, "end": end, "text": "\n".join(lines[start-1:end])} for start, end in ranges]})
    memory_evidence = {
        "conclusion": "Inspected SDK DirectFB software window pixels are SDL_malloc-backed, not a locked DirectFB framebuffer",
        "sdk_sdl_sha256": sha(sdk_library),
        "candidate39_staged_sdl_sha256": sha(staged_library) if staged_library.exists() else None,
        "candidate39_staged_sdl_matches_sdk": staged_library.exists() and sha(staged_library) == sha(sdk_library),
        "source_excerpts": excerpts,
        "musl_memcmp": (sdk / "build/musl-1.2.4/src/string/memcmp.c").read_text(),
        "limitation": "Source/path identity evidence, not a physical kernel PTE/cacheability measurement; no second-canvas speedup established",
        "decision": "Do not add a duplicate framebuffer to the qualified UI on this evidence",
    }
    (out / "sdl-canvas-evidence.json").write_text(json.dumps(memory_evidence, indent=2) + "\n")
    timings = {}
    for job, data in reports["timing-after"]["results"].items():
        old = reports["timing-before"]["results"][job]
        timings[job] = {phase: {"before_ms": old[phase]["render_ms"], "after_ms": data[phase]["render_ms"]}
            for phase in ("idle", "system", "grid")}
    manifest = {
        "scope": "UI only; no SDK target, board, PCSX, native-display, SD or host-key writes",
        "source_matches_current": True, "build_source_consistent": True,
        "changed_production_files": changed,
        "unstripped_sha256": sha(build / "build/lilsmoke-ui"),
        "stripped_sha256": sha(payload / "lilsmoke-ui"),
        "stripped_bytes": (payload / "lilsmoke-ui").stat().st_size,
        "pixel_identical_frames": sum(v["frames"] for v in reports["final"]["results"].values()),
        "hardware_baseline_log_sha256": sha(hardware_log) if hardware_log.exists() else None,
        "host_timing_without_call_wrappers": timings,
        "mips_qemu_damage": reports["damage"],
        "sdl_canvas_assessment": memory_evidence["conclusion"],
        "caveats": ["No hardware FPS or smoothness qualification", "Host SDL 2.30; SDK SDL 2.28.5",
            "Instrumented copy counters perturb timing; timing-only pass removes all but authored clock wrapper",
            "Software scaling and theme parsing/copies remain; no resampling shortcut or new scaled-image cache",
            "Cache residency limits exclude existing framebuffer/shadow, transient decoder and TTF allocations"],
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
