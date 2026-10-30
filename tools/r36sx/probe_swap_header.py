#!/usr/bin/env python3
"""Inspect only swap header metadata from the owner's archive; save no swap bytes."""
import argparse
import json
import struct
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("archive")
p.add_argument("--sevenzip", default="7z")
a = p.parse_args()
process = subprocess.Popen([a.sevenzip, "x", "-so", a.archive,
                           "usbimager-20260530T0359/cubegm/pagefile.sys"],
                          stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
try:
    data = process.stdout.read(65536)
finally:
    process.stdout.close()
    process.terminate()
    process.wait(timeout=30)
sizes = [size for size in (4096, 8192, 16384, 32768, 65536)
         if data[size - 10:size] == b"SWAPSPACE2"]
print(json.dumps({"header_bytes_inspected": len(data), "page_sizes": sizes,
                  "version": struct.unpack_from("<I", data, 1024)[0],
                  "last_page": struct.unpack_from("<I", data, 1028)[0],
                  "swap_bytes_saved": False}, indent=2))
if sizes != [4096]:
    raise SystemExit("Expected 4 KiB stock swap; do not build a mismatched new header")
