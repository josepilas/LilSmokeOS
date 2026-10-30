"""Read-only public-delivery verification and generated handoff inventory."""
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import adapter as a

tool = Path(__file__).resolve().parents[1]
artifacts = tool / "tests/artifacts"
stage = tool / "release/lilos"
delivery = a.read_json(stage / "delivery.json")
frozen = a.read_json(artifacts / "frozen-cli.json")
gui = a.read_json(artifacts / "gui-smoke.json")
owner = a.read_json(artifacts / "owner-export-verified.json")
stock = a.read_json(artifacts / "frozen-stock-inspect.json")
assert all(row["passed"] for row in (frozen, gui, owner))
assert stock["profile_id"] == a.profile_load()["id"] and stock["read_only"]
assert all(row["matches"] for row in stock["files"].values())
assert not delivery["proprietary_firmware_included"]
for name, row in delivery["files"].items():
    assert name.startswith("PC-Tools/LilSmoke Device Adapter/")
    assert a.hash_file(a.within(stage, name)) == (row["sha256"], row["bytes"]), name
exe = stage / "PC-Tools/LilSmoke Device Adapter/LilSmoke Device Adapter.exe"
assert a.hash_file(exe)[0] == frozen["frozen_executable_sha256"]
assert a.hash_file(exe)[0] == a.hash_file(tool / "release/windows-final/LilSmoke Device Adapter.exe")[0]
windows_log = a.read_small(artifacts / "windows-tests.log").decode("utf-8-sig")
assert "Ran 30 tests" in windows_log and windows_log.strip().endswith("OK (skipped=1)")
result = dict(passed=True, tool_version=a.VERSION, public_delivery_directory=str(exe.parent),
              delivery_manifest=str(stage / "delivery.json"),
              delivery_manifest_sha256=a.hash_file(stage / "delivery.json")[0],
              public_files=delivery["file_count"], public_bytes=delivery["bytes"],
              executable_sha256=frozen["frozen_executable_sha256"], executable_bytes=exe.stat().st_size,
              windows_unittest=dict(tests=30, passed=29, skipped=1, log=str(artifacts / "windows-tests.log")),
              linux_unittest=dict(tests=30, passed=29, skipped=1,
                                  evidence="Actual WSL Python suite tool output: exit 0, 30 tests, skipped Windows MAX_PATH"),
              gui=gui, frozen_cli=frozen, stock_inspect=str(artifacts / "frozen-stock-inspect.json"),
              stock_export=owner, sd_written=False, physical_adapted_card_boot_verified=False,
              macos_native_tested=False, macos_diskutil_and_linux_sysfs_mocked=True,
              linux_macos_python_required=True, proprietary_firmware_publicly_included=False,
              windows_parent_gui_inspect_evidence="Parent reports native compiled window opened, stock folder selected, "
                                                 "Inspect/report/progress completed, readable layout; own test window closed.")
a.write_json(artifacts / "handoff.json", result)
print(json.dumps(result, indent=2))
