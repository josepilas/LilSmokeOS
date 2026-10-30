#include "asset_pack.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>

namespace portsmoke::sm64 {
namespace {
constexpr std::uint32_t maxPackBytes = 16 * 1024 * 1024;
constexpr std::uint32_t maxBlockBytes = 4 * 1024 * 1024;
constexpr std::uint32_t headerBytes = 64;
constexpr char magic[] = "PSM64DAT";

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

std::uint32_t be32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    require(offset <= data.size() && data.size() - offset >= 4, "truncated MIO0 header");
    return (std::uint32_t(data[offset]) << 24) | (std::uint32_t(data[offset + 1]) << 16) |
           (std::uint32_t(data[offset + 2]) << 8) | data[offset + 3];
}

std::uint32_t rotate(std::uint32_t value, unsigned bits) {
    return (value << bits) | (value >> (32 - bits));
}

// SHA-1 identifies the exact upstream ROM; it is not used as a security signature.
class Sha1 {
public:
    void update(const std::uint8_t *data, std::size_t size) {
        bytes_ += size;
        while (size) {
            std::size_t take = std::min(size, block_.size() - used_);
            std::copy_n(data, take, block_.begin() + used_);
            used_ += take; data += take; size -= take;
            if (used_ == block_.size()) { process(); used_ = 0; }
        }
    }
    std::array<std::uint8_t, 20> finish() {
        auto bits = bytes_ * 8;
        const std::uint8_t pad = 0x80, zero = 0;
        update(&pad, 1);
        while (used_ != 56) update(&zero, 1);
        std::array<std::uint8_t, 8> length{};
        for (unsigned i = 0; i < 8; ++i) length[7 - i] = (bits >> (8 * i)) & 255;
        update(length.data(), length.size());
        std::array<std::uint8_t, 20> digest{};
        for (unsigned i = 0; i < 20; ++i) digest[i] = (state_[i / 4] >> (24 - 8 * (i % 4))) & 255;
        return digest;
    }
private:
    void process() {
        std::uint32_t words[80];
        for (unsigned i = 0; i < 16; ++i) {
            words[i] = (std::uint32_t(block_[i * 4]) << 24) | (std::uint32_t(block_[i * 4 + 1]) << 16) |
                       (std::uint32_t(block_[i * 4 + 2]) << 8) | block_[i * 4 + 3];
        }
        for (unsigned i = 16; i < 80; ++i) words[i] = rotate(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
        auto a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4];
        for (unsigned i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
            else { f = b ^ c ^ d; k = 0xca62c1d6; }
            auto next = rotate(a, 5) + f + e + k + words[i];
            e = d; d = c; c = rotate(b, 30); b = a; a = next;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d; state_[4] += e;
    }
    std::array<std::uint32_t, 5> state_{{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0}};
    std::array<std::uint8_t, 64> block_{};
    std::size_t used_ = 0;
    std::uint64_t bytes_ = 0;
};

std::string hex(const std::array<std::uint8_t, 20>& digest) {
    constexpr char digits[] = "0123456789abcdef";
    std::string value;
    for (auto byte : digest) { value += digits[byte >> 4]; value += digits[byte & 15]; }
    return value;
}

std::array<std::uint8_t, 20> unhex(const std::string& value) {
    require(value.size() == 40, "invalid digest length");
    std::array<std::uint8_t, 20> data{};
    for (unsigned i = 0; i < 40; ++i) {
        auto pos = std::string("0123456789abcdef").find(value[i]);
        require(pos != std::string::npos, "invalid digest");
        data[i / 2] |= static_cast<std::uint8_t>(pos << (i % 2 ? 0 : 4));
    }
    return data;
}

std::uint32_t get(std::FILE *stream, unsigned bytes) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) {
        int byte = std::fgetc(stream);
        require(byte != EOF, "truncated asset index");
        value |= std::uint32_t(byte) << (8 * i);
    }
    return value;
}

void write(std::FILE *stream, const void *data, std::size_t size) {
    require(std::fwrite(data, 1, size, stream) == size, "asset write failed (check free space)");
}

