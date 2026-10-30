#!/bin/sh
# Called by board post-build hooks. Trust comes from immutable image policy.
set -eu
target=$1
board=$2
case "$board" in
    qemu-mipsel) soc=qemu-malta; revision=malta-dev; binding=qemu-malta; compatible=mti,malta; ram=256; arch=mipsel; abi=o32-hard-float ;;
    sf3000) soc=hichip-hc16xx; revision=UNVERIFIED; binding=linux-hichip; compatible=datafrog,sf3000; ram=256; arch=mipsel; abi=o32-hard-float ;;
    gb350) soc=UNVERIFIED; revision=UNVERIFIED; binding=UNVERIFIED; compatible=UNVERIFIED; ram=128; arch=mipsel; abi=o32-hard-float ;;
    vbox-x86_64) soc=pc-test; revision=virtual-dev; binding=none; compatible=none; ram=256; arch=x86_64; abi=x86_64 ;;
    *) exit 1 ;;
esac
kernel_id=UNVERIFIED
if [ -f "$target/../images/vmlinux" ]; then
    kernel_id=$(sha256sum "$target/../images/vmlinux" | cut -d' ' -f1)
fi
install -d "$target/etc/lilsmoke/syspck-keys"
printf '%s\n' "PolicyVersion=1" "Board=$board" "HardwareRevision=$revision" \
    "SoC=$soc" "Architecture=$arch" "ABI=$abi" "Libc=musl" "KernelID=$kernel_id" \
    "RAMMB=$ram" "DTBinding=$binding" "DTBCompatible=$compatible" \
    "HardwareValidated=no" "ApprovedDTBSHA256=none" > "$target/etc/lilsmoke/syspck-policy.conf"
