// SPDX-License-Identifier: MPL-2.0
#include "TreeFrogCatalog.h"
#include "AppsProtocol.h"

#include <algorithm>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace lilsmoke {
namespace {
struct File {
    int fd;
    explicit File(int value) : fd(value) {}
    ~File() { if (fd >= 0) close(fd); }
    File(const File &) = delete;
    File &operator=(const File &) = delete;
};

bool plain(const std::string &value, size_t limit, bool multiline = false) {
    return !value.empty() && value.size() <= limit &&
        std::none_of(value.begin(), value.end(), [multiline](unsigned char c) {
            return (c < 32 && !(multiline && (c == '\n' || c == '\r' || c == '\t'))) || c == 127;
        });
}

bool relativePath(const std::string &path) {
    if (!plain(path, TreeFrogCatalog::MaxPathBytes) || path.front() == '/' ||
        path.find_first_of("\\:") != std::string::npos) return false;
    size_t start = 0;
    while (start < path.size()) {
        const size_t end = path.find('/', start);
        const auto part = path.substr(start, end == std::string::npos ? end : end - start);
        if (part.empty() || part == ".." || (part == "." && start != 0)) return false;
        if (end == std::string::npos) return part != ".";
        start = end + 1;
    }
    return false;
}

std::string stringValue(json_object *value, size_t limit, bool multiline = false) {
    if (!value || !json_object_is_type(value, json_type_string))
        throw std::runtime_error("Expected string");
    std::string text(json_object_get_string(value), json_object_get_string_len(value));
    if (!plain(text, limit, multiline)) throw std::runtime_error("Invalid string");
    return text;
}

std::string pathValue(json_object *value) {
    const auto path = stringValue(value, TreeFrogCatalog::MaxPathBytes);
    if (!relativePath(path)) throw std::runtime_error("Invalid relative path");
    return path;
}

void require(bool valid, const char *message) {
    if (!valid) throw std::runtime_error(message);
}

// Walk from a pinned directory descriptor; neither intermediate nor final links
// may redirect metadata or file checks outside the original release folder.
bool regularFile(int directory, const std::string &path) {
    if (!relativePath(path)) return false;
    File current(dup(directory));
    size_t start = 0;
    for (;;) {
        const size_t end = path.find('/', start);
        const auto part = path.substr(start, end == std::string::npos ? end : end - start);
        if (end == std::string::npos) {
            struct stat st{};
            return current.fd >= 0 && !fstatat(current.fd, part.c_str(), &st, AT_SYMLINK_NOFOLLOW) && S_ISREG(st.st_mode);
        }
        const int next = openat(current.fd, part.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (next < 0) return false;
        close(current.fd);
        current.fd = next;
        start = end + 1;
    }
}

std::string readConfig(int directory) {
    File file(openat(directory, "config.json", O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    struct stat st{};
    if (file.fd < 0 || fstat(file.fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        static_cast<uint64_t>(st.st_size) > TreeFrogCatalog::MaxJsonBytes)
        throw std::runtime_error("Invalid config file");
    std::string text;
    char buffer[4096];
    for (;;) {
        const auto count = read(file.fd, buffer, sizeof(buffer));
        if (!count) return text;
        if (count < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("Cannot read config");
        }
        if (text.size() + static_cast<size_t>(count) > TreeFrogCatalog::MaxJsonBytes)
            throw std::runtime_error("Config exceeds budget");
        text.append(buffer, static_cast<size_t>(count));
    }
}

std::optional<TreeFrogPort> load(int root, const std::string &rootPath, const std::string &id,
                               std::string *error = nullptr) {
    if (!TreeFrogCatalog::validId(id)) return std::nullopt;
    File directory(openat(root, id.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (directory.fd < 0) {
        if (error) *error = "Port directory unavailable or unsafe";
        return std::nullopt;
    }
    try {
        auto parsed = TreeFrogCatalog::parse(readConfig(directory.fd), id, error);
        if (!parsed) return std::nullopt;
        auto port = std::move(*parsed);
        port.directory = rootPath + "/" + id;
        port.requiredFilesSatisfied = port.requiredFileSets.empty();
        for (const auto &set : port.requiredFileSets) {
            std::vector<std::string> missing;
            for (const auto &path : set) {
                if (!regularFile(directory.fd, path)) missing.push_back(path);
            }
            if (missing.empty()) port.requiredFilesSatisfied = true;
            port.missingFileSets.push_back(std::move(missing));
        }
        if (regularFile(directory.fd, "boxart.png")) port.boxartPath = port.directory + "/boxart.png";
        port.hasRunScript = regularFile(directory.fd, "run.sh");
        port.hasBinary = regularFile(directory.fd, port.binary);
        port.hasContent = port.content.empty() || regularFile(directory.fd, port.content);
        port.filesChecked = true;
        if (!port.hasRunScript) port.reason = "Original run.sh is missing";
        else if (!port.hasBinary) port.reason = "Launch binary is missing";
        else if (!port.requiredFilesSatisfied || !port.hasContent)
            port.reason = "Required game data is missing";
        return port;
    } catch (const std::runtime_error &failure) {
        if (error) *error = failure.what();
        return std::nullopt;
    }
}

bool validRoot(const std::string &path) {
    return plain(path, TreeFrogCatalog::MaxPathBytes) && path.front() == '/';
}
} // namespace

bool TreeFrogCatalog::validId(const std::string &id) {
    const auto alnum = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    };
    return !id.empty() && id.size() <= MaxIdBytes && alnum(id.front()) &&
        std::all_of(id.begin(), id.end(), [&](unsigned char c) { return alnum(c) || c == '_' || c == '-'; });
}

bool TreeFrogCatalog::validRelativePath(const std::string &path) { return relativePath(path); }

std::optional<TreeFrogPort> TreeFrogCatalog::parse(const std::string &text,
                                                const std::string &expectedId,
                                                std::string *error) {
    if (error) error->clear();
    try {
        require(validId(expectedId), "Invalid port folder ID");
        require(text.size() <= MaxJsonBytes && text.find('\0') == std::string::npos, "Invalid config size or NUL byte");
        const auto json = parseAppJson(text);
        require(json_object_is_type(json.get(), json_type_object), "Config must be an object");
        auto version = appField(json.get(), "schema_version");
        require(version && json_object_is_type(version, json_type_int) && json_object_get_int64(version) == 1,
                "Unsupported schema_version");
        TreeFrogPort port;
        port.id = stringValue(appField(json.get(), "id"), MaxIdBytes);
        require(port.id == expectedId, "Config ID does not match port folder");
        port.name = stringValue(appField(json.get(), "name"), MaxNameBytes);
        auto launch = appField(json.get(), "launch");
        require(launch && json_object_is_type(launch, json_type_object), "Invalid launch object");
        port.launchType = stringValue(appField(launch, "type"), 16);
        require(port.launchType == "native" || port.launchType == "libretro", "Unsupported launch type");
        port.binary = pathValue(appField(launch, "binary"));
        json_object *value = nullptr;
        if (json_object_object_get_ex(launch, "content", &value)) port.content = pathValue(value);
        if (json_object_object_get_ex(launch, "args", &value)) {
            require(value && json_object_is_type(value, json_type_array) &&
                json_object_array_length(value) <= MaxEntries, "Invalid launch args");
            for (size_t i = 0; i < json_object_array_length(value); ++i) {
                auto arg = json_object_array_get_idx(value, i);
                require(arg && json_object_is_type(arg, json_type_string), "Invalid launch argument");
                // Empty argv strings are valid; control bytes and unbounded strings are not.
                port.args.push_back(json_object_get_string_len(arg) ? stringValue(arg, MaxPathBytes) : "");
            }
        }
        auto sets = appField(json.get(), "required_file_sets");
        require(sets && json_object_is_type(sets, json_type_array) &&
            json_object_array_length(sets) <= MaxEntries, "Invalid required_file_sets");
        size_t paths = 0;
        for (size_t i = 0; i < json_object_array_length(sets); ++i) {
            auto set = json_object_array_get_idx(sets, i);
            require(set && json_object_is_type(set, json_type_array) && json_object_array_length(set) &&
                json_object_array_length(set) <= MaxEntries - paths, "Invalid required file alternative");
            std::vector<std::string> files;
            for (size_t j = 0; j < json_object_array_length(set); ++j) {
                files.push_back(pathValue(json_object_array_get_idx(set, j)));
                ++paths;
            }
            port.requiredFileSets.push_back(std::move(files));
        }
        if (json_object_object_get_ex(json.get(), "missingdata", &value)) {
            require(value && json_object_is_type(value, json_type_string), "Invalid missingdata text");
            if (json_object_get_string_len(value)) port.missingData = stringValue(value, MaxTextBytes, true);
        }
        return port;
    } catch (const std::runtime_error &failure) {
        if (error) *error = failure.what();
        return std::nullopt;
    }
}

std::optional<TreeFrogPort> TreeFrogCatalog::find(const std::string &id, const std::string &root,
                                               std::string *error) {
    if (error) error->clear();
    if (!validRoot(root) || !validId(id)) {
        if (error) *error = "Invalid root or port ID";
        return std::nullopt;
    }
    File directory(open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (directory.fd < 0) {
        if (error) *error = "Ports root unavailable or unsafe";
        return std::nullopt;
    }
    return load(directory.fd, root, id, error);
}

std::vector<TreeFrogPort> TreeFrogCatalog::scan(const std::string &root) {
    std::vector<TreeFrogPort> ports;
    if (!validRoot(root)) return ports;
    File directory(open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (directory.fd < 0) return ports;
    const int copy = dup(directory.fd);
    DIR *raw = copy < 0 ? nullptr : fdopendir(copy);
    if (!raw) { if (copy >= 0) close(copy); return ports; }
    const auto closeDirectory = [](DIR *dir) { closedir(dir); };
    std::unique_ptr<DIR, decltype(closeDirectory)> entries(raw, closeDirectory);
    // Bound enumeration as well as retained entries, including malformed folders.
    for (size_t visited = 0; visited < 4096 && ports.size() < MaxEntries; ++visited) {
        const auto entry = readdir(entries.get());
        if (!entry) break;
        if (auto port = load(directory.fd, root, entry->d_name)) ports.push_back(std::move(*port));
    }
    std::sort(ports.begin(), ports.end(), [](const auto &a, const auto &b) {
        return a.name == b.name ? a.id < b.id : a.name < b.name;
    });
    return ports;
}
} // namespace lilsmoke
