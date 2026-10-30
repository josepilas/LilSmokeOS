/* SPDX-License-Identifier: MPL-2.0
 * Copyright (c) 2026 Jose Pilas.
 */
#include <lilsmoke/app.h>
#include <stdio.h>

int main(void) {
    char path[8192];
    const char *id = lilsmoke_app_id();
    const char *name = lilsmoke_app_name();
    int result = lilsmoke_open_rom_path(path, sizeof path);
    if (!id || !name || !lilsmoke_app_directory()) {
        fputs("Launch this example through LilSmoke Applications.\n", stderr);
        return 2;
    }
    printf("SDK %s: %s [%s]\n", LILSMOKE_SDK_VERSION, name, id);
    if (result < 0) { perror("Open Core environment"); return 2; }
    if (!result) printf("Logical ROM path: %s\n", path);
    else puts("Normal application launch; no ROM arguments.");
    /* A diagnostic, not a graphical app or an emulator. Output goes to app logs. */
    return 0;
}