void put(std::FILE *stream, std::uint32_t value, unsigned bytes) {
    std::uint8_t data[4];
    for (unsigned i = 0; i < bytes; ++i) data[i] = (value >> (8 * i)) & 255;
    write(stream, data, bytes);
}

void readAt(int fd, std::uint32_t offset, void *destination, std::size_t bytes) {
    require(destination || !bytes, "null asset read destination");
    auto *out = static_cast<std::uint8_t *>(destination);
    while (bytes) {
        auto amount = ::pread(fd, out, bytes, offset);
        if (amount < 0 && errno == EINTR) continue;
        require(amount > 0, "truncated asset read");
        offset += amount; out += amount; bytes -= amount;
    }
}

std::shared_ptr<std::FILE> openRegular(const std::filesystem::path& path) {
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    require(fd >= 0, "cannot open regular asset file");
    struct stat info{};
    if (::fstat(fd, &info) || !S_ISREG(info.st_mode)) {
        ::close(fd); throw std::runtime_error("asset is not a regular file");
    }
    auto *file = ::fdopen(fd, "rb");
    if (!file) { ::close(fd); throw std::runtime_error("cannot open asset stream"); }
    return {file, std::fclose};
}

std::array<std::uint8_t, 20> fileDigest(std::FILE *file, std::uint32_t bytes) {
    Sha1 digest;
    std::uint32_t offset = 0;
    std::array<std::uint8_t, 65536> buffer;
    while (bytes) {
        auto amount = std::min<std::uint32_t>(bytes, buffer.size());
        readAt(::fileno(file), offset, buffer.data(), amount);
        digest.update(buffer.data(), amount);
        bytes -= amount;
        offset += amount;
    }
    return digest.finish();
}

bool safeName(const std::string& name) {
    if (name.empty() || name.size() > 255 || name.front() == '/' || name.back() == '/') return false;
    for (auto byte : name) if (static_cast<unsigned char>(byte) < 32 || byte == '\\' || byte == ':') return false;
    for (const auto& part : std::filesystem::path(name)) if (part == ".." || part == "." || part.empty()) return false;
    return name.find("//") == std::string::npos;
}

void noLinkComponents(const std::filesystem::path& path) {
    auto absolute = std::filesystem::absolute(path);
    std::filesystem::path current;
    for (const auto& part : absolute) {
        current /= part;
        require(!std::filesystem::is_symlink(current), "symlink in private data path");
    }
}

void syncDirectory(const std::filesystem::path& path) {
    int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    require(fd >= 0, "cannot open asset directory for sync");
    int result = ::fsync(fd), error = errno;
    if (result && error == EINVAL) ::sync();
    ::close(fd);
    require(result == 0 || error == EINVAL, "asset directory sync failed");
}

std::string processStart(pid_t pid) {
    auto id = std::to_string(pid);
    // Read the kernel leader record. QEMU user mode synthesizes /proc/self/stat
    // with a different start time than other processes observe for that PID.
    std::ifstream stream("/proc/" + id + "/task/" + id + "/stat");
    std::string line, field;
    require(bool(std::getline(stream, line)), "cannot identify lock process; /proc must be mounted");
    auto end = line.rfind(')');
    require(end != std::string::npos, "invalid process identity");
    std::istringstream fields(line.substr(end + 1));
    for (unsigned i = 3; i <= 22; ++i) require(bool(fields >> field), "truncated process identity");
    require(!field.empty() && field.find_first_not_of("0123456789") == std::string::npos, "invalid process start time");
    return field;
}

std::string bootIdentity() {
    std::ifstream stream("/proc/sys/kernel/random/boot_id");
    std::string value;
    if (std::getline(stream, value) && value.size() == 36 &&
        value.find_first_not_of("0123456789abcdef-") == std::string::npos) return value;
    // Missing boot-id only makes PID reuse across reboot conservative; never
    // steal a lock whose live PID/start time match.
    return "unknown";
}

