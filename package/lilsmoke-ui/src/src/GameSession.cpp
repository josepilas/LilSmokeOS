#include "GameHistory.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace lilsmoke {
namespace {
volatile sig_atomic_t sessionChild = 0;
void forwardSignal(int signal) {
    if (sessionChild > 0) kill(sessionChild, signal);
}
}

int runGameSession(const RecentGame &game, const std::string &executable,
                   const std::vector<std::string> &arguments, GameHistory &history) {
    if (arguments.empty()) return 2;
    // CLOEXEC distinguishes an exec failure from a program that really exited 0.
    int errors[2];
    if (pipe(errors) != 0) return 1;
    if (fcntl(errors[1], F_SETFD, FD_CLOEXEC) != 0) { close(errors[0]); close(errors[1]); return 1; }
    sigset_t blocked, oldMask;
    sigemptyset(&blocked);
    for (int sig : {SIGTERM, SIGINT, SIGHUP}) sigaddset(&blocked, sig);
    sigprocmask(SIG_BLOCK, &blocked, &oldMask);
    const uint64_t start = monotonicMilliseconds();
    const pid_t child = fork();
    if (child == 0) {
        sigprocmask(SIG_SETMASK, &oldMask, nullptr);
        close(errors[0]);
        std::vector<char *> argv;
        for (const auto &arg : arguments) argv.push_back(const_cast<char *>(arg.c_str()));
        argv.push_back(nullptr);
        execv(executable.c_str(), argv.data());
        const int error = errno;
        const ssize_t ignored = write(errors[1], &error, sizeof(error));
        (void)ignored;
        _exit(127);
    }
    close(errors[1]);
    if (child < 0) { close(errors[0]); sigprocmask(SIG_SETMASK, &oldMask, nullptr); return 1; }
    sessionChild = child;
    struct sigaction handler{}, oldHandlers[3]{};
    handler.sa_handler = forwardSignal;
    sigemptyset(&handler.sa_mask);
    const int signals[] = {SIGTERM, SIGINT, SIGHUP};
    for (size_t i = 0; i < 3; ++i) sigaction(signals[i], &handler, &oldHandlers[i]);
    sigprocmask(SIG_SETMASK, &oldMask, nullptr);
    int execError = 0;
    ssize_t count;
    do { count = read(errors[0], &execError, sizeof(execError)); } while (count < 0 && errno == EINTR);
    close(errors[0]);
    int status = 0;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    const uint64_t end = monotonicMilliseconds();
    sigprocmask(SIG_BLOCK, &blocked, &oldMask);
    sessionChild = 0;
    for (size_t i = 0; i < 3; ++i) sigaction(signals[i], &oldHandlers[i], nullptr);
    sigprocmask(SIG_SETMASK, &oldMask, nullptr);
    if (waited != child || count != 0) return 127;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        if (start && end >= start && !history.record(game, end - start))
            std::fprintf(stderr, "Session completed; LILROMS history unavailable or not writable\n");
        return 0;
    }
    // No duration heuristic: paused games still count in full on a normal exit.
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}
} // namespace lilsmoke
