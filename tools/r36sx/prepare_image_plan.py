#!/usr/bin/env python3
"""Validate the inspected R36SX backup and emit an image plan, never an image."""
import argparse
import json
from pathlib import Path

ARCHIVE_SHA256 = "d21c25f9ea95d870825ccd0d952edd1fa80193272fe7ac14fc20e19379eb3a2c"
BOOT = ("AcXtrnal", "advapi32.dll", "ApplicationFrame.dll", "Bubbles.scr", "dtb.bin",
        "vmlinux.uImage", "avp.uImage", "driver.so", "rkgame", "xgame-logo.bmp")


def build_plan(inventory, probe):
    if inventory["sha256"] != ARCHIVE_SHA256 or probe.get("archive_sha256") != ARCHIVE_SHA256:
        raise ValueError("Backup does not match the inspected R36SX 2.7 lineage")
    facts = probe["facts"]
    fb = facts["boot_framebuffer"]
    if fb["xres"]["cells"] != [640] or fb["yres"]["cells"] != [480] or facts["rotation"] != [0]:
        raise ValueError("Unexpected panel geometry")
    protected = probe["protection_manifest"]
    if not protected["all_archive_crc_matches"] or len(protected["entries"]) != 18:
        raise ValueError("Protected stock-file matrix is incomplete")
    members = {row["path"].split("/", 1)[-1]: row for row in inventory["members"] if not row["directory"]}
    preserve = {"cubegm/" + name for name in BOOT} | set(protected["entries"])
    missing = preserve - members.keys()
    if missing:
        raise ValueError("Missing stock dependencies: " + ", ".join(sorted(missing)))
    # The stock directory backup carries a flattened BusyBox root. Keep its
    # files in the private build plan until the actual kernel route is tested.
    rootfs = sorted(path for path in members if path.startswith("rootfs/"))
    os_mib, rom_mib, start = 1536, 1024, 2048
    rom_start = start + os_mib * 2048
    return {
        "status": "research-and-private-build-plan-only",
        "image_generated": False, "physical_device_verified": False,
        "archive_sha256": ARCHIVE_SHA256,
        "profile_id": "r36sx-2.7-owner-backup-20261022",
        "initial_settings": {"DEVICE_MODE": "handheld", "DISPLAY_WIDTH": 640,
                             "DISPLAY_HEIGHT": 480, "TV_SAFE_AREA_PERCENT": 0,
                             "SHOW_BATTERY": "on"},
        "partitions": [
            {"index": 1, "label": "LILOS", "filesystem": "fat32", "start_sector": start,
             "sectors": os_mib * 2048, "bootable": True},
            {"index": 2, "label": "LILROMS", "filesystem": "fat32", "start_sector": rom_start,
             "sectors": rom_mib * 2048, "expand_on_first_boot": True},
        ],
        "mbr_policy": "Standard MBR candidate for SPI-resident firmware; no M8 raw boot gap borrowed. Exact R36SX boot acceptance remains unverified.",
        "stock_preserve_byte_exact": {path: members[path] for path in sorted(preserve)},
        "stock_rootfs_private_preserve": rootfs,
        "exclude": ["ROMs", "BIOS files", "game screenshots/artwork", "saves", "states",
                    "favorites/recent/history lists", "stock media", "System Volume Information",
                    "chkdsk.exe", "Chkdsk.bat", "existing pagefile.sys"],
        "boot_adapter_proposal": {
            "route": "stock-protected boot/menu -> setting.xml absolute autorun -> new libretro bootstrap -> LilSmoke runtime",
            "preserve": "Do not replace protected rkgame/driver/DTBs/cubevol or insert the M8 icube hook",
            "stock_core_candidate": "cubegm/cores/libemu_md.so",
            "dummy_content_candidate": "MD/lilsmoke.md (authored non-game marker)",
            "status": "primary-source design reference; not tested on this exact protected rkgame",
        },
        "memory": {"dtb_linux_address_range_bytes": facts["linux_memory_range_dtb"][1],
                   "bubbles_linux_address_range_bytes": facts["linux_memory_range_bubbles"][1],
                   "active_variant_unknown": True,
                   "qa_minimum_ram_mib": 48,
                   "policy": "Budget against the smaller tree until a real-device /proc/meminfo capture selects the active variant. Do not rewrite memory ranges."},
        "required_qualification": [
            "Parent UI smoothness work completes before image generation",
            "Compile bootstrap against stock glibc MIPS O32/hard-float ABI, not a musl-linked plugin loaded into glibc",
            "Hardware accept/launch path for this exact wrapped rkgame",
            "Panel/cubevol shared-memory input survives chroot and transitions to every emulator",
            "640x480 output, panel/backlight/headphones/battery and native audio",
            "Safe shutdown respects the handheld power controller and flushes both volumes",
            "First-boot FAT growth tested on file-backed images, never physical cards during CI",
            "Audit GPL/LGPL corresponding source and separate stock vendor redistribution rights before public upload",
            "No commercial ROM/BIOS/artwork or user-private data in release",
        ],
        "builder_reuse": {
            "tools/m8/stock_format.py": "Reuse MBR parsing/verification for plain FAT images; uimage() does not accept these wrapped blobs",
            "tools/m8/build_stock_candidate.py": "Reuse prepare_runtime, prepare_growable_fat and assemble only after independent file-backed tests; main() is M8-only",
            "tools/device-adapter/adapter.py": "Current profile/HOOK is M8-only; do not weaken exact hashes or label the R36SX profile certified",
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inventory", type=Path)
    parser.add_argument("probe", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    plan = build_plan(json.loads(args.inventory.read_text(encoding="utf-8")),
                      json.loads(args.probe.read_text(encoding="utf-8")))
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(plan, output, indent=2)
        output.write("\n")
    print(json.dumps({"output": str(args.output), "image_generated": False,
                      "protected_and_boot_files": len(plan["stock_preserve_byte_exact"]),
                      "rootfs_files": len(plan["stock_rootfs_private_preserve"]),
                      "partitions": plan["partitions"]}, indent=2))


if __name__ == "__main__":
    main()
