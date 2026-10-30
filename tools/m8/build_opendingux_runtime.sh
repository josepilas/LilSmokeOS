#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
B=/root/lilsmoke-os/build/buildroot/output
make -C /root/lilsmoke-os/build/buildroot O="$B" olddefconfig
make -C /root/lilsmoke-os/build/buildroot O="$B" -j2 lilsmoke-opendingux-rebuild
