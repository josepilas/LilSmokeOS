#include <archive.h>
#include <archive_entry.h>
#include <libfdt.h>
#include <sodium.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <dirent.h>
#include <grp.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;
using Values = std::map<std::string, std::string>;
constexpr size_t ArchiveLimit = 2 * 1024 * 1024;
constexpr size_t ExpandedLimit = 1024 * 1024;

[[noreturn]] static void fail(const std::string &message) { throw std::runtime_error(message); }
static void require(bool condition, const std::string &message) { if (!condition) fail(message); }
static fs::path setting(const char *key, const char *fallback) {
    const char *value = std::getenv(key); return value && *value ? value : fallback;
}
struct Fd {
    int value;
    explicit Fd(int n) : value(n) { require(n >= 0, "cannot open protected file"); }
    ~Fd() { close(value); }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
};
static Bytes readFile(const fs::path &path, size_t limit) {
    Fd fd(open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    struct stat st{};
    require(fstat(fd.value, &st) == 0 && S_ISREG(st.st_mode) &&
            st.st_size >= 0 && static_cast<uint64_t>(st.st_size) <= limit, "file type/size rejected");
    Bytes result(static_cast<size_t>(st.st_size));
    size_t offset = 0;
    while (offset < result.size()) {
        ssize_t n = read(fd.value, result.data() + offset, result.size() - offset);
        if (n < 0 && errno == EINTR) continue;
        require(n > 0, "short file read");
        offset += static_cast<size_t>(n);
    }
    unsigned char extra;
    require(read(fd.value, &extra, 1) == 0, "file changed during snapshot");
    return result;
}
static std::string asText(const Bytes &bytes) { return std::string(bytes.begin(), bytes.end()); }
static Bytes asBytes(const std::string &text) { return Bytes(text.begin(), text.end()); }
static std::string hash(const Bytes &bytes) {
    unsigned char digest[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(digest, bytes.data(), bytes.size());
    char hex[65]; sodium_bin2hex(hex, sizeof hex, digest, sizeof digest); return hex;
}
static bool token(const std::string &s, size_t limit = 80) {
    return !s.empty() && s.size() <= limit &&
           std::all_of(s.begin(), s.end(), [](unsigned char c) {
               return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
           }) && s != "." && s != "..";
}
static Values parse(const Bytes &bytes, bool manifest = false) {
    require(bytes.size() <= 16384, "configuration too large");
    std::string text = asText(bytes);
    require(std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return c == '\n' || c == '\r' || (c >= 32 && c <= 126);
    }), "configuration must be plain ASCII data");
    Values result;
    bool section = !manifest;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            require(manifest && !section && line == "[SystemPackage]", "invalid/duplicate section");
            section = true; continue;
        }
        size_t eq = line.find('=');
        require(section && eq != std::string::npos && eq > 0, "invalid config line");
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        require(token(key) && value.size() <= 256 && result.emplace(key, value).second,
                "duplicate/invalid config key");
    }
    require(section, "missing SystemPackage section"); return result;
}
static std::string get(const Values &values, const std::string &key) {
    auto found = values.find(key); require(found != values.end(), "missing " + key); return found->second;
}
static unsigned number(const std::string &text) {
    require(!text.empty() && text.size() <= 6 &&
            std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }),
            "invalid number");
    return static_cast<unsigned>(std::stoul(text));
}
static void directory(const fs::path &path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    require(!ec, "cannot create private directory");
    for (fs::path p = path; !p.empty() && p != p.root_path(); p = p.parent_path()) {
        struct stat st{};
        require(lstat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode),
                "directory contains a symlink");
    }
    require(chmod(path.c_str(), 0700) == 0, "cannot protect private directory");
}
static void syncDirectory(const fs::path &path) {
    Fd fd(open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    require(fsync(fd.value) == 0, "directory sync failed");
}
static void atomic(const fs::path &path, const Bytes &bytes) {
    fs::path temporary = path.string() + ".new";
    require(!fs::exists(temporary), "unfinished file requires recovery: " + temporary.string());
    Fd fd(open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    size_t offset = 0;
    while (offset < bytes.size()) {
        ssize_t n = write(fd.value, bytes.data() + offset, bytes.size() - offset);
        if (n < 0 && errno == EINTR) continue;
        require(n > 0, "write failed"); offset += static_cast<size_t>(n);
    }
    require(fsync(fd.value) == 0 && rename(temporary.c_str(), path.c_str()) == 0, "atomic commit failed");
    syncDirectory(path.parent_path());
}
struct Package {
    Bytes original;
    std::map<std::string, Bytes> files;
    Values manifest, config, policy;
    std::string digest, id, kind;
};
static void validateConfig(const Values &config, unsigned ram) {
    const std::map<std::string, std::set<std::string>> allowed = {
        {"DISPLAY_ROTATION", {"0", "90", "180", "270"}},
        {"DEVICE_MODE", {"handheld", "tv"}},
        {"TV_SAFE_AREA_PERCENT", {"0", "2", "3", "5", "7", "10"}},
        {"SHOW_BATTERY", {"off", "icon", "text"}},
        {"SCALING_MODE", {"integer", "stretch", "aspect"}},
        {"CPU_GOVERNOR", {"ondemand", "performance", "powersave"}},
        {"CPU_OVERCLOCK", {"stock"}},
        {"ZRAM_ENABLED", {"off", "on"}},
        {"ZRAM_SIZE_MB", {"24", "32", "48", "64"}},
        {"POINTER_SPEED", {"slow", "normal", "fast"}},
    };
    for (const auto &item : config) {
        auto found = allowed.find(item.first);
        require(found != allowed.end() && found->second.count(item.second), "unsupported setting: " + item.first);
        if (item.first == "ZRAM_SIZE_MB") require(number(item.second) <= ram / 2, "ZRAM exceeds RAM limit");
    }
}
static void validateDtb(const Bytes &bytes, const Package &p) {
    // libfdt expects an 8-byte-aligned base on MIPS. Copy into aligned storage.
    std::vector<uint64_t> aligned((bytes.size() + 7) / 8);
    std::memcpy(aligned.data(), bytes.data(), bytes.size());
    const void *dt = aligned.data();
    require(bytes.size() >= sizeof(fdt_header) && fdt_check_full(dt, bytes.size()) == 0 &&
            static_cast<size_t>(fdt_totalsize(dt)) == bytes.size(), "invalid/truncated DTB");
    if (p.kind == "reference") return;
    require(p.kind == "hardware", "userspace package cannot replace a DTB");
    require(get(p.policy, "HardwareValidated") == "yes", "hardware profile is unverified");
    require(get(p.manifest, "DTBinding") == get(p.policy, "DTBinding"), "DT binding mismatch");
    require(fdt_node_check_compatible(dt, 0, get(p.policy, "DTBCompatible").c_str()) == 0,
            "DTB root compatible does not match kernel");
    require(hash(bytes) == get(p.policy, "ApprovedDTBSHA256"), "DTB is not approved for this revision");
    int ac = fdt_address_cells(dt, 0), sc = fdt_size_cells(dt, 0), depth = 0;
    require((ac == 1 || ac == 2) && (sc == 1 || sc == 2), "unsupported memory cell layout");
    uint64_t maximum = static_cast<uint64_t>(number(get(p.policy, "RAMMB"))) * 1024 * 1024;
    bool memory = false;
    for (int node = fdt_next_node(dt, -1, &depth); node >= 0; node = fdt_next_node(dt, node, &depth)) {
        int length = 0;
        const char *type = static_cast<const char *>(fdt_getprop(dt, node, "device_type", &length));
        if (!type || length != 7 || std::memcmp(type, "memory", 7)) continue;
        memory = true;
        const auto *reg = static_cast<const fdt32_t *>(fdt_getprop(dt, node, "reg", &length));
        int cells = ac + sc;
        require(reg && length > 0 && length % (cells * 4) == 0, "invalid memory reg");
        for (int i = 0; i < length / 4; i += cells) {
            uint64_t start = 0, size = 0;
            for (int j = 0; j < ac; ++j) start = (start << 32) | fdt32_to_cpu(reg[i + j]);
            for (int j = 0; j < sc; ++j) size = (size << 32) | fdt32_to_cpu(reg[i + ac + j]);
            require(size > 0 && size <= maximum && start <= maximum - size, "memory exceeds console RAM");
        }
    }
    require(memory, "hardware DTB has no memory description");
}
using Members = std::map<std::string, Bytes>;
static Members decode(const Bytes &original) {
    Members files;
    archive *a = archive_read_new(); require(a, "archive allocation failed");
    struct Guard { archive *a; ~Guard() { archive_read_free(a); } } guard{a};
    archive_read_support_filter_none(a);
    archive_read_support_format_7zip(a);
    require(archive_read_open_memory(a, original.data(), original.size()) == ARCHIVE_OK, "cannot read 7z");
    const std::map<std::string, size_t> limits = {
        {"manifest.ini", 16384}, {"manifest.sig", 64}, {"config.conf", 8192}, {"dtb.bin", 512 * 1024},
    };
    archive_entry *entry = nullptr;
    size_t total = 0; int result;
    while ((result = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        const char *name = archive_entry_pathname(entry);
        require(name, "unnamed archive member");
        auto limit = limits.find(name);
        require(limit != limits.end() && !files.count(name) && files.size() < 4,
                "unknown/duplicate/unsafe member path");
        require(archive_entry_filetype(entry) == AE_IFREG && !archive_entry_symlink(entry) &&
                !archive_entry_hardlink(entry) && !archive_entry_is_encrypted(entry),
                "links, devices, directories and encryption are forbidden");
        la_int64_t size = archive_entry_size(entry);
        require(size >= 0 && static_cast<uint64_t>(size) <= limit->second, "archive member too large");
        Bytes bytes(static_cast<size_t>(size));
        size_t offset = 0;
        while (offset < bytes.size()) {
            la_ssize_t n = archive_read_data(a, bytes.data() + offset, bytes.size() - offset);
            require(n > 0, "7z data/CRC failure"); offset += static_cast<size_t>(n);
        }
        unsigned char extra;
        require(archive_read_data(a, &extra, 1) == 0, "member exceeds declared size");
        total += bytes.size(); require(total <= ExpandedLimit, "expanded archive too large");
        files.emplace(name, std::move(bytes));
    }
    require(result == ARCHIVE_EOF && archive_format(a) == ARCHIVE_FORMAT_7ZIP &&
            archive_read_has_encrypted_entries(a) <= 0,
            "invalid/encrypted archive: " + std::string(archive_error_string(a) ? archive_error_string(a) : "format mismatch"));
    return files;
}
static void append32(Bytes &bytes, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back((value >> shift) & 255);
}
static uint32_t take32(const Bytes &bytes, size_t &offset) {
    require(offset + 4 <= bytes.size(), "short decoder response");
    uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) value |= uint32_t(bytes[offset++]) << shift;
    return value;
}
static void isolateDecoder(int output, const fs::path &empty) {
    // Keep only the reply pipe: a compromised decoder must not inherit OS file handles.
    DIR *fds = opendir("/proc/self/fd");
    require(fds, "decoder requires procfs");
    int iterator = dirfd(fds);
    while (dirent *entry = readdir(fds)) {
        if (entry->d_name[0] == '.') continue;
        int fd = std::atoi(entry->d_name);
        if (fd != output && fd != iterator) close(fd);
    }
    closedir(fds);
    if (geteuid() == 0) {
        struct stat st{};
        require(lstat(empty.c_str(), &st) == 0 && S_ISDIR(st.st_mode) &&
                st.st_uid == 0 && !(st.st_mode & 0022), "unsafe/missing decoder jail");
        require(chdir(empty.c_str()) == 0 && chroot(".") == 0 && chdir("/") == 0,
                "cannot isolate decoder filesystem");
        require(setgroups(0, nullptr) == 0 && setgid(65534) == 0 && setuid(65534) == 0,
                "cannot drop decoder privileges");
    }
    require(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0, "cannot prevent privilege gain");
}
static Members decodeIsolated(const Bytes &original) {
    fs::path empty = setting("LILSMOKE_SYSPCK_EMPTY", "/usr/share/lilsmoke-syspck/empty");
    int pipes[2];
    require(pipe(pipes) == 0, "decoder pipe failed");
    Fd input(pipes[0]), output(pipes[1]);
    pid_t pid = fork();
    require(pid >= 0, "decoder fork failed");
    if (pid == 0) {
        Bytes reply;
        try {
            isolateDecoder(output.value, empty);
            Members files = decode(original);
            reply.push_back(0);
            append32(reply, static_cast<uint32_t>(files.size()));
            for (const auto &item : files) {
                append32(reply, static_cast<uint32_t>(item.first.size()));
                reply.insert(reply.end(), item.first.begin(), item.first.end());
                append32(reply, static_cast<uint32_t>(item.second.size()));
                reply.insert(reply.end(), item.second.begin(), item.second.end());
            }
        } catch (const std::exception &error) {
            reply = Bytes{1};
            std::string message(error.what());
            if (message.size() > 1024) message.resize(1024);
            reply.insert(reply.end(), message.begin(), message.end());
        }
        size_t offset = 0;
        while (offset < reply.size()) {
            ssize_t n = write(output.value, reply.data() + offset, reply.size() - offset);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) _exit(2);
            offset += static_cast<size_t>(n);
        }
        _exit(0);
    }
    close(output.value); output.value = -1;
    struct Child {
        pid_t pid;
        ~Child() {
            if (pid > 0) {
                kill(pid, SIGKILL);
                while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
            }
        }
    } child{pid};
    Bytes reply;
    unsigned char chunk[4096];
    for (;;) {
        ssize_t n = read(input.value, chunk, sizeof chunk);
        if (n < 0 && errno == EINTR) continue;
        require(n >= 0, "decoder pipe read failed");
        if (n == 0) break;
        require(reply.size() + static_cast<size_t>(n) <= ExpandedLimit + 4096,
                "decoder reply exceeds limit");
        reply.insert(reply.end(), chunk, chunk + n);
    }
    int status = 0;
    pid_t waited;
    do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
    require(waited == pid, "decoder wait failed");
    child.pid = 0;
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0 && !reply.empty(),
            "decoder failed or exceeded resource limit");
    if (reply[0] == 1) fail(std::string(reply.begin() + 1, reply.end()));
    require(reply[0] == 0, "invalid decoder response");
    size_t offset = 1;
    uint32_t count = take32(reply, offset);
    require(count >= 3 && count <= 4, "required package member missing");
    const std::map<std::string, size_t> limits = {
        {"manifest.ini", 16384}, {"manifest.sig", 64}, {"config.conf", 8192}, {"dtb.bin", 512 * 1024},
    };
    Members files;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t length = take32(reply, offset);
        require(length <= 32 && offset + length <= reply.size(), "bad decoder member name");
        std::string name(reply.begin() + offset, reply.begin() + offset + length); offset += length;
        auto limit = limits.find(name);
        require(limit != limits.end() && !files.count(name), "bad decoder member identity");
        length = take32(reply, offset);
        require(length <= limit->second && offset + length <= reply.size(), "bad decoder member size");
        files.emplace(name, Bytes(reply.begin() + offset, reply.begin() + offset + length)); offset += length;
    }
    require(offset == reply.size(), "unexpected decoder response data");
    return files;
}
static Package load(const fs::path &path) {
    require(path.extension() == ".syspck", "expected .syspck extension");
    Package p;
    p.original = readFile(path, ArchiveLimit); p.digest = hash(p.original);
    const unsigned char magic[] = {0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c};
    require(p.original.size() >= 32 && std::memcmp(p.original.data(), magic, sizeof magic) == 0,
            "not a renamed 7z archive");
    p.files = decodeIsolated(p.original);
    require(p.files.count("manifest.ini") && p.files.count("manifest.sig") &&
            p.files.count("config.conf"), "required package member missing");
    p.manifest = parse(p.files.at("manifest.ini"), true);
    const std::set<std::string> keys = {"FormatVersion", "PackageID", "Version", "Board",
        "HardwareRevision", "SoC", "Architecture", "ABI", "Libc", "KernelID", "DTBinding",
        "Kind", "RequiredRAMMB", "KeyID", "ConfigSHA256", "DTBSHA256"};
    for (const auto &item : p.manifest) require(keys.count(item.first), "unknown manifest key");
    p.id = get(p.manifest, "PackageID"); p.kind = get(p.manifest, "Kind");
    std::string key = get(p.manifest, "KeyID");
    require(token(p.id) && token(get(p.manifest, "Version")) && token(key, 64), "invalid identity");
    require(p.files.at("manifest.sig").size() == crypto_sign_BYTES, "invalid signature size");
    Bytes pub = readFile(setting("LILSMOKE_SYSPCK_KEYS", "/etc/lilsmoke/syspck-keys") / (key + ".pub"), 32);
    require(pub.size() == crypto_sign_PUBLICKEYBYTES &&
            crypto_sign_verify_detached(p.files.at("manifest.sig").data(),
                p.files.at("manifest.ini").data(), p.files.at("manifest.ini").size(), pub.data()) == 0,
            "untrusted/invalid signature");
    p.policy = parse(readFile(setting("LILSMOKE_SYSPCK_POLICY", "/etc/lilsmoke/syspck-policy.conf"), 16384));
    require(get(p.policy, "PolicyVersion") == "1" && get(p.manifest, "FormatVersion") == "1", "unsupported version");
    for (const char *field : {"Board", "HardwareRevision", "SoC", "Architecture", "ABI", "Libc", "KernelID"})
        require(get(p.manifest, field) == get(p.policy, field), std::string("incompatible ") + field);
    require(p.kind == "userspace" || p.kind == "hardware" || p.kind == "reference", "invalid package kind");
    unsigned ram = number(get(p.policy, "RAMMB")), required = number(get(p.manifest, "RequiredRAMMB"));
    require((ram == 128 || ram == 256) && required >= 16 && required <= ram, "RAM requirement rejected");
    require(hash(p.files.at("config.conf")) == get(p.manifest, "ConfigSHA256"), "config hash mismatch");
    p.config = parse(p.files.at("config.conf")); validateConfig(p.config, ram);
    if (p.files.count("dtb.bin")) {
        require(hash(p.files.at("dtb.bin")) == get(p.manifest, "DTBSHA256"), "DTB hash mismatch");
        validateDtb(p.files.at("dtb.bin"), p);
    } else {
        require(p.kind == "userspace" && !p.manifest.count("DTBSHA256"), "hardware/reference DTB required");
        require(get(p.manifest, "DTBinding") == "none", "userspace DTBinding must be none");
    }
    return p;
}
int main(int argc, char **argv) {
    try {
        umask(0077);
        require(sodium_init() >= 0, "crypto initialization failed");
        rlimit memory{48 * 1024 * 1024, 48 * 1024 * 1024}, cpu{30, 30};
        require(setrlimit(RLIMIT_AS, &memory) == 0 && setrlimit(RLIMIT_CPU, &cpu) == 0, "cannot apply resource limits");
        require(argc >= 2, "usage: lilsmoke-syspck check|stage|apply|activate-dtb FILE.syspck; rollback|recover|boot-select");
        std::string command = argv[1];
        require(command == "check" || command == "stage" || command == "apply" ||
                command == "activate-dtb" || command == "boot-select" || command == "rollback" || command == "recover", "unknown command");
        fs::path persist = setting("LILSMOKE_SYSPCK_PERSIST", "/persist");
        fs::path state = persist / "syspck", conf = persist / "config/lilsmoke.conf";
        if (command == "check") {
            require(argc == 3, "package path required");
            Package p = load(argv[2]);
            std::cout << "VALID " << p.id << " kind=" << p.kind << " sha256=" << p.digest << "\n";
            return 0;
        }
        require(geteuid() == 0, "installation requires root");
        directory(state);
        Fd lock(open((state / "lock").c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600));
        require(flock(lock.value, LOCK_EX | LOCK_NB) == 0, "package operation already running");
        if (command == "rollback" || command == "recover") {
            require(argc == 2, "unexpected argument");
            if (command == "recover" && !fs::exists(state / "pending")) return 0;
            Bytes previous = readFile(state / "previous.conf", 128 * 1024);
            directory(conf.parent_path());
            // Only these internally generated temporary files are recoverable.
            fs::remove(conf.string() + ".new");
            atomic(conf, previous);
            fs::remove(state / "pending"); fs::remove(state / "active");
            syncDirectory(state);
            std::cout << "ROLLED BACK userspace configuration\n"; return 0;
        }
        fs::path packagePath;
        if (command == "boot-select") {
            require(argc == 2, "boot-select accepts no package path");
            require(!fs::exists(state / "pending"), "interrupted install requires recover");
            packagePath = setting("LILSMOKE_SYSPCK_LILOS", "/lilos") / "main.syspck";
            struct stat st{};
            if (lstat(packagePath.c_str(), &st) < 0 && errno == ENOENT) {
                std::cout << "NO SELECTION: using existing board configuration\n";
                return 0;
            }
        } else {
            require(argc == 3, "package path required");
            packagePath = argv[2];
        }
        Package p = load(packagePath);
        if (command == "boot-select") {
            require(p.kind == "userspace",
                    "main.syspck DTB activation blocked: no verified pre-kernel boot/rollback adapter");
            fs::path selected = state / "boot-selected";
            if (fs::exists(selected) &&
                asText(readFile(selected, 128)) == p.digest + "\n") {
                std::cout << "UNCHANGED main.syspck; preserving user settings\n";
                return 0;
            }
            fs::path temporary = selected.string() + ".new";
            require(!fs::exists(temporary) && !fs::is_symlink(temporary),
                    "unfinished boot selection marker requires inspection");
            // Finish marker bookkeeping if power failed after the active commit.
            if (fs::exists(state / "active") &&
                asText(readFile(state / "active", 256)) == p.id + "\n" + p.digest + "\n") {
                atomic(selected, asBytes(p.digest + "\n"));
                std::cout << "RECORDED committed main.syspck without reapplying\n";
                return 0;
            }
            // Initialize the full immutable board default, never a sparse profile.
            directory(conf.parent_path());
            require(!fs::is_symlink(conf), "configuration symlink rejected");
            if (!fs::exists(conf))
                atomic(conf, readFile(setting("LILSMOKE_SYSPCK_DEFAULT",
                    "/etc/lilsmoke/lilsmoke.conf.default"), 128 * 1024));
        }
        if (command == "activate-dtb") fail("DTB activation blocked: no verified HC16xx production boot/rollback adapter");
        directory(state / "packages");
        fs::path staged = state / "packages" / (p.digest + ".syspck");
        if (fs::exists(staged)) require(hash(readFile(staged, ArchiveLimit)) == p.digest, "staged file changed");
        else atomic(staged, p.original);
        if (command == "stage") {
            std::cout << "STAGED " << staged.string() << "\n"; return 0;
        }
        require(p.kind == "userspace", "hardware/reference packages cannot be applied as userspace settings");
        require(!fs::exists(state / "pending"), "interrupted install requires recover");
        directory(conf.parent_path());
        Bytes previous = readFile(conf, 128 * 1024);
        for (const auto &path : {state / "previous.conf", state / "pending", state / "active", conf}) {
            fs::path temporary = path.string() + ".new";
            require(!fs::exists(temporary) && !fs::is_symlink(temporary),
                    "unfinished transaction requires inspection: " + temporary.string());
        }
        atomic(state / "previous.conf", previous);
        atomic(state / "pending", asBytes(p.digest + "\n"));
        std::string merged = asText(previous);
        if (!merged.empty() && merged.back() != '\n') merged += '\n';
        merged += "# System Package " + p.id + "\n";
        for (const auto &item : p.config) merged += item.first + "=" + item.second + "\n";
        atomic(conf, asBytes(merged));
        atomic(state / "active", asBytes(p.id + "\n" + p.digest + "\n"));
        fs::remove(state / "pending"); syncDirectory(state);
        if (command == "boot-select")
            atomic(state / "boot-selected", asBytes(p.digest + "\n"));
        std::cout << "APPLIED " << p.id << "; restart UI/reboot for settings\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "lilsmoke-syspck: " << error.what() << "\n"; return 1;
    }
}
