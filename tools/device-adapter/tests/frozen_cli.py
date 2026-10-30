"""Frozen EXE CLI with synthetic source/export; never applies to a card."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parent)]
import adapter as a
from test_adapter import Fixture

exe = Path(__file__).resolve().parents[1] / "release/windows-final/LilSmoke Device Adapter.exe"
def run(*args, expected=0):
    result = subprocess.run([str(exe), *map(str, args)], capture_output=True, timeout=90)
    assert result.returncode == expected, result.stderr.decode(errors="replace")
    return json.loads(result.stdout or result.stderr)

with tempfile.TemporaryDirectory() as sandbox:
    fixture = Fixture(sandbox)
    before = fixture.snapshot()
    inspected = run("inspect", fixture.source)
    assert inspected["compatibility"] == "unknown-export-only"
    exported = run("export", fixture.source, fixture.bundle)
    assert exported["complete"] and exported["profile_id"] is None
    assert exported["file_count"] == len(a.GATE_FILES)
    refused = run("plan", fixture.bundle, fixture.target, expected=2)
    assert refused["status"] == "refused"
    assert fixture.snapshot() == before
print(json.dumps(dict(passed=True, frozen_executable_sha256=a.hash_file(exe)[0],
                      cli_inspect_export=True, unknown_install_refused=True,
                      sd_written=False, synthetic_source_readonly=True)))
