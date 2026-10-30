// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <elf.h>
#include "AppsProtocol.h"
#include "TreeFrogCatalog.h"
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace treefrog {
struct ApplicationPlan {
    std::string frontend, abi;
};
struct Fd {
    int value;
    explicit Fd(int fd) : value(fd) {}
    ~Fd() { if (value >= 0) close(value); }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
};
inline void need(bool valid, const char *message) {
    if (!valid) throw std::runtime_error(message);
}
inline std::string textFile(int root, const char *name) {
    Fd file(openat(root, name, O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
    struct stat st{};
    need(file.value >= 0 && !fstat(file.value, &st) && S_ISREG(st.st_mode) &&
         st.st_size >= 0 && st.st_size <= 65536, "Missing, unsafe or oversized application metadata");
    std::string result;
    char bytes[4096];
    for (;;) {
        auto count = read(file.value, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR) continue;
        need(count >= 0, "Cannot read application metadata");
        if (!count) break;
        need(result.size() + size_t(count) <= 65536, "Application metadata exceeds budget");
        result.append(bytes, size_t(count));
    }
    need(result.find('\0') == std::string::npos, "NUL in application metadata");
    return result;
}
inline std::string field(json_object *json, const char *key, bool empty = false) {
    auto value = lilsmoke::appField(json, key);
    need(value && json_object_is_type(value, json_type_string), "Missing or non-string adapter field");
    auto result = lilsmoke::appString(json, key);
    need((empty || !result.empty()) && result.size() <= 1024 &&
         std::none_of(result.begin(), result.end(), [](unsigned char c) { return c < 32 || c == 127; }),
         "Invalid adapter string");
    return result;
}
inline Fd *directory(int root, const char *name) {
    auto result = new Fd(openat(root, name, O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
    if (result->value < 0) { delete result; throw std::runtime_error("Unsafe or missing application directory"); }
    return result;
}
// FAT has no symlinks. Reject them (and special objects) on other filesystems
// too, so a game's writable legacy data tree cannot alias a sibling app.
inline void checkTree(int root, size_t &budget, unsigned depth = 0) {
    need(depth <= 64, "Application port tree is too deep");
    DIR *raw = fdopendir(dup(root));
    need(raw, "Cannot enumerate application port tree");
    std::unique_ptr<DIR, int(*)(DIR *)> entries(raw, closedir);
    for (;;) {
        errno = 0;
        auto entry = readdir(raw);
        if (!entry) { need(!errno, "Cannot read application port tree"); break; }
        std::string name(entry->d_name);
        if (name == "." || name == "..") continue;
        need(budget > 0, "Application port tree exceeds entry budget");
        --budget;
        struct stat st{};
        need(!fstatat(root, name.c_str(), &st, AT_SYMLINK_NOFOLLOW), "Cannot stat application port object");
        if (S_ISDIR(st.st_mode)) {
            std::unique_ptr<Fd> child(directory(root, name.c_str()));
            checkTree(child->value, budget, depth + 1);
        } else need(S_ISREG(st.st_mode) && st.st_nlink == 1, "Application port tree contains a link or special object");
    }
}
inline std::string elfAbi(const std::string &path, bool executable) {
    Fd fd(open(path.c_str(), O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
    struct stat st{};
    need(fd.value >= 0 && !fstat(fd.value, &st) && S_ISREG(st.st_mode) && st.st_size <= 64*1024*1024,
         "Missing or oversized ELF runtime object");
    const auto readAt = [&](void *out, size_t count, uint64_t at) {
        need(at <= uint64_t(st.st_size) && count <= uint64_t(st.st_size)-at &&
             pread(fd.value, out, count, off_t(at)) == ssize_t(count), "Truncated ELF runtime object");
    };
    Elf32_Ehdr eh{};
    readAt(&eh, sizeof(eh), 0);
    need(!memcmp(eh.e_ident, "\177ELF\1\1\1", 7) && eh.e_machine == EM_MIPS &&
         (eh.e_type == ET_EXEC || eh.e_type == ET_DYN) && eh.e_phentsize == sizeof(Elf32_Phdr) &&
         eh.e_phnum > 0 && eh.e_phnum <= 128, "Runtime must be ELF32 little-endian MIPS");
    const unsigned arch = eh.e_flags >> 28;
    need(!(eh.e_flags & 0x0620u) && (eh.e_flags & 0xf000u) == 0x1000u &&
         (arch == 0 || arch == 1 || arch == 5 || arch == 7), "Runtime needs the MIPS32/o32 ABI");
    std::vector<Elf32_Phdr> ph(eh.e_phnum);
    readAt(ph.data(), ph.size()*sizeof(ph[0]), eh.e_phoff);
    std::string interpreter;
    std::vector<Elf32_Dyn> dynamic;
    for (const auto &p : ph) {
        if (p.p_type == PT_INTERP) {
            need(p.p_filesz > 1 && p.p_filesz <= 128, "Invalid ELF interpreter");
            interpreter.resize(p.p_filesz);
            readAt(interpreter.data(), interpreter.size(), p.p_offset);
            need(interpreter.back() == '\0', "Unterminated ELF interpreter");
            interpreter.pop_back();
        }
        if (p.p_type == PT_DYNAMIC) {
            need(p.p_filesz <= 65536 && p.p_filesz % sizeof(Elf32_Dyn) == 0, "Invalid ELF dynamic table");
            dynamic.resize(p.p_filesz/sizeof(Elf32_Dyn));
            readAt(dynamic.data(), p.p_filesz, p.p_offset);
        }
    }
    if (executable) {
        if (interpreter == "/lib/ld.so.1") return "glibc";
        if (interpreter == "/lib/ld-musl-mipsel.so.1") return "musl";
        throw std::runtime_error("Independent frontend needs a recognized dynamic libc interpreter");
    }
    uint64_t address = 0, size = 0;
    for (const auto &d : dynamic) {
        if (d.d_tag == DT_NULL) break;
        if (d.d_tag == DT_STRTAB) address = d.d_un.d_ptr;
        if (d.d_tag == DT_STRSZ) size = d.d_un.d_val;
    }
    need(size && size <= 1024*1024, "Core libc ABI cannot be established from ELF dynamic strings");
    std::string strings;
    for (const auto &p : ph) if (p.p_type == PT_LOAD && address >= p.p_vaddr &&
        address-p.p_vaddr <= p.p_filesz && size <= p.p_filesz-(address-p.p_vaddr)) {
        strings.resize(size);
        readAt(strings.data(), strings.size(), p.p_offset+(address-p.p_vaddr));
        break;
    }
    need(strings.size() == size, "Invalid core ELF string table mapping");
    bool glibc = false, musl = false;
    for (const auto &d : dynamic) {
        if (d.d_tag == DT_NULL) break;
        if (d.d_tag != DT_NEEDED) continue;
        const size_t start = d.d_un.d_val;
        need(start < strings.size() && strings.find('\0', start) != std::string::npos, "Invalid needed SONAME");
        const std::string name(strings.c_str()+start);
        glibc |= name == "libc.so.6";
        musl |= name == "libc.so" || name == "libc.musl-mipsel.so.1";
    }
    need(glibc != musl, "Unknown or mixed core libc ABI; manual adaptation required");
    return glibc ? "glibc" : "musl";
}
inline lilsmoke::TreeFrogPort application(const std::string &path, ApplicationPlan *plan = nullptr, bool bound = false) {
    constexpr const char *base = "/storage/independent-apps/";
    std::unique_ptr<Fd> app;
    if (bound) {
        need(path == "/app", "Invalid bound application path");
        app.reset(directory(AT_FDCWD, "/app"));
    } else {
        need(path.compare(0, strlen(base), base) == 0, "Application must be below /storage/independent-apps/");
        const auto name = path.substr(strlen(base));
        need(!name.empty() && name.size() <= 255 && name != "." && name != ".." &&
             name.find_first_of("/\\\n\r\t") == std::string::npos && name.front() != '.', "Invalid installed application directory");
        Fd storage(open("/storage", O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
        std::unique_ptr<Fd> apps(directory(storage.value, "independent-apps"));
        app.reset(directory(apps->value, name.c_str()));
    }
    const auto meta = lilsmoke::parseAppJson(textFile(app->value, "meta.json"));
    const auto adapter = lilsmoke::parseAppJson(textFile(app->value, "adapter.json"));
    need(json_object_is_type(meta.get(), json_type_object) && json_object_is_type(adapter.get(), json_type_object),
         "Application metadata must be objects");
    auto schema = lilsmoke::appField(adapter.get(), "schema");
    auto recompiled = lilsmoke::appField(adapter.get(), "binary_recompiled");
    need(schema && json_object_is_type(schema, json_type_int) && json_object_get_int64(schema) == 1,
         "Unsupported converted adapter schema");
    need(field(adapter.get(), "kind") == "treefrog-converted" &&
         field(adapter.get(), "port_root") == "runtime/port" &&
         field(adapter.get(), "requires_runtime") == "treefrog-compat-0.2" &&
         recompiled && json_object_is_type(recompiled, json_type_boolean) && !json_object_get_boolean(recompiled),
         "Unsupported converted adapter contract");
    const auto id = field(adapter.get(), "port_id");
    need(lilsmoke::TreeFrogCatalog::validId(id), "Invalid converted port ID");
    need(field(meta.get(), "id") == "org.lilsmoke.treefrog." + id, "Application ID does not match converted port");
    field(meta.get(), "name");
    field(meta.get(), "version");
    // Do not impose a display-name/folder-name match: installed apps may be renamed.
    DIR *top = fdopendir(dup(app->value));
    need(top, "Cannot enumerate application launchers");
    std::unique_ptr<DIR, int(*)(DIR *)> topEntries(top, closedir);
    unsigned scripts = 0, topBudget = 4096;
    for (;;) {
        errno = 0;
        auto entry = readdir(top);
        if (!entry) { need(!errno, "Cannot read application root"); break; }
        need(topBudget-- > 0, "Application root exceeds entry budget");
        const std::string name(entry->d_name);
        if (name.size() >= 3 && name[name.size()-3] == '.' &&
            (name[name.size()-2] == 's' || name[name.size()-2] == 'S') &&
            (name.back() == 'h' || name.back() == 'H')) {
            textFile(app->value, name.c_str());
            ++scripts;
        }
    }
    need(scripts == 1, "Application requires exactly one top-level shell launcher");
    std::unique_ptr<Fd> runtime(directory(app->value, "runtime"));
    std::unique_ptr<Fd> portRoot(directory(runtime->value, "port"));
    size_t budget = 65536;
    checkTree(runtime->value, budget);
    std::string error;
    auto parsed = lilsmoke::TreeFrogCatalog::parse(textFile(portRoot->value, "config.json"), id, &error);
    if (!parsed) throw std::runtime_error(error);
    auto port = *parsed;
    need(field(adapter.get(), "profile") == port.launchType && field(adapter.get(), "binary") == port.binary &&
         field(adapter.get(), "content", true) == port.content, "Adapter does not match original config.json");
    // The upstream ID is not the fixed directory name. Check the original data
    // sets against this tree, without ever registering it in the catalogue.
    const auto regular = [&](const std::string &relative) {
        Fd file(openat(portRoot->value, relative.c_str(), O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
        struct stat st{};
        return file.value >= 0 && !fstat(file.value, &st) && S_ISREG(st.st_mode);
    };
    need(regular("run.sh") && regular(port.binary) && (port.content.empty() || regular(port.content)),
         "Original run.sh, engine or content is missing");
    bool data = port.requiredFileSets.empty();
    for (const auto &set : port.requiredFileSets)
        if (std::all_of(set.begin(), set.end(), regular)) data = true;
    need(data, "Original required game data is missing");
    port.directory = path + "/runtime/port";
    if(port.launchType == "native" && plan) plan->abi=elfAbi(port.directory+'/'+port.binary,true);
    if (port.launchType == "libretro") {
        auto independent = lilsmoke::appField(adapter.get(), "independent_libretro");
        need(independent && json_object_is_type(independent, json_type_boolean) && json_object_get_boolean(independent),
             "Converted Libretro app needs an independent frontend, not an upstream shell launcher");
        const auto frontend = field(adapter.get(), "frontend");
        const auto abi = field(adapter.get(), "runtime_profile");
        need(frontend == "runtime/frontend/lilretro" && (abi == "glibc" || abi == "musl"),
             "Unsupported independent frontend path or libc profile");
        need(elfAbi(port.directory+'/'+port.binary, false) == abi && elfAbi(path+'/'+frontend, true) == abi,
             "Independent frontend/core libc ABI mismatch; never load a glibc core into musl");
        const auto policy = lilsmoke::parseAppJson(textFile(runtime->value, "launch.json"));
        need(json_object_is_type(policy.get(), json_type_object), "Runtime launch policy must be an object");
        const auto save = field(policy.get(), "save_directory");
        need(lilsmoke::TreeFrogCatalog::validRelativePath(save), "Runtime save path must stay in the application");
        if (lilsmoke::appField(policy.get(), "system_directory"))
            need(lilsmoke::TreeFrogCatalog::validRelativePath(field(policy.get(), "system_directory")),
                 "Runtime system path must stay in the application");
        if (plan) { plan->frontend = frontend; plan->abi = abi; }
    }
    port.filesChecked = port.hasRunScript = port.hasBinary = port.hasContent = port.requiredFilesSatisfied = true;
    return port;
}
} // namespace treefrog
