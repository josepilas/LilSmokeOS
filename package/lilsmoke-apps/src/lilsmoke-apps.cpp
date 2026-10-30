#include "AppsProtocol.h"
#include <algorithm>
#include <archive.h>
#include <archive_entry.h>
#include <array>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sodium.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using namespace lilsmoke;
constexpr uint64_t MaxBytes = 8ULL * 1024 * 1024 * 1024,
                   MaxFile = 0xffffffffULL;
constexpr unsigned MaxFiles = 20000;
static volatile sig_atomic_t cancelled;
static void signalCancel(int) { cancelled = 1; }
static void checkCancel() {
  if (cancelled)
    throw std::runtime_error("Cancelled");
}
static void failpoint(const char *point) {
#ifdef LILSMOKE_APPS_QA
  const char *requested = getenv("LILSMOKE_APPS_FAILPOINT");
  if (requested && std::string(requested) == point)
    _exit(86);
#else
  (void)point;
#endif
}
static fs::path root() {
  const char *p = getenv("LILSMOKE_APPS_ROOT");
  return p ? p : "/storage/independent-apps";
}
static fs::path manager() { return root().parent_path() / ".lilsmoke-apps"; }
static std::string fold(std::string s) {
  for (char &c : s)
    if (c >= 'A' && c <= 'Z')
      c += 32;
  return s;
}
static bool regular(const fs::path &p) {
  return fs::is_regular_file(fs::symlink_status(p));
}
static std::string readText(const fs::path &p, size_t limit = 65536) {
  if (!regular(p) || fs::file_size(p) > limit)
    throw std::runtime_error("Missing or oversized metadata");
  std::ifstream in(p, std::ios::binary);
  std::string s((std::istreambuf_iterator<char>(in)), {});
  if (!in.eof() && in.fail())
    throw std::runtime_error("Metadata read failed");
  return s;
}
static void component(const std::string &s) {
  if (s.empty() || s.size() > 120 || s == "." || s == ".." ||
      s.front() == '.' || s.back() == '.' || s.back() == ' ')
    throw std::runtime_error("Invalid application/file name");
  for (unsigned char c : s)
    if (c < 32 || c == 127 ||
        std::string("/\\:*?\"<>|").find(c) != std::string::npos)
      throw std::runtime_error("Unsafe application/file name");
  auto base = fold(s.substr(0, s.find('.')));
  if (base == "con" || base == "prn" || base == "aux" || base == "nul" ||
      (base.size() == 4 &&
       (base.substr(0, 3) == "com" || base.substr(0, 3) == "lpt") &&
       base[3] >= '1' && base[3] <= '9'))
    throw std::runtime_error("Reserved FAT filename");
}
static std::string member(const char *raw) {
  if (!raw)
    throw std::runtime_error("Missing archive path");
  std::string s = raw;
  if (s.size() > 1024 || s.empty() || s.front() == '/' ||
      s.find('\\') != std::string::npos)
    throw std::runtime_error("Unsafe archive path");
  fs::path out;
  for (const auto &part : fs::path(s)) {
    if (part == ".")
      continue;
    if (part == "..")
      throw std::runtime_error("Archive traversal refused");
    auto p = part.string();
    if (p.empty())
      continue;
    if (p == ".lilsmoke-install.json")
      throw std::runtime_error("Reserved package state file");
    if (p.front() == '.') {
      // Hidden payload files are allowed, but never dot-directory traversal.
      if (p.size() > 120)
        throw std::runtime_error("Oversized archive filename");
      for (unsigned char c : p)
        if (c < 32 || c == 127 ||
            std::string("/\\:*?\"<>|").find(c) != std::string::npos)
          throw std::runtime_error("Unsafe archive filename");
    } else
      component(p);
    out /= part;
  }
  return out.generic_string();
}
static std::string digest(const fs::path &p) {
  if (!regular(p))
    throw std::runtime_error("Package is not a regular file");
  crypto_hash_sha256_state state;
  crypto_hash_sha256_init(&state);
  std::ifstream in(p, std::ios::binary);
  std::array<char, 65536> data;
  while (in) {
    checkCancel();
    in.read(data.data(), data.size());
    crypto_hash_sha256_update(
        &state, reinterpret_cast<unsigned char *>(data.data()), in.gcount());
  }
  if (!in.eof())
    throw std::runtime_error("Package read failed");
  unsigned char hash[32];
  char result[65];
  crypto_hash_sha256_final(&state, hash);
  sodium_bin2hex(result, sizeof(result), hash, sizeof(hash));
  return result;
}
static void syncDir(const fs::path &p) {
  int fd = open(p.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0)
    throw std::runtime_error("Cannot open directory for sync");
  int rc = fsync(fd), error = errno;
  close(fd);
  if (rc && error == EINVAL)
    sync();
  if (rc && error != EINVAL && error != EROFS)
    throw std::runtime_error("Directory sync failed");
}
static void writeText(const fs::path &p, const std::string &s) {
  auto temp = p;
  temp += ".new";
  int fd = open(temp.c_str(),
                O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
  if (fd < 0)
    throw std::runtime_error("Cannot create metadata");
  size_t at = 0;
  bool good = true;
  while (at < s.size()) {
    ssize_t n = write(fd, s.data() + at, s.size() - at);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0) {
      good = false;
      break;
    }
    at += n;
  }
  if (fsync(fd))
    good = false;
  if (close(fd))
    good = false;
  if (!good) {
    fs::remove(temp);
    throw std::runtime_error("Metadata write/sync failed");
  }
  fs::rename(temp, p);
  syncDir(p.parent_path());
}
static void emit(AppJson j) {
  std::cout << appSerialize(j.get()) << '\n' << std::flush;
}
static AppJson event(const char *type) {
  auto j = appJson(json_object_new_object());
  appSet(j.get(), "type", type);
  return j;
}
static void storageGuard() {
  if (fs::exists("/run/lilsmoke-storage/exporting"))
    throw std::runtime_error("Eject LILDRIVE first");
  std::ifstream backing("/sys/kernel/config/usb_gadget/lilsmoke/functions/"
                        "mass_storage.0/lun.0/file");
  std::string s;
  if (std::getline(backing, s) && !s.empty())
    throw std::runtime_error("Eject LILDRIVE first");
  auto r = root();
  if (r.filename() != "independent-apps")
    throw std::runtime_error("Application root must be named independent-apps");
  for (fs::path p = r; !p.empty() && p != p.parent_path(); p = p.parent_path())
    if (fs::is_symlink(fs::symlink_status(p)))
      throw std::runtime_error("Application root contains a symlink");
}
struct Version {
  std::vector<std::string> numbers, pre;
};
static Version version(std::string s) {
  if (s.empty() || s.size() > 64)
    throw std::runtime_error("Invalid version");
  auto build = s.find('+');
  if (build != std::string::npos)
    s.resize(build);
  Version v;
  auto dash = s.find('-');
  std::string main = s.substr(0, dash),
              pre = dash == std::string::npos ? "" : s.substr(dash + 1);
  auto split = [](const std::string &text) {
    std::vector<std::string> out;
    size_t at = 0, end;
    do {
      end = text.find('.', at);
      out.push_back(text.substr(at, end == std::string::npos ? end : end - at));
      at = end + 1;
    } while (end != std::string::npos);
    return out;
  };
  v.numbers = split(main);
  if (v.numbers.size() > 3)
    throw std::runtime_error("Version needs one to three numeric components");
  for (auto &n : v.numbers) {
    if (n.empty() || n.find_first_not_of("0123456789") != std::string::npos)
      throw std::runtime_error("Invalid version number");
    auto p = n.find_first_not_of('0');
    n = p == std::string::npos ? "0" : n.substr(p);
  }
  while (v.numbers.size() < 3)
    v.numbers.push_back("0");
  if (dash != std::string::npos) {
    v.pre = split(pre);
    for (auto &p : v.pre)
      if (p.empty() ||
          p.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUV"
                              "WXYZ0123456789-") != std::string::npos)
        throw std::runtime_error("Invalid prerelease version");
  }
  return v;
}
static int numberCompare(const std::string &a, const std::string &b) {
  return a.size() != b.size() ? (a.size() < b.size() ? -1 : 1)
         : a == b             ? 0
         : a < b              ? -1
                              : 1;
}
static int compare(const std::string &a, const std::string &b) {
  auto x = version(a), y = version(b);
  for (unsigned i = 0; i < 3; ++i) {
    int n = numberCompare(x.numbers[i], y.numbers[i]);
    if (n)
      return n;
  }
  if (x.pre.empty() != y.pre.empty())
    return x.pre.empty() ? 1 : -1;
  for (size_t i = 0; i < std::min(x.pre.size(), y.pre.size()); ++i) {
    auto p = x.pre[i], q = y.pre[i];
    if (p == q)
      continue;
    bool pn = p.find_first_not_of("0123456789") == std::string::npos,
         qn = q.find_first_not_of("0123456789") == std::string::npos;
    if (pn != qn)
      return pn ? -1 : 1;
    return pn ? numberCompare(p, q) : (p < q ? -1 : 1);
  }
  return x.pre.size() == y.pre.size()  ? 0
         : x.pre.size() < y.pre.size() ? -1
                                       : 1;
}
struct Meta {
  AppJson json;
  std::string name, version, id, icon;
  std::vector<std::string> openCores;
};
static Meta metadata(const std::string &text) {
  auto j = parseAppJson(text);
  if (!json_object_is_type(j.get(), json_type_object))
    throw std::runtime_error("meta.json must be an object");
  Meta m{j, appString(j.get(), "name"), appString(j.get(), "version"),
         appString(j.get(), "id"), appString(j.get(), "icon"), appOpenCores(j.get())};
  component(m.name);
  version(m.version);
  if (m.id.empty()) {
    unsigned char hash[32];
    char hex[65];
    crypto_hash_sha256(hash,
                       reinterpret_cast<const unsigned char *>(m.name.data()),
                       m.name.size());
    sodium_bin2hex(hex, sizeof(hex), hash, 32);
    m.id = "name." + std::string(hex);
  }
  if (m.id.size() > 128 ||
      m.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVW"
                             "XYZ0123456789._-") != std::string::npos)
    throw std::runtime_error("Invalid application id");
  if (!m.icon.empty() && member(m.icon.c_str()) != m.icon)
    throw std::runtime_error("Invalid icon path");
  if (!m.openCores.empty() && m.id.find("..") != std::string::npos)
    throw std::runtime_error("Emulator application id must not contain '..'");
  appSet(j.get(), "id", m.id);
  return m;
}
static std::string script(const fs::path &dir) {
  std::string result;
  unsigned count = 0;
  for (auto &e : fs::directory_iterator(dir))
    if (regular(e.path()) && fold(e.path().extension().string()) == ".sh") {
      result = e.path().filename().string();
      ++count;
    }
  if (count != 1)
    throw std::runtime_error(
        "Application requires exactly one top-level .sh file");
  return result;
}
struct Installed {
  fs::path dir;
  Meta meta;
  std::string launcher;
};
static std::vector<Installed> catalog() {
  std::vector<Installed> out;
  if (!fs::exists(root()))
    return out;
  unsigned count = 0;
  for (auto &e : fs::directory_iterator(root())) {
    if (++count > 2048)
      break;
    if (!fs::is_directory(e.symlink_status()))
      continue;
    try {
      auto m = metadata(readText(e.path() / "meta.json"));
      auto launcher = script(e.path());
      m.name = e.path().filename().string();
      component(m.name);
      out.push_back({e.path(), m, launcher});
    } catch (const std::exception &) {
    }
  }
  std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
    return fold(a.meta.name) < fold(b.meta.name);
  });
  return out;
}
static Installed installed(const std::string &name) {
  component(name);
  auto p = root() / name;
  if (!fs::is_directory(fs::symlink_status(p)))
    throw std::runtime_error("Application folder missing");
  auto m = metadata(readText(p / "meta.json"));
  m.name = name;
  return {p, m, script(p)};
}
struct Entry {
  std::string name;
  uint64_t bytes;
  bool dir;
};
struct Package {
  Meta meta;
  std::vector<Entry> entries;
  uint64_t bytes = 0;
  std::string hash, launcher;
};
struct Reader {
  archive *p = archive_read_new();
  ~Reader() {
    if (p)
      archive_read_free(p);
  }
  Reader(const fs::path &file) {
    if (!p)
      throw std::runtime_error("Cannot allocate archive reader");
    archive_read_support_filter_all(p);
    archive_read_support_format_all(p);
    if (archive_read_open_filename(p, file.c_str(), 65536) != ARCHIVE_OK)
      throw std::runtime_error(archive_error_string(p) ? archive_error_string(p)
                                                       : "Cannot read archive");
  }
};
static Package inspect(const fs::path &source) {
  Package p;
  p.hash = digest(source);
  Reader r(source);
  archive_entry *e;
  std::map<std::string, std::string> seen;
  std::string meta;
  unsigned launchers = 0;
  for (;;) {
    checkCancel();
    int rc = archive_read_next_header(r.p, &e);
    if (rc == ARCHIVE_EOF)
      break;
    if (rc != ARCHIVE_OK)
      throw std::runtime_error("Invalid archive header");
    auto name = member(archive_entry_pathname(e));
    if (name.empty() && archive_entry_filetype(e) == AE_IFDIR)
      continue;
    if (name.empty() || archive_entry_symlink(e) || archive_entry_hardlink(e))
      throw std::runtime_error(
          "Package links are not supported on FAT storage");
    bool dir = archive_entry_filetype(e) == AE_IFDIR;
    if (!dir && archive_entry_filetype(e) != AE_IFREG)
      throw std::runtime_error("Archive special file refused");
    auto key = fold(name);
    if (!seen.emplace(key, name).second)
      throw std::runtime_error("Duplicate/case-colliding archive path");
    auto size = archive_entry_size(e);
    if (size < 0 || static_cast<uint64_t>(size) > MaxFile)
      throw std::runtime_error("Package member exceeds FAT file limit");
    if (p.entries.size() >= MaxFiles ||
        static_cast<uint64_t>(size) > MaxBytes - p.bytes)
      throw std::runtime_error("Package exceeds installation budget");
    p.entries.push_back({name, dir ? 0 : static_cast<uint64_t>(size), dir});
    p.bytes += dir ? 0 : size;
    if (!dir && fs::path(name).parent_path().empty() &&
        fold(fs::path(name).extension().string()) == ".sh") {
      ++launchers;
      p.launcher = name;
    }
    if (name == "meta.json" && !dir) {
      if (size > 65536)
        throw std::runtime_error("meta.json exceeds metadata budget");
      std::array<char, 4096> block;
      la_ssize_t n;
      while ((n = archive_read_data(r.p, block.data(), block.size())) > 0) {
        meta.append(block.data(), n);
        if (meta.size() > 65536)
          throw std::runtime_error("Oversized metadata");
      }
      if (n < 0)
        throw std::runtime_error("Cannot read meta.json");
    } else if (archive_read_data_skip(r.p) != ARCHIVE_OK)
      throw std::runtime_error("Cannot read package member");
  }
  if (meta.empty())
    throw std::runtime_error(
        "meta.json must be at the archive root, not inside a wrapper folder");
  if (launchers != 1)
    throw std::runtime_error("Package requires exactly one top-level .sh file");
  p.meta = metadata(meta);
  for (auto &e : p.entries) {
    fs::path parent = fs::path(e.name).parent_path();
    while (!parent.empty()) {
      auto found = seen.find(fold(parent.generic_string()));
      if (found != seen.end()) {
        auto item =
            std::find_if(p.entries.begin(), p.entries.end(),
                         [&](auto &x) { return fold(x.name) == found->first; });
        if (!item->dir)
          throw std::runtime_error("File/directory collision in package");
      }
      parent = parent.parent_path();
    }
  }
  if (!p.meta.icon.empty() &&
      std::none_of(p.entries.begin(), p.entries.end(), [&](auto &e) {
        return e.name == p.meta.icon && !e.dir && e.bytes <= 2 * 1024 * 1024;
      }))
    throw std::runtime_error("Icon missing or oversized");
  return p;
}
static std::string currentName(const Package &p) {
  std::string found;
  for (auto &a : catalog())
    if (a.meta.id == p.meta.id) {
      if (!found.empty())
        throw std::runtime_error("Duplicate installed application id");
      found = a.meta.name;
    }
  if (found.empty())
    for (auto &e : fs::directory_iterator(root()))
      if (fold(e.path().filename().string()) == fold(p.meta.name))
        throw std::runtime_error(
            "An application with this name already exists");
  return found;
}
static void recover() {
  auto path = manager() / "transaction.json";
  if (!fs::exists(path))
    return;
  auto j = parseAppJson(readText(path));
  auto name = appString(j.get(), "name"), old = appString(j.get(), "old", name),
       stageName = appString(j.get(), "stage"),
       backupName = appString(j.get(), "backup");
  component(name);
  component(old);
  component(stageName);
  component(backupName);
  auto target = root() / name, stage = manager() / stageName,
       backup = manager() / backupName;
  for (auto &p : {target, stage, backup})
    if (fs::is_symlink(fs::symlink_status(p)))
      throw std::runtime_error("Unsafe transaction symlink");
  if (fs::exists(backup)) {
    if (appString(j.get(), "operation") == "uninstall" || fs::exists(target))
      fs::remove_all(backup);
    else
      fs::rename(backup, root() / old);
  }
  if (fs::exists(stage))
    fs::remove_all(stage);
  fs::remove(path);
  syncDir(root());
  syncDir(manager());
}
static void removeAbandonedStages() {
  for (auto &entry : fs::directory_iterator(manager())) {
    auto name = entry.path().filename().string();
    if (name.rfind("stage-", 0) == 0 && name.size() > 6 &&
        name.find_first_not_of("0123456789", 6) == std::string::npos) {
      if (!fs::is_directory(entry.symlink_status()))
        throw std::runtime_error("Unsafe abandoned staging directory");
      fs::remove_all(entry.path());
    }
  }
  auto unfinished = manager() / "transaction.json.new";
  if (fs::exists(fs::symlink_status(unfinished))) {
    if (!regular(unfinished))
      throw std::runtime_error("Unsafe unfinished transaction metadata");
    fs::remove(unfinished);
  }
}
struct Lock {
  bool held = false;
  Lock() {
    storageGuard();
    fs::create_directories(root());
    fs::create_directories(manager());
    auto p = manager() / "lock";
    if (!fs::create_directory(p)) {
      // FAT timestamps are unreliable before the console's clock is set.
      for (int attempt = 0; attempt < 20 && !regular(p / "owner") &&
                            !regular(p / "owner.new"); ++attempt)
        usleep(10000);
      try {
        auto owner = regular(p / "owner") ? p / "owner" : p / "owner.new";
        auto j = parseAppJson(readText(owner));
        auto pid = json_object_get_int64(appField(j.get(), "pid"));
        if (pid > 1 && (kill(pid, 0) == 0 || errno == EPERM))
          throw std::runtime_error("Another application operation is running");
      } catch (const std::runtime_error &e) {
        if (std::string(e.what()) == "Another application operation is running")
          throw;
      }
      if (fs::is_symlink(fs::symlink_status(p)))
        throw std::runtime_error("Unsafe application lock");
      fs::remove_all(p);
      if (!fs::create_directory(p))
        throw std::runtime_error("Cannot lock application storage");
    }
    held = true;
    auto j = appJson(json_object_new_object());
    appNumber(j.get(), "pid", getpid());
    writeText(p / "owner", appSerialize(j.get()));
    recover();
    removeAbandonedStages();
  }
  ~Lock() {
    if (held) {
      std::error_code ec;
      fs::remove_all(manager() / "lock", ec);
    }
  }
};
struct Progress {
  uint64_t done = 0, total = 0;
  std::chrono::steady_clock::time_point last{};
  void update(uint64_t n, bool force = false) {
    checkCancel();
    done += n;
    auto now = std::chrono::steady_clock::now();
    if (!force && now - last < std::chrono::milliseconds(120))
      return;
    last = now;
    auto j = event("progress");
    appNumber(j.get(), "processed", done);
    appNumber(j.get(), "total", total);
    appNumber(j.get(), "percent",
              total ? std::min<uint64_t>(99, done * 100 / total) : 0);
    emit(j);
  }
};
static void copyFile(const fs::path &from, const fs::path &to,
                     Progress &progress) {
  fs::create_directories(to.parent_path());
  int in = open(from.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC), out = -1;
  if (in >= 0)
    out = open(to.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
               0755);
  if (in < 0 || out < 0) {
    if (in >= 0)
      close(in);
    throw std::runtime_error("Cannot preserve application data");
  }
  std::array<char, 65536> buffer;
  try {
    for (;;) {
      checkCancel();
      ssize_t n = read(in, buffer.data(), buffer.size());
      if (n < 0 && errno == EINTR)
        continue;
      if (n < 0)
        throw std::runtime_error("Application data read failed");
      if (!n)
        break;
      size_t at = 0;
      while (at < static_cast<size_t>(n)) {
        auto k = write(out, buffer.data() + at, n - at);
        if (k < 0 && errno == EINTR)
          continue;
        if (k <= 0)
          throw std::runtime_error("Application data write failed");
        at += k;
      }
      progress.update(n);
    }
    if (fsync(out))
      throw std::runtime_error("Application data sync failed");
    close(in);
    close(out);
  } catch (...) {
    close(in);
    close(out);
    throw;
  }
}
static void extract(const fs::path &source, const Package &p,
                    const fs::path &stage, Progress &progress) {
  Reader r(source);
  archive_entry *e;
  size_t index = 0;
  for (;;) {
    checkCancel();
    auto rc = archive_read_next_header(r.p, &e);
    if (rc == ARCHIVE_EOF)
      break;
    if (rc != ARCHIVE_OK)
      throw std::runtime_error("Archive extraction failed");
    auto name = member(archive_entry_pathname(e));
    if (name.empty() && archive_entry_filetype(e) == AE_IFDIR)
      continue;
    if (index >= p.entries.size() || name != p.entries[index].name ||
        archive_entry_symlink(e) || archive_entry_hardlink(e))
      throw std::runtime_error("Package changed while installing");
    auto entry = p.entries[index++];
    auto path = stage / name;
    if (entry.dir) {
      fs::create_directories(path);
      continue;
    }
    fs::create_directories(path.parent_path());
    int fd = open(path.c_str(),
                  O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0755);
    if (fd < 0)
      throw std::runtime_error("Cannot create application file");
    uint64_t size = 0;
    std::array<char, 65536> block;
    try {
      la_ssize_t n;
      while ((n = archive_read_data(r.p, block.data(), block.size())) > 0) {
        checkCancel();
        if (static_cast<uint64_t>(n) > entry.bytes - size)
          throw std::runtime_error("Archive size mismatch");
        size_t at = 0;
        while (at < static_cast<size_t>(n)) {
          ssize_t k = write(fd, block.data() + at, n - at);
          if (k < 0 && errno == EINTR)
            continue;
          if (k <= 0)
            throw std::runtime_error("Installation write failed");
          at += k;
        }
        size += n;
        progress.update(n);
        failpoint("extracting");
      }
      if (n < 0 || size != entry.bytes || fsync(fd))
        throw std::runtime_error("Incomplete/corrupt application file");
      close(fd);
    } catch (...) {
      close(fd);
      throw;
    }
  }
  if (index != p.entries.size() || digest(source) != p.hash)
    throw std::runtime_error("Package changed during installation");
}
static void transaction(const std::string &name, const std::string &old,
                        const fs::path &stage, const char *operation) {
  auto backup = manager() / ("backup-" + std::to_string(getpid()));
  if (fs::exists(backup))
    throw std::runtime_error("Stale transaction backup");
  auto j = appJson(json_object_new_object());
  appSet(j.get(), "name", name);
  appSet(j.get(), "old", old.empty() ? name : old);
  appSet(j.get(), "stage", stage.filename().string());
  appSet(j.get(), "backup", backup.filename().string());
  appSet(j.get(), "operation", operation);
  writeText(manager() / "transaction.json", appSerialize(j.get()));
  failpoint("prepared");
  try {
    if (!old.empty())
      fs::rename(root() / old, backup);
    syncDir(root());
    syncDir(manager());
    failpoint("old-moved");
    if (std::string(operation) != "uninstall")
      fs::rename(stage, root() / name);
    syncDir(root());
    failpoint("new-moved");
    if (fs::exists(backup))
      fs::remove_all(backup);
    fs::remove(manager() / "transaction.json");
    syncDir(manager());
  } catch (...) {
    recover();
    throw;
  }
}
static void installLocked(const fs::path &source, const std::string &expected) {
  auto p = inspect(source);
  if (!expected.empty() && p.hash != expected)
    throw std::runtime_error("Package changed since preview");
  auto old = currentName(p), name = old.empty() ? p.meta.name : old;
  if (!old.empty() && compare(p.meta.version, installed(old).meta.version) == 0)
    throw std::runtime_error("Already installed.");
  std::set<std::string> owned, ownedDirs, incomingFiles, incomingDirs;
  for (auto &e : p.entries) {
    if (!e.dir)
      incomingFiles.insert(fold(e.name));
    fs::path at = e.dir ? fs::path(e.name) : fs::path(e.name).parent_path();
    while (!at.empty()) {
      incomingDirs.insert(fold(at.generic_string()));
      at = at.parent_path();
    }
  }
  if (!old.empty() && regular(root() / old / ".lilsmoke-install.json")) {
    auto j = parseAppJson(
        readText(root() / old / ".lilsmoke-install.json", 2 * 1024 * 1024));
    auto a = appField(j.get(), "files");
    if (!a || !json_object_is_type(a, json_type_array) ||
        json_object_array_length(a) > MaxFiles)
      throw std::runtime_error("Invalid installed file manifest");
    for (size_t i = 0; i < json_object_array_length(a); ++i) {
      auto v = json_object_array_get_idx(a, i);
      if (!json_object_is_type(v, json_type_string))
        throw std::runtime_error("Invalid installed file manifest");
      auto rel = member(json_object_get_string(v));
      owned.insert(fold(rel));
      for (auto parent = fs::path(rel).parent_path(); !parent.empty();
           parent = parent.parent_path())
        ownedDirs.insert(fold(parent.generic_string()));
    }
  }
  std::vector<fs::path> preserved, preservedDirs;
  Progress progress;
  progress.total = p.bytes;
  if (!old.empty()) {
    unsigned count = 0;
    for (auto &e : fs::recursive_directory_iterator(root() / old)) {
      checkCancel();
      if (++count > MaxFiles)
        throw std::runtime_error("Application data exceeds file budget");
      if (e.is_symlink())
        throw std::runtime_error(
            "Application data symlinks cannot be updated safely");
      auto rel = fs::relative(e.path(), root() / old).generic_string();
      auto key = fold(rel);
      bool conflict = false;
      for (auto &incoming : incomingFiles)
        if (key == incoming || key.rfind(incoming + "/", 0) == 0)
          conflict = true;
      if (e.is_directory()) {
        if (!conflict && !ownedDirs.count(key) && !incomingDirs.count(key))
          preservedDirs.push_back(e.path());
        continue;
      }
      if (!e.is_regular_file())
        throw std::runtime_error("Unsupported application data");
      if (key == ".lilsmoke-install.json" || owned.count(key) || conflict ||
          incomingDirs.count(key) ||
          (fs::path(rel).parent_path().empty() &&
           fold(fs::path(rel).extension().string()) == ".sh"))
        continue;
      if (e.file_size() > MaxFile || e.file_size() > MaxBytes - progress.total)
        throw std::runtime_error("Application data exceeds disk budget");
      progress.total += e.file_size();
      preserved.push_back(e.path());
    }
  }
  struct statvfs space;
  if (statvfs(root().c_str(), &space) ||
      static_cast<uint64_t>(space.f_bavail) * space.f_frsize <
          progress.total + 1024 * 1024)
    throw std::runtime_error("Not enough free storage for safe installation");
  auto stage = manager() / ("stage-" + std::to_string(getpid()));
  if (!fs::create_directory(stage))
    throw std::runtime_error("Stale installation staging directory");
  try {
    progress.update(0, true);
    failpoint("staging");
    for (auto &path : preservedDirs)
      fs::create_directories(stage / fs::relative(path, root() / old));
    for (auto &path : preserved)
      copyFile(path, stage / fs::relative(path, root() / old), progress);
    extract(source, p, stage, progress);
    appSet(p.meta.json.get(), "name", name);
    appSet(p.meta.json.get(), "id", p.meta.id);
    writeText(stage / "meta.json", appSerialize(p.meta.json.get()));
    auto state = appJson(json_object_new_object());
    appSet(state.get(), "id", p.meta.id);
    auto files = json_object_new_array();
    for (auto &e : p.entries)
      if (!e.dir)
        json_object_array_add(files, json_object_new_string(e.name.c_str()));
    json_object_object_add(state.get(), "files", files);
    writeText(stage / ".lilsmoke-install.json", appSerialize(state.get()));
    script(stage);
    syncDir(stage);
    checkCancel();
    transaction(name, old, stage, "install");
    auto done = event("done");
    appSet(done.get(), "name", name);
    appSet(done.get(), "version", p.meta.version);
    appNumber(done.get(), "percent", 100);
    appNumber(done.get(), "processed", progress.total);
    appNumber(done.get(), "total", progress.total);
    emit(done);
  } catch (...) {
    if (fs::exists(stage))
      fs::remove_all(stage);
    throw;
  }
}
static void install(const fs::path &source, const std::string &expected) {
  Lock lock;
  installLocked(source, expected);
}
static void seed(const fs::path &source) {
  Lock lock;
  const auto p = inspect(source);
  unsigned char hash[32]; char hex[65];
  crypto_hash_sha256(hash, reinterpret_cast<const unsigned char *>(p.meta.id.data()), p.meta.id.size());
  sodium_bin2hex(hex, sizeof(hex), hash, sizeof(hash));
  const auto marker = manager() / ("builtin-" + std::string(hex) + ".json");
  const auto old = currentName(p);
  if (regular(marker)) {
    auto saved = parseAppJson(readText(marker, 4096));
    if (appString(saved.get(), "id") != p.meta.id) throw std::runtime_error("Invalid builtin seed record");
    // A deliberate uninstall stays uninstalled. Never downgrade a user package.
    if (!old.empty() && compare(p.meta.version, installed(old).meta.version) > 0)
      installLocked(source, p.hash);
  } else {
    if (!old.empty() && installed(old).meta.id != p.meta.id)
      throw std::runtime_error("Builtin application name belongs to another package");
    if (old.empty()) installLocked(source, p.hash);
    auto saved = appJson(json_object_new_object());
    appSet(saved.get(), "id", p.meta.id);
    appSet(saved.get(), "version", p.meta.version);
    writeText(marker, appSerialize(saved.get()));
  }
  emit(event("seeded"));
}
static void renameApp(const std::string &from, const std::string &to) {
  Lock lock;
  component(to);
  auto a = installed(from);
  if (from == to)
    return;
  for (auto &e : fs::directory_iterator(root()))
    if (fold(e.path().filename().string()) == fold(to) && e.path() != a.dir)
      throw std::runtime_error("Application name already exists");
  auto stage = manager() / ("stage-" + std::to_string(getpid()));
  if (!fs::create_directory(stage))
    throw std::runtime_error("Stale rename staging directory");
  try {
    Progress p;
    for (auto &e : fs::recursive_directory_iterator(a.dir)) {
      checkCancel();
      if (e.is_symlink())
        throw std::runtime_error("Application contains symlinks");
      if (e.is_directory())
        fs::create_directories(stage / fs::relative(e.path(), a.dir));
      else if (e.is_regular_file())
        copyFile(e.path(), stage / fs::relative(e.path(), a.dir), p);
      else
        throw std::runtime_error("Unsupported application file");
    }
    appSet(a.meta.json.get(), "name", to);
    appSet(a.meta.json.get(), "id", a.meta.id);
    writeText(stage / "meta.json", appSerialize(a.meta.json.get()));
    checkCancel();
    transaction(to, from, stage, "rename");
  } catch (...) {
    if (fs::exists(stage))
      fs::remove_all(stage);
    throw;
  }
  auto j = event("done");
  appSet(j.get(), "name", to);
  emit(j);
}
static void uninstall(const std::string &name) {
  Lock lock;
  auto a = installed(name);
  auto stage = manager() / ("stage-" + std::to_string(getpid()));
  transaction(name, name, stage, "uninstall");
  emit(event("done"));
}
static void preview(const fs::path &source, const fs::path &output) {
  storageGuard();
  auto p = inspect(source);
  fs::create_directories(root());
  auto old = currentName(p);
  auto j = event("package");
  appSet(j.get(), "name", p.meta.name);
  appSet(j.get(), "version", p.meta.version);
  appSet(j.get(), "id", p.meta.id);
  appSet(j.get(), "sha256", p.hash);
  appNumber(j.get(), "bytes", p.bytes);
  appSet(j.get(), "installed_name", old);
  appSet(j.get(), "relation",
         old.empty()                                                 ? "new"
         : compare(p.meta.version, installed(old).meta.version) == 0 ? "same"
         : compare(p.meta.version, installed(old).meta.version) > 0  ? "newer"
                                                                     : "older");
  if (!p.meta.icon.empty()) {
    fs::create_directories(output);
    auto target =
        output / ("icon" + fs::path(p.meta.icon).extension().string());
    Reader r(source);
    archive_entry *e;
    while (archive_read_next_header(r.p, &e) == ARCHIVE_OK) {
      if (member(archive_entry_pathname(e)) == p.meta.icon) {
        std::string bytes;
        std::array<char, 65536> block;
        la_ssize_t n;
        while ((n = archive_read_data(r.p, block.data(), block.size())) > 0) {
          bytes.append(block.data(), n);
          if (bytes.size() > 2 * 1024 * 1024)
            throw std::runtime_error("Oversized icon");
        }
        if (n < 0)
          throw std::runtime_error("Cannot read icon");
        writeText(target, bytes);
        appSet(j.get(), "icon", target.string());
        break;
      }
      archive_read_data_skip(r.p);
    }
  }
  emit(j);
}
static void launch(const std::string &name, const std::string &core = {}, const std::string &rom = {}) {
  storageGuard();
  auto a = installed(name);
  fs::current_path(a.dir);
  setenv("LILSMOKE_APP_DIR", a.dir.c_str(), 1);
  setenv("LILSMOKE_APP_NAME", name.c_str(), 1);
  setenv("LILSMOKE_APP_ID", a.meta.id.c_str(), 1);
  setenv("HOME", a.dir.c_str(), 1);
  std::string libs =
      (a.dir / "runtime/lib").string() + ":" + (a.dir / "lib").string();
  if (const char *old = getenv("LD_LIBRARY_PATH"))
    libs += ":" + std::string(old);
  setenv("LD_LIBRARY_PATH", libs.c_str(), 1);
  setenv("SDL_VIDEODRIVER", "directfb", 1);
  setenv("SDL_RENDER_DRIVER", "software", 1);
  setenv("SDL12COMPAT_OPENGL_SCALING", "0", 1);
  unsetenv("LILSMOKE_OD_INPUT_EXTERNAL");
  unsetenv("core"); unsetenv("rom");
  unsetenv("LILSMOKE_OPEN_CORE"); unsetenv("LILSMOKE_OPEN_ROM");
  if (!core.empty()) {
    setenv("core", core.c_str(), 1); setenv("rom", rom.c_str(), 1);
    setenv("LILSMOKE_OPEN_CORE", core.c_str(), 1); setenv("LILSMOKE_OPEN_ROM", rom.c_str(), 1);
  }
  unsetenv("DISPLAY");
  unsetenv("WAYLAND_DISPLAY");
  setenv("SDL_AUDIODRIVER", "alsa", 1);
  if (const char *h = getenv("LILSMOKE_HCFB"); h && std::string(h) == "1") {
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    setenv("LILSMOKE_M8_AUDIO", "1", 1);
    setenv("LILSMOKE_M8_NATIVE_VIDEO", "1", 1);
    setenv("LILSMOKE_OD_NATIVE", "1", 1);
    setenv("LILSMOKE_OD_ENGINE", "independent", 1);
    if (!fs::is_regular_file("/usr/lib/lilsmoke/libod-sdl.so") ||
        !fs::is_regular_file("/os/libm8-display.so"))
      throw std::runtime_error("Native M8 application adapters missing");
    const char *loading = getenv("LILSMOKE_M8_LOADING_GAME");
    const char *token = getenv("LILSMOKE_M8_BOOT_STATE");
    const bool loadingGame = loading && std::string(loading) == "1" &&
        token && std::string(token) == "/run/m8-boot-logo.state";
    setenv("LD_PRELOAD", loadingGame
        ? "/os/libm8-boot-logo.so:/usr/lib/lilsmoke/libod-sdl.so:/os/libm8-display.so"
        : "/usr/lib/lilsmoke/libod-sdl.so:/os/libm8-display.so", 1);
  }
  setenv("DFBARGS",
         "no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-"
         "switching,disable-module=keyboard,linux-input-force,depth=32,"
         "pixelformat=ARGB",
         0);
  auto path = a.dir / a.launcher;
  if (core.empty()) execl("/bin/sh", "sh", path.c_str(), static_cast<char *>(nullptr));
  else {
    const auto coreArg = "core=" + core, romArg = "rom=" + rom;
    execl("/bin/sh", "sh", path.c_str(), coreArg.c_str(), romArg.c_str(), static_cast<char *>(nullptr));
  }
  throw std::runtime_error("Application launch failed");
}
static void emulatorCatalog() {
  storageGuard();
  const auto apps = catalog();
  std::map<std::string, unsigned> ids;
  for (const auto &a : apps) ++ids[a.meta.id];
  unsigned emitted = 0;
  for (const auto &a : apps) {
    if (a.meta.openCores.empty() || ids[a.meta.id] != 1 || emitted >= 128) continue;
    std::string systems;
    for (const auto &id : a.meta.openCores) { if (!systems.empty()) systems += ','; systems += id; }
    std::cout << a.meta.id << '\t' << a.meta.name << '\t' << systems << '\t' << a.meta.version << '\n';
    ++emitted;
  }
}
static void launchCore(const std::string &id, const std::string &requested, const fs::path &input) {
  storageGuard();
  const auto core = canonicalEmulatorSystem(requested);
  if (!emulatorSystemDefinition(core)) throw std::runtime_error("Unknown emulator system");
  std::string name; unsigned matches = 0;
  for (const auto &a : catalog()) if (a.meta.id == id) {
    ++matches;
    if (std::find(a.meta.openCores.begin(), a.meta.openCores.end(), core) != a.meta.openCores.end()) name = a.meta.name;
  }
  if (matches != 1 || name.empty()) throw std::runtime_error("Application missing, ambiguous or does not declare this open_core");
  const char *configured = getenv("LILSMOKE_ROM_ROOT");
  const auto base = fs::canonical(fs::path(configured ? configured : "/storage/roms") / core);
  const auto candidate = input.is_absolute() ? input : base / input;
  if (!regular(candidate)) throw std::runtime_error("ROM is missing or is a link");
  const auto full = fs::canonical(candidate), relative = full.lexically_relative(base);
  if (relative.empty() || relative.is_absolute()) throw std::runtime_error("ROM is outside its system folder");
  for (const auto &part : relative) if (part == ".." || part == ".") throw std::runtime_error("ROM escaped its system folder");
  const auto logical = relative.generic_string();
  if (logical.size() > 1024 || std::any_of(logical.begin(), logical.end(), [](unsigned char c) { return c < 32 || c == 127; }))
    throw std::runtime_error("Unsafe ROM filename");
  launch(name, core, logical);
}
int main(int argc, char **argv) {
  signal(SIGTERM, signalCancel);
  signal(SIGINT, signalCancel);
  signal(SIGPIPE, SIG_IGN);
  if (sodium_init() < 0)
    return 1;
  try {
    if (argc < 2)
      throw std::runtime_error(
          "Usage: lilsmoke-apps "
          "inspect/install/seed/list/rename/uninstall/run/run-core/emulators/recover");
    std::string command = argv[1];
    if (command == "run" && argc == 3) {
      launch(argv[2]);
      return 0;
    }
    if (command == "run-core" && argc == 5) {
      launchCore(argv[2], argv[3], argv[4]); return 0;
    }
    struct rlimit limit = {64 * 1024 * 1024, 64 * 1024 * 1024};
    setrlimit(RLIMIT_AS, &limit);
    if (command == "emulators" && argc == 2)
      emulatorCatalog();
    else if (command == "inspect" && argc == 4)
      preview(argv[2], argv[3]);
    else if (command == "install" && (argc == 3 || argc == 4))
      install(argv[2], argc == 4 ? argv[3] : "");
    else if (command == "seed" && argc == 3)
      seed(argv[2]);
    else if (command == "rename" && argc == 4)
      renameApp(argv[2], argv[3]);
    else if (command == "uninstall" && argc == 3)
      uninstall(argv[2]);
    else if (command == "recover" && argc == 2) {
      Lock lock;
      emit(event("done"));
    } else if (command == "list" && argc == 2) {
      storageGuard();
      auto j = event("catalog");
      auto list = json_object_new_array();
      for (auto &a : catalog()) {
        auto item = json_object_new_object();
        appSet(item, "name", a.meta.name);
        appSet(item, "version", a.meta.version);
        appSet(item, "id", a.meta.id);
        appSet(item, "icon",
               a.meta.icon.empty() ? "" : (a.dir / a.meta.icon).string());
        json_object_array_add(list, item);
      }
      json_object_object_add(j.get(), "apps", list);
      emit(j);
    } else
      throw std::runtime_error("Invalid application command");
    return 0;
  } catch (const std::exception &e) {
    auto j = event("error");
    appSet(j.get(), "message", std::string(e.what()).substr(0, 512));
    emit(j);
    return 1;
  }
}
