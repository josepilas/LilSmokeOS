/* SPDX-License-Identifier: MPL-2.0 */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The compiler's glibc stays private; game objects and executables use musl. */
int main(int argc, char **argv) {
    char root[PATH_MAX], loader[PATH_MAX], libraries[PATH_MAX * 3];
    char program[PATH_MAX], prefix[PATH_MAX + 3], self[PATH_MAX];
    ssize_t bytes = readlink("/proc/self/exe", root, sizeof(root) - 1);
    if (bytes <= 0 || bytes >= (ssize_t)sizeof(root) - 1) return 126;
    root[bytes] = 0;
    strcpy(self,root);
    char *slash = strrchr(root, '/');
    if (!slash) return 126;
    char name[64];
    if (strlen(slash + 1) >= sizeof(name)) return 126;
    strcpy(name, slash + 1);
    *slash = 0;
    slash = strrchr(root, '/');
    if (!slash || strcmp(slash + 1, "wrappers")) return 126;
    *slash = 0;
    const char *tool = NULL;
    int driver = 0;
    if (!strcmp(name, "cc") || !strcmp(name, "gcc")) {
        tool = "usr/bin/mipsel-linux-gnu-gcc-12";
        driver = 1;
    } else if (!strcmp(name, "cpp")) {
        tool = "usr/bin/mipsel-linux-gnu-cpp-12";
        driver = 1;
    } else if (!strcmp(name, "cc1")) tool = "usr/lib/gcc/mipsel-linux-gnu/12/cc1";
    else if (!strcmp(name, "as")) tool = "usr/bin/mipsel-linux-gnu-as";
    else if (!strcmp(name, "ld")) tool = "usr/bin/mipsel-linux-gnu-ld";
    else if (!strcmp(name, "objcopy")) tool = "usr/bin/mipsel-linux-gnu-objcopy";
    else if (!strcmp(name, "collect2")) tool = "usr/lib/gcc/mipsel-linux-gnu/12/collect2";
    else if (!strcmp(name, "make")) tool = "usr/bin/make";
    else { fprintf(stderr, "Unknown native compiler tool: %s\n", name); return 126; }
    if (snprintf(loader, sizeof(loader), "%s/lib/mipsel-linux-gnu/ld.so.1", root) >= (int)sizeof(loader)
        || snprintf(libraries, sizeof(libraries), "%s/lib/mipsel-linux-gnu:%s/usr/lib/mipsel-linux-gnu", root, root) >= (int)sizeof(libraries)
        || snprintf(program, sizeof(program), "%s/%s", root, tool) >= (int)sizeof(program)
        || snprintf(prefix, sizeof(prefix), "-B%s/wrappers/", root) >= (int)sizeof(prefix)) return 126;
    char **arguments = calloc((size_t)argc + 8, sizeof(*arguments));
    if (!arguments) return 126;
    size_t next = 0;
    arguments[next++] = loader;
    arguments[next++] = "--inhibit-cache";
    arguments[next++] = "--library-path";
    arguments[next++] = libraries;
    /* GNU Make restarts itself after regenerating an included makefile. */
    if (!strcmp(name,"make")) {
        arguments[next++] = "--argv0";
        arguments[next++] = self;
    }
    arguments[next++] = program;
    if (driver) arguments[next++] = prefix;
    for (int index = 1; index < argc; ++index) arguments[next++] = argv[index];
    unsetenv("GCC_EXEC_PREFIX");
    unsetenv("COMPILER_PATH");
    unsetenv("LIBRARY_PATH");
    unsetenv("LD_PRELOAD");
    if (getenv("SM64_NATIVE_TOOL_TRACE"))
        fprintf(stderr,"NativeTool=%s Program=%s QemuPrefix=%s\n",name,program,
            getenv("QEMU_LD_PREFIX")?getenv("QEMU_LD_PREFIX"):"unset");
    execv(loader, arguments);
    fprintf(stderr, "Cannot execute bundled native compiler: %s\n", strerror(errno));
    free(arguments);
    return 126;
}