class Lock {
public:
    explicit Lock(const std::filesystem::path& path) : path_(path) {
        const auto start = processStart(::getpid()), boot = bootIdentity();
        if (::mkdir(path.c_str(), 0700)) {
            require(errno == EEXIST, "cannot create extraction directory lock");
            recover(path, boot);
            require(::mkdir(path.c_str(), 0700) == 0, "another extraction acquired the lock");
        }
        fd_ = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        require(fd_ >= 0, "cannot open extraction directory lock");
        int owner = -1;
        try {
            owner = ::openat(fd_, "owner", O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0600);
            require(owner >= 0, "cannot create exclusive lock owner");
            owned_ = true;
            auto token = "SM64LOCK1 " + std::to_string(::getpid()) + " " + start + " " + boot + "\n";
            require(::write(owner, token.data(), token.size()) == static_cast<ssize_t>(token.size()) &&
                    ::fsync(owner) == 0, "cannot persist extraction lock owner");
            ::close(owner); owner = -1;
            syncDirectory(path); syncDirectory(path.parent_path());
        } catch (...) {
            if (owner >= 0) ::close(owner);
            release(); throw;
        }
    }
    ~Lock() { release(); }
private:
    // The guard lives inside the observed directory inode. Competing reapers
    // cannot remove a newly acquired lock through an old directory descriptor.
    static void recover(const std::filesystem::path& path, const std::string& boot) {
        int dir = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        require(dir >= 0, "unsafe extraction lock; preserved");
        int owner = -1;
        bool guarded = false;
        try {
            owner = ::openat(dir, "owner", O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
            struct stat info{};
            require(owner >= 0 && ::fstat(owner, &info) == 0 && S_ISREG(info.st_mode) &&
                    info.st_nlink == 1 && info.st_size > 0 && info.st_size <= 128,
                    "lock owner missing/unsafe; initialization or interruption requires inspection");
            std::string token(info.st_size, '\0');
            readAt(owner, 0, token.data(), token.size());
            ::close(owner); owner = -1;
            std::istringstream fields(token);
            std::string magic, start, oldBoot, extra;
            long long pid = 0;
            require(bool(fields >> magic >> pid >> start >> oldBoot) && !(fields >> extra) &&
                    magic == "SM64LOCK1" && pid > 1 && pid <= std::numeric_limits<pid_t>::max() &&
                    !start.empty() && start.find_first_not_of("0123456789") == std::string::npos &&
                    (oldBoot == "unknown" || (oldBoot.size() == 36 &&
                     oldBoot.find_first_not_of("0123456789abcdef-") == std::string::npos)),
                    "unrecognized extraction lock owner; preserved");
            bool alive = ::kill(static_cast<pid_t>(pid), 0) == 0;
            int error = errno;
            require(alive || error == ESRCH || error == EPERM, "cannot establish extraction owner liveness");
            bool sameBoot = boot == "unknown" || oldBoot == "unknown" || oldBoot == boot;
            if ((alive || error == EPERM) && sameBoot && processStart(pid) == start)
                throw std::runtime_error("another SM64 extraction is already running");
            // Never add a guard to a live owner's directory: its orderly
            // release must not be obstructed by a competing launch.
            require(::mkdirat(dir, "reap", 0700) == 0,
                    "lock recovery already running or interrupted; preserved");
            guarded = true;
            struct stat observed{}, current{};
            require(::fstat(dir, &observed) == 0 && ::lstat(path.c_str(), &current) == 0 &&
                    observed.st_dev == current.st_dev && observed.st_ino == current.st_ino,
                    "extraction lock path changed; preserved");
            for (const auto& item : std::filesystem::directory_iterator(path))
                require(item.path().filename() == "owner" || item.path().filename() == "reap",
                        "unexpected extraction lock contents; preserved");
            require(::unlinkat(dir, "owner", 0) == 0, "cannot remove stale lock owner");
            require(::unlinkat(dir, "reap", AT_REMOVEDIR) == 0, "cannot release lock recovery guard");
            guarded = false;
            // One attempt only. Another contender may have installed a guard,
            // in which case rmdir fails without deleting its/new owner's data.
            require(::rmdir(path.c_str()) == 0, "extraction lock changed during recovery; retry");
            ::close(dir);
            dir = -1;
            syncDirectory(path.parent_path());
        } catch (...) {
            if (owner >= 0) ::close(owner);
            if (guarded) ::unlinkat(dir, "reap", AT_REMOVEDIR);
            if (dir >= 0) ::close(dir);
            throw;
        }
    }
    void release() noexcept {
        if (fd_ < 0) return;
        struct stat observed{}, current{};
        bool same = ::fstat(fd_, &observed) == 0 && ::lstat(path_.c_str(), &current) == 0 &&
                    observed.st_dev == current.st_dev && observed.st_ino == current.st_ino;
        if (owned_) ::unlinkat(fd_, "owner", 0);
        ::close(fd_); fd_ = -1;
        if (same) ::rmdir(path_.c_str());
    }
    std::filesystem::path path_;
    int fd_ = -1;
    bool owned_ = false;
};

void recoverTemporary(const std::filesystem::path& path, const GameDefinition& definition) {
    struct stat info{};
    if (::lstat(path.c_str(), &info)) {
        require(errno == ENOENT, "cannot inspect asset staging file");
        return;
    }
    require(S_ISREG(info.st_mode) && info.st_nlink == 1 && info.st_size >= headerBytes &&
            info.st_size <= maxPackBytes, "unrecognized staging file; inspect it manually");
    auto file = openRegular(path);
    char signature[8];
    require(std::fread(signature, 1, 8, file.get()) == 8 &&
            std::memcmp(signature, magic, 8) == 0 && get(file.get(), 4) == 1 &&
            get(file.get(), 4) == definition.count, "unrecognized staging header; preserved");
    std::array<std::uint8_t, 20> digest{};
    require(std::fread(digest.data(), 1, 20, file.get()) == 20 &&
            hex(digest) == definition.manifestSha1, "staging manifest mismatch; preserved");
    require(std::fread(digest.data(), 1, 20, file.get()) == 20 &&
            hex(digest) == definition.romSha1, "staging ROM mismatch; preserved");
    auto start = get(file.get(), 4), total = get(file.get(), 4);
    std::uint32_t expectedStart = headerBytes, expectedTotal = headerBytes + 20;
    for (std::size_t i = 0; i < definition.count; ++i) {
        expectedStart += 16 + std::strlen(definition.assets[i].name);
        expectedTotal += 16 + std::strlen(definition.assets[i].name) + definition.assets[i].size;
    }
    require(start == expectedStart && total == expectedTotal, "staging geometry mismatch; preserved");
    file.reset();
    // Only the reserved, recognized staging file is disposable. Never promote it.
    require(::unlink(path.c_str()) == 0, "cannot discard interrupted asset staging file");
    syncDirectory(path.parent_path());
}

void failpoint(const char *point) {
#ifdef SM64_TEST_FAILPOINTS
    const char *requested = std::getenv("SM64_TEST_FAILPOINT");
    if (requested && std::strcmp(requested, point) == 0) std::_Exit(86);
#else
    (void)point;
#endif
}
}

