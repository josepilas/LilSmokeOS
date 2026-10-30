"""Read-only identity and instruction checks for the user's exact audio driver.

This checks evidence, not device behavior. No driver is loaded or executed.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import struct


EXPECTED_SHA256 = "5b761f9a5370454737c2b716acfca421dbb82c22112c5d4f3e6a8f604503d1a3"
ANCHORS = {
    0x6418: (0x24060270, "memset length = 624, not malloc object length 632"),
    0x6448: (0xA2140002, "bits_per_coded_sample at offset 2"),
    0x644C: (0xAE030004, "codec ID at offset 4"),
    0x6450: (0xA2130003, "channels at offset 3"),
    0x6454: (0xAE12000C, "sample rate at offset 12"),
    0x6458: (0xAE110018, "snd_devs at offset 24"),
    0x645C: (0xAE02001C, "flush threshold at offset 28"),
    0x6460: (0xAE000240, "buffer start at offset 576"),
    0x6464: (0xAE020244, "buffer end at offset 580"),
    0x64A0: (0x3C128270, "AUDDEC_INIT upper half 0x8270"),
    0x64B0: (0x26450301, "AUDDEC_INIT lower half 0x0301"),
    0x64CC: (0x3C052000, "AUDDEC_START upper half 0x2000"),
    0x64E4: (0x24A50304, "AUDDEC_START lower half 0x0304"),
    0x6500: (0x24A51503, "GET_AV_BUFFER_SIZE lower half 0x1503"),
    0x65F0: (0x3C054004, "GET_AV_BUFFERING_PERCENT upper half 0x4004"),
    0x65FC: (0x24A51502, "GET_AV_BUFFERING_PERCENT lower half 0x1502"),
    0x6674: (0x7CC2E804, "packet byte count in low 30 bits"),
    0x667C: (0xAFA30018, "packet PTS = -1"),
    0x6684: (0xAFA20020, "packet size/flags at offset 8"),
    0x6698: (0x24060010, "separate 16-byte header write"),
    0x66C8: (0x02003025, "separate PCM payload length"),
    0x677C: (0x24A50305, "AUDDEC_FLUSH lower half 0x0305"),
    0x6A84: (0x24040010, "decoder init: bits = 16"),
    0x6A8C: (0x02203025, "decoder init: third argument is rate"),
    0x6A98: (0x02002825, "decoder init: second argument is channels"),
    0x6B20: (0x3C058001, "SND SET_VOLUME upper half 0x8001"),
    0x6B28: (0x27A60018, "SND SET_VOLUME pointer to byte, not scalar"),
    0x6B2C: (0x24A5080B, "SND SET_VOLUME lower half 0x080b"),
    0x6C34: (0x2842005B, "stock buffering threshold < 91"),
    0x6C64: (0x00113080, "playframe second argument = stereo frames, times 4 bytes"),
}


def verify(path: Path) -> dict:
    with path.open("rb") as source:
        st = os.fstat(source.fileno())
        if not stat.S_ISREG(st.st_mode) or not 52 <= st.st_size <= 16 * 1024 * 1024:
            raise ValueError("driver must be a bounded regular file")
        data = source.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError(f"driver identity mismatch: {digest}")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    if header[0][:7] != b"\x7fELF\x01\x01\x01" or header[2] != 8:
        raise ValueError("not ELF32 little-endian MIPS")
    phoff, phsize, phcount = header[5], header[9], header[10]
    if phsize != 32 or phoff + phsize * phcount > len(data):
        raise ValueError("invalid ELF program header table")
    segments = [struct.unpack_from("<IIIIIIII", data, phoff + i * phsize)
                for i in range(phcount)]
    checked = []
    for address, (expected, meaning) in ANCHORS.items():
        segment = next((s for s in segments if s[0] == 1 and
                        s[2] <= address and address + 4 <= s[2] + s[4]), None)
        if segment is None:
            raise ValueError(f"unmapped instruction 0x{address:x}")
        offset = segment[1] + address - segment[2]
        if offset + 4 > len(data):
            raise ValueError("instruction outside file")
        actual = struct.unpack_from("<I", data, offset)[0]
        if actual != expected:
            raise ValueError(f"instruction mismatch at 0x{address:x}: 0x{actual:08x}")
        checked.append({"address": f"0x{address:x}", "evidence": meaning})
    return {"driver": str(path), "sha256": digest,
            "abi": "MIPS little-endian o32; init 0x82700301, 624-byte config",
            "instruction_checks": checked, "hardware_accessed": False}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", type=Path,
                        default=Path("/root/lilsmoke-m8-bringup-2026-10-06/stock/driver.so"))
    args = parser.parse_args()
    try:
        print(json.dumps(verify(args.driver), indent=2))
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"FAIL: {error}\n")
