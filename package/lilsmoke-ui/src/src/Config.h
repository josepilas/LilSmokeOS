// LilSmokeUI — Config.h
//
// Reads /persist/config/lilsmoke.conf (key=value, # comments).
// Falls back to /etc/lilsmoke/lilsmoke.conf.default if persist doesn't exist.
//
// We don't use a heavyweight INI parser — the file is simple enough that
// a line-by-line parser is sufficient and stays under 1 KB of code.

#pragma once

#include <string>
#include <unordered_map>

namespace lilsmoke {

class Config {
public:
    Config();

    // Load from file. Returns true on success.
    // If path doesn't exist, falls back to default path.
    bool load(const std::string &path);

    // Get a string value, or default if missing.
    std::string get(const std::string &key, const std::string &def = "") const;

    // Get an integer value, or default if missing / unparsable.
    int getInt(const std::string &key, int def = 0) const;

    // Get a boolean value ("1" / "true" / "yes" = true).
    bool getBool(const std::string &key, bool def = false) const;

    // Set a value (in-memory only; call save() to persist).
    void set(const std::string &key, const std::string &value);

    // Save back to the loaded path.
    bool save() const;

    // Get the loaded path (for settings UI to display).
    const std::string &path() const { return path_; }

private:
    std::string path_;
    std::unordered_map<std::string, std::string> values_;
};

}  // namespace lilsmoke
