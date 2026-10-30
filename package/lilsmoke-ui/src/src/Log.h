// LilSmokeUI — Log.h
//
// Tiny stderr-based logger. No file logging — keeps the binary small and
// the boot log clean. Set LILSMOKE_UI_DEBUG=1 in the environment to
// enable debug messages.

#pragma once

#include <cstdio>

namespace lilsmoke {

enum class LogLevel {
    Error,
    Warn,
    Info,
    Debug,
};

void log(LogLevel level, const char *fmt, ...);

}  // namespace lilsmoke

#define LOG_ERROR(...) ::lilsmoke::log(::lilsmoke::LogLevel::Error, __VA_ARGS__)
#define LOG_WARN(...)  ::lilsmoke::log(::lilsmoke::LogLevel::Warn,  __VA_ARGS__)
#define LOG_INFO(...)  ::lilsmoke::log(::lilsmoke::LogLevel::Info,  __VA_ARGS__)
#define LOG_DEBUG(...) ::lilsmoke::log(::lilsmoke::LogLevel::Debug, __VA_ARGS__)
