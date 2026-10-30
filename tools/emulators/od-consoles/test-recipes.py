#!/usr/bin/env python3
"""Check source fix contexts and idempotence without compiling any emulator."""
import io
import json
from pathlib import Path
import shutil
import tempfile
from build import CACHE, DEFAULT_REPO, source_fixes


def main():
    package = DEFAULT_REPO / "package/lilsmoke-od-consoles"
    sources = json.loads((package / "sources.json").read_text())["engines"]
    fixes = json.loads((package / "source-fixes.json").read_text())
    root = CACHE / "od-console-qa"
    root.mkdir(exist_ok=True)
    failures = []
    for engine, entry in sources.items():
        try:
            with tempfile.TemporaryDirectory(prefix="recipes-", dir=root) as temporary:
                work = Path(temporary)
                source = CACHE / "od-console-src" / entry.get("source_directory", engine)
                for name in {fix["file"] for fix in fixes[engine]}:
                    target = work / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source / name, target)
                source_fixes(engine, work, package, io.StringIO())
                first = {name: (work / name).read_bytes() for name in {f["file"] for f in fixes[engine]}}
                source_fixes(engine, work, package, io.StringIO())
                assert all((work / name).read_bytes() == content for name, content in first.items())
                print(engine + ": fresh contexts and second application PASS", flush=True)
        except (RuntimeError, AssertionError, OSError) as error:
            failures.append(engine)
            print(engine + ": FAIL " + str(error), flush=True)
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
