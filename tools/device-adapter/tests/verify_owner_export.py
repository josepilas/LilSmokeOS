"""Independent readback of the one complete private owner export and source."""
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import adapter as a

root = Path(__file__).resolve().parent / "artifacts"
bundle = root / "owner-stock-private-export-03"
profile = a.profile_load()
source = a.validate_bundle(bundle, profile)
for name, row in profile["files"].items():
    assert a.hash_file(a.within(Path(source["source"]), name)) == (row["sha256"], row["bytes"]), name
result = dict(passed=True, private_export=str(bundle), files=len(profile["files"]),
              bytes=sum(row["bytes"] for row in profile["files"].values()),
              source_unchanged=True, proprietary_export_not_in_public_delivery=True,
              raw_image_read=False, sd_written=False)
a.write_json(root / "owner-export-verified.json", result)
print(json.dumps(result, indent=2))