std::string sha1(const std::vector<std::uint8_t>& data) {
    Sha1 digest;
    digest.update(data.data(), data.size());
    return hex(digest.finish());
}

std::vector<std::uint8_t> decodeMio0(const std::vector<std::uint8_t>& rom, std::size_t base) {
    require(base <= rom.size() && rom.size() - base >= 16, "truncated MIO0");
    require(std::memcmp(rom.data() + base, "MIO0", 4) == 0, "invalid MIO0 signature");
    auto size = be32(rom, base + 4), compressed = be32(rom, base + 8), plain = be32(rom, base + 12);
    require(size && size <= maxBlockBytes, "MIO0 output exceeds memory budget");
    require(compressed >= 16 && plain >= compressed && plain <= rom.size() - base, "invalid MIO0 stream offsets");
    std::size_t mask = base + 16, token = base + compressed, literal = base + plain;
    std::vector<std::uint8_t> out;
    out.reserve(size);
    unsigned bits = 0, flags = 0;
    while (out.size() < size) {
        if (!bits) {
            require(mask < base + compressed, "truncated MIO0 mask");
            flags = rom[mask++]; bits = 8;
        }
        if (flags & 0x80) {
            require(literal < rom.size(), "truncated MIO0 literal");
            out.push_back(rom[literal++]);
        } else {
            require(token + 1 < base + plain, "truncated MIO0 backreference");
            unsigned first = rom[token++], second = rom[token++];
            std::size_t count = (first >> 4) + 3, distance = ((first & 15) << 8) + second + 1;
            require(distance <= out.size() && count <= size - out.size(), "invalid MIO0 backreference");
            for (std::size_t i = 0; i < count; ++i) out.push_back(out[out.size() - distance]);
        }
        flags <<= 1; --bits;
    }
    return out;
}

