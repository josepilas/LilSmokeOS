"""Fetch pinned repositories without replacing an existing checkout."""
import json
import pathlib
import subprocess
import sys

root = pathlib.Path(sys.argv[1]).resolve()
pins = json.loads(pathlib.Path(sys.argv[2]).read_text())
for name, pin in pins.items():
    if name == "openbor":
        continue
    path = root / "sources" / name
    if not path.exists():
        path.mkdir(parents=True)
        subprocess.run(["git", "init", str(path)], check=True)
        subprocess.run(["git", "-C", str(path), "remote", "add", "origin", pin["url"]], check=True)
        subprocess.run(["git", "-C", str(path), "fetch", "--depth=1", "origin", pin["commit"]], check=True)
        subprocess.run(["git", "-C", str(path), "checkout", "--detach", "FETCH_HEAD"], check=True)
    actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
    if actual != pin["commit"]:
        raise SystemExit(f"Refusing to replace {name}: expected {pin['commit']}, found {actual}")
    print(f"PIN {name} {actual}", flush=True)
