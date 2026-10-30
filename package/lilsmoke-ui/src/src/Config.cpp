// LilSmokeUI — Config.cpp

#include "Config.h"
#include "Log.h"
#include "FilePersistence.h"

#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace lilsmoke {

static const char *PERSIST_CONF = "/persist/config/lilsmoke.conf";
static const char *DEFAULT_CONF = "/etc/lilsmoke/lilsmoke.conf.default";

static int createDirectory(const char *path) {
#ifdef _WIN32
    return mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

static std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string stripInlineComment(const std::string &s) {
    bool inSingle = false;
    bool inDouble = false;
    bool escaped = false;

    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];

        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\' && inDouble) {
            escaped = true;
            continue;
        }
        if (c == '\'' && !inDouble) {
            inSingle = !inSingle;
            continue;
        }
        if (c == '"' && !inSingle) {
            inDouble = !inDouble;
            continue;
        }
        if (c == '#' && !inSingle && !inDouble) {
            if (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t') {
                return trim(s.substr(0, i));
            }
        }
    }

    return trim(s);
}

static std::string unquoteValue(const std::string &s) {
    if (s.size() >= 2) {
        char first = s.front();
        char last = s.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return s.substr(1, s.size() - 2);
        }
    }
    return s;
}

Config::Config() {
    // Try persist first, fall back to default
    if (!load(PERSIST_CONF)) {
        LOG_WARN("could not load %s, falling back to %s", PERSIST_CONF, DEFAULT_CONF);
        load(DEFAULT_CONF);
        // Defaults are read-only. Any setting changed by the user must be
        // written to the persistent configuration on the first save.
        path_ = PERSIST_CONF;
    }
}

bool Config::load(const std::string &path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    path_ = path;
    values_.clear();

    std::string line;
    while (std::getline(f, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;
        if (trimmed[0] == '#') continue;
        trimmed = stripInlineComment(trimmed);
        if (trimmed.empty()) continue;

        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trimmed.substr(0, eq);
        std::string val = trimmed.substr(eq + 1);
        key = trim(key);
        val = unquoteValue(trim(val));
        if (key.empty()) continue;

        values_[key] = val;
    }

    LOG_INFO("loaded config from %s (%zu keys)", path.c_str(), values_.size());
    return true;
}

std::string Config::get(const std::string &key, const std::string &def) const {
    auto it = values_.find(key);
    return it == values_.end() ? def : it->second;
}

int Config::getInt(const std::string &key, int def) const {
    auto it = values_.find(key);
    if (it == values_.end()) return def;
    try { return std::stoi(it->second); }
    catch (...) { return def; }
}

bool Config::getBool(const std::string &key, bool def) const {
    auto it = values_.find(key);
    if (it == values_.end()) return def;
    const std::string &v = it->second;
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

void Config::set(const std::string &key, const std::string &value) {
    values_[key] = value;
}

bool Config::save() const {
    if (path_.empty()) return false;

    // Ensure parent directory exists
    size_t slash = path_.find_last_of('/');
    if (slash != std::string::npos) {
        std::string dir = path_.substr(0, slash);
        std::string partial;
        size_t pos = 0;
        if (!dir.empty() && dir[0] == '/') {
            partial = "/";
            pos = 1;
        }
        while (pos <= dir.size()) {
            size_t next = dir.find('/', pos);
            std::string component = dir.substr(
                pos, next == std::string::npos ? std::string::npos : next - pos);
            if (!component.empty()) {
                if (partial.size() > 1 && partial.back() != '/') partial += '/';
                partial += component;
                if (createDirectory(partial.c_str()) != 0 && errno != EEXIST) {
                    LOG_ERROR("failed to create dir %s", partial.c_str());
                    return false;
                }
            }
            if (next == std::string::npos) break;
            pos = next + 1;
        }
    }

    std::ostringstream f;

    f << "# LilSmokeOS configuration\n";
    f << "# This file is managed by LilSmokeUI Settings.\n";
    f << "# Format: key=value (lines starting with # are comments)\n\n";

    for (const auto &kv : values_) {
        f << kv.first << "=" << kv.second << "\n";
    }

    if(!f || !durableReplace(path_,f.str())){
        LOG_ERROR("failed to safely save %s errno=%d",path_.c_str(),errno);
        return false;
    }
    LOG_INFO("saved config to %s", path_.c_str());
    return true;
}

}  // namespace lilsmoke