AssetPack::AssetPack(const std::filesystem::path& path) {
    noLinkComponents(path);
    file_ = openRegular(path);
    struct stat info{};
    require(::fstat(::fileno(file_.get()), &info) == 0, "cannot stat asset pack");
    auto size = static_cast<std::uint64_t>(info.st_size);
    require(size >= headerBytes + 20 && size <= maxPackBytes, "invalid asset pack size");
    auto *stream = file_.get();
    char signature[8];
    require(std::fread(signature, 1, 8, stream) == 8 &&
            std::memcmp(signature, magic, 8) == 0 && get(stream, 4) == 1, "unsupported asset pack");
    auto count = get(stream, 4);
    require(count && count <= 4096, "invalid asset count");
    std::array<std::uint8_t, 20> digest;
    require(std::fread(digest.data(), 1, 20, stream) == 20, "truncated manifest digest"); manifestSha1_ = hex(digest);
    require(std::fread(digest.data(), 1, 20, stream) == 20, "truncated ROM digest"); romSha1_ = hex(digest);
    auto dataOffset = get(stream, 4), total = get(stream, 4);
    require(total == size && dataOffset >= headerBytes && dataOffset <= size - 20, "invalid asset pack geometry");
    std::uint32_t next = dataOffset;
    for (unsigned i = 0; i < count; ++i) {
        auto length = get(stream, 2), format = get(stream, 1), flags = get(stream, 1);
        auto width = get(stream, 2), height = get(stream, 2);
        auto offset = get(stream, 4), bytes = get(stream, 4);
        require(length && length <= 255 && format <= 7 && !flags && offset == next && bytes && bytes <= maxBlockBytes &&
                next <= size - 20 && bytes <= size - 20 - next, "invalid asset entry");
        std::string name(length, '\0');
        require(std::fread(name.data(), 1, length, stream) == length &&
                safeName(name) && (entries_.empty() || name > entries_.back().name), "unsafe/unsorted asset name");
        entries_.push_back({name, offset, bytes, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height), static_cast<std::uint8_t>(format)});
        next += bytes;
        require(std::ftell(stream) >= 0 && static_cast<std::uint32_t>(std::ftell(stream)) <= dataOffset, "asset index overlaps payload");
    }
    require(std::ftell(stream) == static_cast<long>(dataOffset) && next == size - 20, "asset pack has gaps or extra bytes");
    readAt(::fileno(stream), size - 20, digest.data(), digest.size());
    require(fileDigest(stream, size - 20) == digest, "asset pack checksum failed");
}

const AssetEntry& AssetPack::entry(const std::string& name) const {
    auto entry = std::lower_bound(entries_.begin(), entries_.end(), name,
                                 [](const AssetEntry& a, const std::string& b) { return a.name < b; });
    require(entry != entries_.end() && entry->name == name, "asset not found");
    return *entry;
}

void AssetPack::read(const std::string& name, std::uint32_t offset, void *destination, std::size_t bytes) const {
    const auto& asset = entry(name);
    require(offset <= asset.size && bytes <= asset.size - offset, "asset read exceeds entry");
    readAt(::fileno(file_.get()), asset.offset + offset, destination, bytes);
}

std::vector<std::uint8_t> AssetPack::load(const std::string& name) const {
    std::vector<std::uint8_t> data(entry(name).size);
    read(name, 0, data.data(), data.size());
    return data;
}

