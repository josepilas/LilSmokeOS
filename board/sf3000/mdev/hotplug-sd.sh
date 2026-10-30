#!/bin/sh
# Never automount a system partition or the USB-owned ROM partition.
DEVNAME=${MDEV:-}
case "$DEVNAME" in ''|*[!A-Za-z0-9]*) exit 0 ;; esac
device=/dev/$DEVNAME
label=$(blkid "$device" 2>/dev/null)
case "$label" in *'LABEL="LILOS"'*|*'LABEL="LILROMS"'*) exit 0 ;; esac
if [ -f /run/lilsmoke-storage/roms-device ] &&
   [ "$(cat /run/lilsmoke-storage/roms-device)" = "$device" ]; then exit 0; fi
# LILROMS may have been reformatted and lost its label: check its sibling p1.
case "$DEVNAME" in *p2) sibling=${DEVNAME%p2}p1 ;; *2) sibling=${DEVNAME%2}1 ;; *) sibling= ;; esac
if [ -n "$sibling" ]; then
    case "$(blkid "/dev/$sibling" 2>/dev/null)" in *'LABEL="LILOS"'*) exit 0 ;; esac
fi
case "${ACTION:-add}" in
    add)
        mountpoint -q /mnt/sd && exit 0
        mkdir -p /mnt/sd
        mount -t vfat -o nosuid,nodev,noexec "$device" /mnt/sd 2>/dev/null ||
        mount -t exfat -o nosuid,nodev,noexec "$device" /mnt/sd 2>/dev/null || true
        ;;
    remove)
        source=$(awk '$2=="/mnt/sd" {print $1}' /proc/mounts)
        [ "$source" != "$device" ] || umount /mnt/sd
        ;;
esac
exit 0
