// SPDX-License-Identifier: MPL-2.0
#include "ProcessLaunch.h"
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

using namespace lilsmoke;
static bool wrapperPresent = true;
static std::string executable;
static std::vector<std::string> arguments;

extern "C" int __real_access(const char *, int);
extern "C" int __wrap_access(const char *path, int mode) {
    if (!std::strcmp(path, "/usr/lib/lilsmoke/m8-loading-run")) {
        assert(mode == X_OK);
        return wrapperPresent ? 0 : -1;
    }
    return __real_access(path, mode);
}
extern "C" int __wrap_execve(const char *path, char *const argv[], char *const[]) {
    executable = path;
    arguments.clear();
    for (size_t i = 0; argv[i]; ++i) arguments.emplace_back(argv[i]);
    errno = ENOENT; // Returning is intentional: no real process or loading logo starts.
    return -1;
}
static void launch(const char *target = "/usr/lib/lilsmoke/lilsmoke-treefrog-launch") {
    assert(executeProcessLaunch({target, {"lilsmoke-treefrog-launch", "hexahop"}}) == 1);
}
int main() {
    unsetenv("LILSMOKE_HCFB");
    launch();
    assert(executable == "/usr/lib/lilsmoke/lilsmoke-treefrog-launch");
    assert(arguments == std::vector<std::string>({"lilsmoke-treefrog-launch", "hexahop"}));
    setenv("LILSMOKE_HCFB", "1", 1);
    launch();
    assert(executable == "/usr/lib/lilsmoke/m8-loading-run");
    assert(arguments == std::vector<std::string>({"m8-loading-run",
        "/usr/lib/lilsmoke/lilsmoke-treefrog-launch", "hexahop"}));
    wrapperPresent = false;
    launch();
    assert(executable == "/usr/lib/lilsmoke/lilsmoke-treefrog-launch");
    assert(arguments == std::vector<std::string>({"lilsmoke-treefrog-launch", "hexahop"}));
    wrapperPresent = true;
    launch("/usr/lib/lilsmoke/m8-loading-run");
    assert(executable == "/usr/lib/lilsmoke/m8-loading-run");
    assert(arguments == std::vector<std::string>({"lilsmoke-treefrog-launch", "hexahop"}));
    unsetenv("LILSMOKE_HCFB");
}