std::vector<std::uint8_t> normalizedRom(const std::filesystem::path& romPath, const GameDefinition& definition) {
    noLinkComponents(romPath);
    require(std::filesystem::is_regular_file(romPath) && !std::filesystem::is_symlink(romPath), "ROM must be a regular file, not a symlink");
    require(definition.romBytes >= 4 && definition.romBytes <= 8 * 1024 * 1024 && definition.romBytes % 4 == 0 &&
            std::filesystem::file_size(romPath) == definition.romBytes, "unsupported ROM size (US retail ROM required)");
    std::vector<std::uint8_t> rom(definition.romBytes);
    auto input = openRegular(romPath);
    readAt(::fileno(input.get()), 0, rom.data(), rom.size());
    std::array<std::uint8_t, 4> z64{{0x80, 0x37, 0x12, 0x40}}, v64{{0x37, 0x80, 0x40, 0x12}}, n64{{0x40, 0x12, 0x37, 0x80}};
    if (std::equal(v64.begin(), v64.end(), rom.begin())) {
        for (std::size_t i = 0; i < rom.size(); i += 2) std::swap(rom[i], rom[i + 1]);
    } else if (std::equal(n64.begin(), n64.end(), rom.begin())) {
        for (std::size_t i = 0; i < rom.size(); i += 4) std::reverse(rom.begin() + i, rom.begin() + i + 4);
    }
    require(std::equal(z64.begin(), z64.end(), rom.begin()) && sha1(rom) == definition.romSha1, "unsupported ROM checksum (unmodified US ROM required)");
    return rom;
}

std::filesystem::path discoverRom(const std::filesystem::path& original, const GameDefinition& definition, bool allowMissing) {
    noLinkComponents(original);
    require(std::filesystem::is_directory(original), "original/ is missing or not a directory");
    std::filesystem::path selected;
    unsigned files = 0;
    for (const auto& item : std::filesystem::directory_iterator(original)) {
        require(++files <= 64, "original/ contains too many entries");
        require(!item.is_symlink() && item.is_regular_file(), "original/ must contain regular files only; no symlinks");
        if (item.file_size() < 4) continue;
        auto file = openRegular(item.path());
        std::array<std::uint8_t, 4> header{};
        readAt(::fileno(file.get()), 0, header.data(), header.size());
        auto signature = (std::uint32_t(header[0]) << 24) | (std::uint32_t(header[1]) << 16) |
                         (std::uint32_t(header[2]) << 8) | header[3];
        if (signature != 0x80371240 && signature != 0x37804012 && signature != 0x40123780) continue;
        // Header and normalized supported hash, never a filename/extension guess.
        normalizedRom(item.path(), definition);
        require(selected.empty(), "ambiguous duplicate supported ROMs in original/");
        selected = item.path();
    }
    require(allowMissing || !selected.empty(), "put your unmodified US SM64 ROM (any filename) inside original/");
    return selected;
}

