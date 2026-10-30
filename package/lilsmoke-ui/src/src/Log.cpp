// LilSmokeUI — Log.cpp

#include "Log.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace lilsmoke {

static bool debug_enabled() {
    static int v = -1;
    if (v < 0) v = getenv("LILSMOKE_UI_DEBUG") ? 1 : 0;
    return v == 1;
}

static const char *level_prefix(LogLevel l) {
    switch (l) {
        case LogLevel::Error: return "[lilsmoke-ui] ERROR:";
        case LogLevel::Warn:  return "[lilsmoke-ui] WARN: ";
        case LogLevel::Info:  return "[lilsmoke-ui] INFO: ";
        case LogLevel::Debug: return "[lilsmoke-ui] DEBUG:";
    }
    return "[lilsmoke-ui]";
}

void log(LogLevel level, const char *fmt, ...) {
    if (level == LogLevel::Debug && !debug_enabled()) return;

    // Timestamp
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
#ifdef _WIN32
    localtime_s(&tm, &ts.tv_sec);
#else
    localtime_r(&ts.tv_sec, &tm);
#endif
    fprintf(stderr, "%02d:%02d:%02d.%03ld %s ",
            tm.tm_hour, tm.tm_min, tm.tm_sec,
            ts.tv_nsec / 1000000,
            level_prefix(level));

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

}  // namespace lilsmoke
