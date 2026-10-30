/* SPDX-License-Identifier: MPL-2.0 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* VM fixture only: records the independent frontend argv. It is not a core
   host and must never be reported as independent-game execution evidence. */
int main(int argc, char **argv) {
    const char *expected[] = {"/app/runtime/frontend/lilretro",
        "/mnt/sdcard/roms/ports/probe/core.so",
        "/mnt/sdcard/roms/ports/probe/content ;literal.dat",
        "--profile", "/app/runtime/launch.json"};
    if (argc != 5) return 21;
    for (int i = 0; i < argc; ++i) if (strcmp(argv[i], expected[i])) return 22;
    if (mkdir("/app/runtime/userdata", 0700)) return 23;
    FILE *out = fopen("/app/runtime/userdata/argv.txt", "w");
    if (!out) return 24;
    for (int i = 0; i < argc; ++i) fprintf(out, "%s\n", argv[i]);
    return fclose(out) ? 25 : 0;
}
