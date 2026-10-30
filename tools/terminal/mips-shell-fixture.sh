#!/bin/sh
# Run the actual SDK BusyBox shell in the host PTY tests, without changing it.
SDK=${SDK:-/root/lilsmoke-os/build/buildroot/output}
exec qemu-mipsel -cpu 74Kf -L "$SDK/staging" "$SDK/target/bin/busybox" sh "$@"