bool prepare(const std::filesystem::path& romPath, const std::filesystem::path& output, const GameDefinition& definition,
             const std::function<void(std::uint64_t, std::uint64_t)>& progress) {
    noLinkComponents(output);
    require(definition.assets && definition.count && definition.count <= 4096, "invalid extraction manifest");
    std::uint64_t expectedBytes = 0;
    for (std::size_t i = 0; i < definition.count; ++i) expectedBytes += definition.assets[i].size;
    if (progress) progress(0, expectedBytes);
    auto rom = normalizedRom(romPath, definition);
    require(std::filesystem::is_directory(output.parent_path()) && !std::filesystem::is_symlink(output.parent_path()) &&
            !std::filesystem::is_symlink(output) && output != romPath, "unsafe asset output path");
    require(!std::filesystem::exists(output) || !std::filesystem::equivalent(romPath, output),
            "asset output aliases the original ROM");
    Lock lock(output.string() + ".lock.d");
    recoverTemporary(output.string() + ".tmp", definition);
    if (std::filesystem::exists(output)) {
        require(std::filesystem::is_regular_file(output), "asset output is not a file");
        try {
            AssetPack cache(output);
            bool matches = cache.romSha1() == definition.romSha1 && cache.manifestSha1() == definition.manifestSha1 &&
                           cache.entries().size() == definition.count;
            for (std::size_t i = 0; matches && i < definition.count; ++i) {
                const auto& actual = cache.entries()[i];
                const auto& expected = definition.assets[i];
                matches = actual.name == expected.name && actual.size == expected.size &&
                          actual.width == expected.width && actual.height == expected.height &&
                          actual.format == expected.format;
            }
            if (matches) return false;
        } catch (const std::runtime_error&) { /* Rebuild a corrupt cache transactionally. */ }
    }
    require(definition.count && definition.count <= 4096, "invalid extraction manifest");
    std::uint32_t dataOffset = headerBytes;
    for (std::size_t i = 0; i < definition.count; ++i) {
        const auto& asset = definition.assets[i];
        require(safeName(asset.name) && (i == 0 || std::strcmp(definition.assets[i - 1].name, asset.name) < 0) &&
                asset.size && asset.size <= maxBlockBytes && asset.format <= 7, "invalid extraction manifest entry");
        dataOffset += 16 + std::strlen(asset.name);
    }
    std::uint32_t total = dataOffset + 20;
    std::vector<std::uint32_t> offsets;
    for (std::size_t i = 0; i < definition.count; ++i) {
        offsets.push_back(total - 20);
        require(total <= maxPackBytes - definition.assets[i].size, "asset pack exceeds storage budget");
        total += definition.assets[i].size;
    }
    auto temporary = output.string() + ".tmp";
    // Exclusive creation prevents following a malicious symlink or stale partial write.
    int fd = ::open(temporary.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    require(fd >= 0, "cannot create exclusive asset staging file");
    std::FILE *raw = ::fdopen(fd, "w+b");
    if (!raw) { ::close(fd); ::unlink(temporary.c_str()); throw std::runtime_error("cannot open asset staging stream"); }
    auto closeFile = [](std::FILE *file) { std::fclose(file); };
    std::unique_ptr<std::FILE, decltype(closeFile)> stream(raw, closeFile);
    try {
        write(raw, magic, 8); put(raw, 1, 4); put(raw, definition.count, 4);
        auto identity = unhex(definition.manifestSha1); write(raw, identity.data(), identity.size());
        identity = unhex(definition.romSha1); write(raw, identity.data(), identity.size());
        put(raw, dataOffset, 4); put(raw, total, 4);
        require(std::fflush(raw) == 0 && ::fsync(fd) == 0, "asset staging header sync failed");
        syncDirectory(output.parent_path());
        failpoint("header-synced");
        for (std::size_t i = 0; i < definition.count; ++i) {
            const auto& asset = definition.assets[i];
            auto length = std::strlen(asset.name);
            put(raw, length, 2); put(raw, asset.format, 1); put(raw, 0, 1);
            put(raw, asset.width, 2); put(raw, asset.height, 2); put(raw, offsets[i], 4); put(raw, asset.size, 4);
            write(raw, asset.name, length);
        }
        std::vector<std::size_t> order;
        for (std::size_t i = 0; i < definition.count; ++i) order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return definition.assets[a].mio0Block < definition.assets[b].mio0Block; });
        std::int32_t block = -1;
        std::vector<std::uint8_t> decoded;
        std::uint64_t extractedBytes = 0;
        for (auto i : order) {
            const auto& asset = definition.assets[i];
            if (asset.mio0Block != block) {
                block = asset.mio0Block;
                decoded = decodeMio0(rom, block);
            }
            const auto& source = block < 0 ? rom : decoded;
            if (asset.offset > source.size() || asset.size > source.size() - asset.offset)
                throw std::runtime_error("asset outside ROM/MIO0 block: " + std::string(asset.name));
            require(std::fseek(raw, offsets[i], SEEK_SET) == 0, "asset seek failed");
            write(raw, source.data() + asset.offset, asset.size);
            extractedBytes += asset.size;
            if (progress) progress(extractedBytes, expectedBytes);
        }
        require(std::fflush(raw) == 0, "asset flush failed");
        auto digestFile = openRegular(temporary);
        identity = fileDigest(digestFile.get(), total - 20);
        digestFile.reset();
        require(std::fseek(raw, total - 20, SEEK_SET) == 0, "asset footer seek failed");
        write(raw, identity.data(), identity.size());
        require(std::fflush(raw) == 0 && ::fsync(fd) == 0, "asset sync failed");
        stream.reset();
        AssetPack check(temporary);
        failpoint("before-rename");
        std::filesystem::rename(temporary, output);
        syncDirectory(output.parent_path());
    } catch (...) {
        stream.reset(); ::unlink(temporary.c_str()); throw;
    }
    return true;
}
}
