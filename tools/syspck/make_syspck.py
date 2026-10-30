#!/usr/bin/env python3
"""Host-only authoring. Target validation/installation uses native C++."""
import argparse
import configparser
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from nacl.signing import SigningKey

def write_new(path, data, mode=0o600):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, mode)
    with os.fdopen(fd, "wb") as output:
        output.write(data)
        output.flush()
        os.fsync(output.fileno())

def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    keygen = sub.add_parser("keygen")
    keygen.add_argument("--private", required=True, type=Path)
    keygen.add_argument("--public", required=True, type=Path)
    pack = sub.add_parser("pack")
    pack.add_argument("--profile", required=True, type=Path)
    pack.add_argument("--config", required=True, type=Path)
    pack.add_argument("--dtb", type=Path)
    pack.add_argument("--private", required=True, type=Path)
    pack.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.command == "keygen":
        key = SigningKey.generate()
        write_new(args.private, bytes(key))
        write_new(args.public, bytes(key.verify_key), 0o644)
        return
    if args.output.suffix != ".syspck" or args.output.exists():
        parser.error("output must be a new .syspck file")
    ini = configparser.ConfigParser(interpolation=None, strict=True)
    ini.optionxform = str
    ini.read_string(args.profile.read_text(encoding="ascii"))
    if ini.sections() != ["SystemPackage"] or ini.defaults():
        parser.error("one [SystemPackage] section is required")
    values = dict(ini["SystemPackage"])
    data = args.config.read_bytes()
    if len(data) > 8192:
        parser.error("config exceeds 8 KiB")
    values["FormatVersion"] = "1"
    values["ConfigSHA256"] = hashlib.sha256(data).hexdigest()
    files = {"config.conf": data}
    if args.dtb:
        dtb = args.dtb.read_bytes()
        if len(dtb) > 512 * 1024:
            parser.error("DTB exceeds 512 KiB")
        files["dtb.bin"] = dtb
        values["DTBSHA256"] = hashlib.sha256(dtb).hexdigest()
    manifest = ("[SystemPackage]\n" + "".join(
        key + "=" + values[key] + "\n" for key in sorted(values))).encode("ascii")
    if len(manifest) > 16384 or any("\n" in v or "\r" in v for v in values.values()):
        parser.error("invalid manifest")
    files["manifest.ini"] = manifest
    files["manifest.sig"] = SigningKey(args.private.read_bytes()).sign(manifest).signature
    tool = shutil.which("7za") or shutil.which("7z")
    if not tool:
        parser.error("7za/7z is needed on the authoring host")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="syspck-pack-") as directory:
        work = Path(directory)
        for name, content in files.items():
            (work / name).write_bytes(content)
        archive = work / "package.7z"
        subprocess.run([tool, "a", "-t7z", "-m0=LZMA2", "-mx=5", "-ms=off",
                        "-md=1m", "-mmt=1", str(archive), *sorted(files)], cwd=work, check=True)
        write_new(args.output, archive.read_bytes(), 0o644)
    print(str(args.output))

if __name__ == "__main__":
    main()
