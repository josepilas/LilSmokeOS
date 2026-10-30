// LilSmokeUI — Keymap.cpp

#include "Keymap.h"
#include "Input.h"
#include "Log.h"

#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>

namespace lilsmoke {

Keymap::Keymap() = default;

InputButton Keymap::parseAction(const std::string &s) {
    if (s == "up")       return InputButton::Up;
    if (s == "down")     return InputButton::Down;
    if (s == "left")     return InputButton::Left;
    if (s == "right")    return InputButton::Right;
    if (s == "a")        return InputButton::A;
    if (s == "b")        return InputButton::B;
    if (s == "x")        return InputButton::X;
    if (s == "y")        return InputButton::Y;
    if (s == "l1")       return InputButton::L1;
    if (s == "r1")       return InputButton::R1;
    if (s == "l2")       return InputButton::L2;
    if (s == "r2")       return InputButton::R2;
    if (s == "l3")       return InputButton::L3;
    if (s == "r3")       return InputButton::R3;
    if (s == "select")   return InputButton::Select;
    if (s == "start")    return InputButton::Start;
    if (s == "home")     return InputButton::Home;
    if (s == "menu")     return InputButton::Home;  // alias
    if (s == "vol_up")   return InputButton::VolUp;
    if (s == "vol_down") return InputButton::VolDown;
    if (s == "power")    return InputButton::Power;
    return InputButton::None;
}

const char *Keymap::actionName(InputButton b) {
    switch (b) {
        case InputButton::Up:      return "up";
        case InputButton::Down:    return "down";
        case InputButton::Left:    return "left";
        case InputButton::Right:   return "right";
        case InputButton::A:       return "a";
        case InputButton::B:       return "b";
        case InputButton::X:       return "x";
        case InputButton::Y:       return "y";
        case InputButton::L1:      return "l1";
        case InputButton::R1:      return "r1";
        case InputButton::L2:      return "l2";
        case InputButton::R2:      return "r2";
        case InputButton::L3:      return "l3";
        case InputButton::R3:      return "r3";
        case InputButton::Select:  return "select";
        case InputButton::Start:   return "start";
        case InputButton::Home:    return "home";
        case InputButton::VolUp:   return "vol_up";
        case InputButton::VolDown: return "vol_down";
        case InputButton::Power:   return "power";
        case InputButton::None:    return "none";
    }
    return "none";
}

int Keymap::loadDir(const std::string &dir) {
    DIR *d = opendir(dir.c_str());
    if (!d) {
        LOG_DEBUG("Keymap: dir %s not accessible", dir.c_str());
        return 0;
    }

    // Collect .map files, sorted
    std::vector<std::string> files;
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        std::string name = ent->d_name;
        if (name.size() < 5) continue;
        if (name.compare(name.size() - 4, 4, ".map") != 0) continue;
        files.push_back(dir + "/" + name);
    }
    closedir(d);

    std::sort(files.begin(), files.end());

    int total = 0;
    for (const auto &f : files) {
        total += loadFile(f);
    }
    LOG_INFO("Keymap: loaded %d bindings from %zu files in %s",
             total, files.size(), dir.c_str());
    return total;
}

int Keymap::loadFile(const std::string &path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        LOG_DEBUG("Keymap: cannot open %s", path.c_str());
        return -1;
    }

    // Use basename as source label
    std::string source = path;
    size_t slash = source.find_last_of('/');
    if (slash != std::string::npos) source = source.substr(slash + 1);

    int count = 0;
    std::string line;
    int lineno = 0;
    while (std::getline(f, line)) {
        lineno++;
        // Strip leading whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        if (line[start] == '#') continue;

        std::istringstream ss(line.substr(start));
        std::string device, codeStr, action;
        if (!(ss >> device >> codeStr >> action)) {
            LOG_WARN("Keymap: %s line %d: parse error", path.c_str(), lineno);
            continue;
        }

        // Only handle evdev for now (js support is J5.1)
        if (device != "evdev") {
            LOG_DEBUG("Keymap: %s line %d: skipping non-evdev binding", path.c_str(), lineno);
            continue;
        }

        int code;
        try { code = std::stoi(codeStr); }
        catch (...) { LOG_WARN("Keymap: %s line %d: bad code", path.c_str(), lineno); continue; }

        InputButton btn = parseAction(action);
        if (btn == InputButton::None) {
            LOG_WARN("Keymap: %s line %d: unknown action '%s'", path.c_str(), lineno, action.c_str());
            continue;
        }

        set(code, btn);
        // Update the source file for this binding
        for (auto &b : bindings_) {
            if (b.code == code) { b.sourceFile = source; break; }
        }
        count++;
    }

    LOG_INFO("Keymap: %s: %d bindings", path.c_str(), count);
    return count;
}

void Keymap::set(int code, InputButton action) {
    // Override if exists
    for (auto &b : bindings_) {
        if (b.code == code) {
            b.action = action;
            b.sourceFile = "user";
            return;
        }
    }
    bindings_.push_back({code, action, "user"});
}

InputButton Keymap::lookup(int evdevCode) const {
    for (const auto &b : bindings_) {
        if (b.code == evdevCode) return b.action;
    }
    return InputButton::None;
}

bool Keymap::saveUserMap(const std::string &path) const {
    // Ensure parent dir exists
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) {
        std::error_code error;
        std::filesystem::create_directories(path.substr(0, slash), error);
        if (error) return false;
    }

    std::string temporary = path + ".new";
    int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    close(fd);
    std::ofstream f(temporary, std::ios::trunc);
    if (!f.is_open()) return false;

    f << "# LilSmokeOS user keymap override\n";
    f << "# This file is managed by LilSmokeUI Settings → Input.\n";
    f << "# Format: evdev <code> <action>\n";
    f << "# Actions: up down left right a b x y l1 r1 l2 r2 select start home vol_up vol_down power\n\n";

    int count = 0;
    for (const auto &b : bindings_) {
        if (b.sourceFile == "user" || b.sourceFile == "user.map") {
            f << "evdev " << b.code << " " << actionName(b.action) << "\n";
            count++;
        }
    }

    LOG_INFO("Keymap: saved %d user overrides to %s", count, path.c_str());
    f.flush();
    bool ok = f.good();
    f.close();
    fd = open(temporary.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    ok = ok && fd >= 0 && fsync(fd) == 0;
    if (fd >= 0) close(fd);
    if (ok) ok = rename(temporary.c_str(), path.c_str()) == 0;
    if (!ok) unlink(temporary.c_str());
    return ok;
}

}  // namespace lilsmoke
