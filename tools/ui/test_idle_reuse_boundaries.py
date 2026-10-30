#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Extra boundary fixture using the exact qualified production-view objects."""
import argparse
import hashlib
import json
import os
import resource
from pathlib import Path
import shutil
import subprocess

P = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--previous", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    old = json.loads((a.previous / "report.json").read_text())
    if old["status"] != "passed" or old["source_drift"]:
        raise ValueError("Previous qualification did not pass")
    for relative, digest in old["inputs_before"].items():
        if sha(P / relative) != digest:
            raise ValueError("Source changed since view qualification: " + relative)
    a.output.mkdir(parents=True, exist_ok=False)
    src = a.output / "source-snapshot"
    src.mkdir()
    for name in ("idle-reuse-test.cpp", "idle-reuse-boundary-test.cpp", "test_idle_reuse_boundaries.py"):
        shutil.copyfile(P / "tools/ui" / name, src / name)
    report = dict(status="running", commands=[], inputs={}, object_sha256={}, results=[])
    report["inputs"] = {str(path): sha(path) for path in src.iterdir()}

    def run(name, command):
        report["commands"].append(dict(name=name, argv=command))
        def limit_compiler():
            resource.setrlimit(resource.RLIMIT_AS, (768 * 1024**2, 768 * 1024**2))
        with (a.output / (name + ".log")).open("w") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=300,
                preexec_fn=limit_compiler if "compile" in name or "link" in name else None,
                env=dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1"))
        report["results"].append(dict(name=name, returncode=result.returncode))
        print(name, result.returncode, flush=True)
        result.check_returncode()

    try:
        commands = {c["name"]: c["argv"] for c in old["commands"]}
        for arch in ("host", "mips"):
            compile_command = commands[arch + "-compile-idle-reuse-test"].copy()
            compile_command[compile_command.index("-c") + 1] = str(src / "idle-reuse-boundary-test.cpp")
            obj = str(a.output / (arch + "-boundaries.o"))
            compile_command[compile_command.index("-o") + 1] = obj
            run(arch + "-compile", compile_command)
            link = commands[arch + "-link"].copy()
            link[link.index(str(a.previous / (arch + "-idle-reuse-test.o")))] = obj
            for path in link:
                if path.endswith(".o"):
                    report["object_sha256"][path] = sha(Path(path))
            binary = str(a.output / (arch + "-boundaries"))
            link[link.index("-o") + 1] = binary
            link.insert(1, "-rdynamic")
            run(arch + "-link", link)
            test = commands[arch + "-test"].copy()
            index = test.index(str(a.previous / (arch + "-idle-reuse")))
            test = test[:index] + [binary, "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                  str(a.output / (arch + "-fixtures"))]
            run(arch + "-test", test)
        report["status"] = "passed"
    finally:
        report["source_drift"] = any(sha(P / relative) != digest for relative, digest in old["inputs_before"].items())
        if report["source_drift"]:
            report["status"] = "failed-source-drift"
        (a.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    if report["status"] != "passed":
        raise RuntimeError("Boundary qualification failed")
    print("QUALIFIED_IDLE_BOUNDARIES_OK")


if __name__ == "__main__":
    main()
