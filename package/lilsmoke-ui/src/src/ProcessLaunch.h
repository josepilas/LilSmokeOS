#pragma once

#include <optional>
#include <string>
#include <vector>

namespace lilsmoke {

struct ProcessLaunch {
    std::string executable;
    std::vector<std::string> arguments;
};

void requestProcessLaunch(ProcessLaunch request);
bool processLaunchRequested();
std::optional<ProcessLaunch> takeProcessLaunch();
int executeProcessLaunch(ProcessLaunch request);

} // namespace lilsmoke
