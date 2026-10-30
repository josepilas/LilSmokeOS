#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
make -C /root/lilsmoke-os/build/buildroot O=/root/lilsmoke-os/build/buildroot/output PARALLEL_JOBS="${LILSMOKE_BUILD_JOBS:-1}" -j"${LILSMOKE_BUILD_JOBS:-1}" lilsmoke-ui-rebuild
