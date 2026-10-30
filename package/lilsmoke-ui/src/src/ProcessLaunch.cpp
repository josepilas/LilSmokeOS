#include "ProcessLaunch.h"
#include "Log.h"

#include <cerrno>
#include <cstring>
#include <utility>
#include <cstdlib>
#include <unistd.h>

extern char **environ;

namespace lilsmoke {
namespace {
std::optional<ProcessLaunch> pendingLaunch;
}

void requestProcessLaunch(ProcessLaunch request) {
    pendingLaunch = std::move(request);
}

bool processLaunchRequested() { return pendingLaunch.has_value(); }

std::optional<ProcessLaunch> takeProcessLaunch() {
    return std::exchange(pendingLaunch, std::nullopt);
}

int executeProcessLaunch(ProcessLaunch request) {
    const char *m8 = std::getenv("LILSMOKE_HCFB");
    const char *loading = "/usr/lib/lilsmoke/m8-loading-run";
    if (m8 && !std::strcmp(m8, "1") && request.executable != loading && !access(loading, X_OK)) {
        // SDL has already shut down; the optional helper now owns scanout until
        // the replacement application's first real software-rendered frame.
        if (!request.arguments.empty()) request.arguments.erase(request.arguments.begin());
        request.arguments.insert(request.arguments.begin(), request.executable);
        request.arguments.insert(request.arguments.begin(), "m8-loading-run");
        request.executable = loading;
    }
    std::vector<char *> argv;
    for (auto &argument : request.arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    execve(request.executable.c_str(), argv.data(), ::environ);
    LOG_ERROR("Launch failed: %s: %s", request.executable.c_str(), strerror(errno));
    return 1;
}
} // namespace lilsmoke
