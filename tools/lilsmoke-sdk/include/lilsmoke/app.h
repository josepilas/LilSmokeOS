/* SPDX-License-Identifier: MPL-2.0
 * Copyright (c) 2026 Jose Pilas.
 * Header-only conveniences over existing application environment variables.
 * No OS service, process launcher, filesystem access, or binary ABI is added.
 */
#ifndef LILSMOKE_SDK_APP_H
#define LILSMOKE_SDK_APP_H
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LILSMOKE_SDK_VERSION "0.1.0-dev"

/* Borrowed environment pointers; NULL when not supplied by the app launcher. */
static inline const char *lilsmoke_app_id(void) { return getenv("LILSMOKE_APP_ID"); }
static inline const char *lilsmoke_app_name(void) { return getenv("LILSMOKE_APP_NAME"); }
static inline const char *lilsmoke_app_directory(void) { return getenv("LILSMOKE_APP_DIR"); }

/* Returns 0: lexical path constructed; 1: normal menu launch; -1: invalid/buffer.
 * This does NOT open, canonicalize, extract, or authorize a ROM. Symlinks and
 * filesystem races remain the caller's responsibility. Never pass it to eval.
 */
static inline int lilsmoke_open_rom_path(char *output, size_t capacity) {
    const char *core = getenv("LILSMOKE_OPEN_CORE");
    const char *rom = getenv("LILSMOKE_OPEN_ROM");
    const char *root = getenv("LILSMOKE_ROM_ROOT");
    const char *part;
    size_t length;
    int count;
    if (!output || !capacity) { errno = EINVAL; return -1; }
    output[0] = '\0';
    if (!core && !rom) return 1;
    if (!core || !*core || !rom || !*rom || strlen(core) > 128 ||
        strlen(rom) > 1024 || *rom == '/' || strstr(core, "..")) {
        errno = EINVAL; return -1;
    }
    for (const char *p = core; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
              *p == '_' || *p == '-')) { errno = EINVAL; return -1; }
    }
    part = rom;
    for (const unsigned char *p = (const unsigned char *)rom;; ++p) {
        if (*p == '/' || !*p) {
            length = (size_t)((const char *)p - part);
            if (!length || (length == 1 && part[0] == '.') ||
                (length == 2 && part[0] == '.' && part[1] == '.')) {
                errno = EINVAL; return -1;
            }
            if (!*p) break;
            part = (const char *)p + 1;
        } else if (*p < 32 || *p == 127 || *p == '\\') {
            errno = EINVAL; return -1;
        }
    }
    if (!root) root = "/storage/roms";
    if (*root != '/' || strlen(root) > 4096) { errno = EINVAL; return -1; }
    length = strlen(root);
    while (length > 1 && root[length - 1] == '/') --length;
    count = snprintf(output, capacity, "%.*s%s%s/%s", (int)length, root,
                     length == 1 ? "" : "/", core, rom);
    if (count < 0 || (size_t)count >= capacity) {
        output[0] = '\0'; errno = ENAMETOOLONG; return -1;
    }
    return 0;
}
#endif
