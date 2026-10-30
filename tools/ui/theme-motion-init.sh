#!/bin/busybox sh
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys
echo THEME_MOTION_KERNEL_BEGIN
/bin/busybox cat /proc/cpuinfo
/bin/busybox head -n 5 /proc/meminfo
/qa
status=$?
echo THEME_MOTION_KERNEL_EXIT=$status
/bin/busybox poweroff -f
