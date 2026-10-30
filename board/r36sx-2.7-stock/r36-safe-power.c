/* SPDX-License-Identifier: MPL-2.0
 * Keep the tested storage guard; first-boot geometry reload uses the same
 * unmount/flush/clean-status sequence, but reboots instead of powering off. */
#define _GNU_SOURCE
#include <sys/reboot.h>
#include <stdlib.h>
#include <string.h>
static int r36_reboot(int command){
    const char *restart=getenv("LILSMOKE_R36_GEOMETRY_REBOOT");
    if(command==RB_POWER_OFF&&restart&&!strcmp(restart,"1"))command=RB_AUTOBOOT;
    return reboot(command);
}
#define reboot r36_reboot
#include "../segam-m8-v6-stock/m8-safe-power.c"
