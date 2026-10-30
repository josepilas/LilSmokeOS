#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
Q=/root/lilsmoke-applications-20261009
work=$(mktemp -d "$Q/fat-qa-XXXXXX")
mounted=0
cleanup() {
    if [ "$mounted" = 1 ]; then umount "$work/mount"; fi
    rm -f "$work/card.fat"
    rmdir "$work/mount" "$work"
}
trap cleanup EXIT HUP INT TERM
mkdir "$work/mount"
truncate -s 128M "$work/card.fat"
mkfs.vfat -F 32 -n APPS_QA "$work/card.fat"
mount -o loop,rw,utf8 "$work/card.fat" "$work/mount"
mounted=1
python3 "$P/tools/apps/test_packages.py" --binary "$Q/host-helper" --work-dir "$work/mount"
sync
umount "$work/mount"
mounted=0
fsck.vfat -n "$work/card.fat"
printf 'PASS real FAT32 package operations and read-only filesystem check\n'
