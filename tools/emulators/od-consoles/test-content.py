#!/usr/bin/env python3
"""Resolver contract, C++11/no-exceptions compatibility, and deadline tests."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
from build import CACHE, DEFAULT_BR, DEFAULT_REPO

FORMATS = ("zip", "7z", "rar", "tar", "gz", "bz2", "xz", "tgz", "tbz2", "txz")
HELPER = '''#!/usr/bin/python3
import json, os, sys, time
with open(os.environ["OD_TEST_ARGS"], "w") as f: json.dump(sys.argv[1:], f)
mode = os.environ["OD_TEST_MODE"]
path = os.environ["OD_TEST_CONTENT"]
if mode == "hang": time.sleep(10)
elif mode == "close-hang": os.close(1); time.sleep(10)
elif mode == "failed": print(path); sys.exit(1)
elif mode == "relative": print("relative.gb")
elif mode == "multiline": print(path + "\\n" + path)
elif mode == "nul": sys.stdout.buffer.write(path.encode() + b"\\0\\n")
elif mode == "overflow": print("/" + "a" * 10000)
elif mode == "missing": print(path + ".missing")
elif mode == "directory": print(os.path.dirname(path))
else: print(path, end="\\n" if mode == "valid" else "")
'''


def main():
    root = CACHE / "od-console-qa"
    root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="content-", dir=root) as temporary:
        work = Path(temporary)
        source = Path(__file__).with_suffix(".cpp")
        package = DEFAULT_REPO / "package/lilsmoke-od-consoles"
        compilers = {"host": "g++", "mips": str(DEFAULT_BR / "host/bin/mipsel-linux-g++")}
        content = work / "resolved name.gb"
        content.write_bytes(b"synthetic resolver fixture, not a game")
        helper = work / "helper"
        helper.write_text(HELPER)
        helper.chmod(0o755)
        env = os.environ.copy()
        env.update(LILSMOKE_ROM_RESOLVER=str(helper), OD_TEST_CONTENT=str(content),
                   OD_TEST_ARGS=str(work / "args.json"), LILSMOKE_OD_SYSTEM="gbc")
        results = {}
        for architecture, compiler in compilers.items():
            binary = work / architecture
            subprocess.run([compiler, "-std=c++11", "-fno-exceptions", "-fno-rtti", "-O1",
                            "-DOD_RESOLVER_TIMEOUT_MS=250", "-I" + str(package),
                            str(source), "-o", str(binary)], check=True)
            command = [str(binary)]
            if architecture == "mips":
                command = ["qemu-mipsel", "-cpu", "74Kf", "-L",
                           str(DEFAULT_BR / "host/mipsel-buildroot-linux-musl/sysroot"), str(binary)]
            count = 0
            for extension in FORMATS:
                archive = str(work / ("name ' ; $." + extension.upper()))
                env["OD_TEST_MODE"] = "valid"
                result = subprocess.run(command + [archive], env=env, capture_output=True, text=True, timeout=3)
                assert result.returncode == 0 and result.stdout.strip() == str(content), result.stderr
                assert json.loads((work / "args.json").read_text()) == ["gbc", archive]
                count += 1
            for mode in ("no-newline", "failed", "relative", "multiline", "nul", "overflow", "missing",
                         "directory", "hang", "close-hang"):
                env["OD_TEST_MODE"] = mode
                started = time.monotonic()
                result = subprocess.run(command + [str(work / "archive.zip")], env=env,
                                        capture_output=True, text=True, timeout=3)
                elapsed = time.monotonic() - started
                assert (result.returncode == 0) == (mode == "no-newline"), (mode, result)
                assert elapsed < 2, (mode, elapsed)
                count += 1
            env["LILSMOKE_ROM_RESOLVER"] = str(work / "absent")
            assert subprocess.run(command + ["any.zip"], env=env, capture_output=True).returncode == 1
            result = subprocess.run(command + ["direct.gb"], env=env, capture_output=True, text=True)
            assert result.returncode == 0 and result.stdout.strip() == "direct.gb"
            env["LILSMOKE_ROM_RESOLVER"] = str(helper)
            results[architecture] = dict(passed=count + 2, cpp11_no_exceptions=True)
            print(json.dumps({architecture: results[architecture]}), flush=True)
        (root / "content-tests.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()
