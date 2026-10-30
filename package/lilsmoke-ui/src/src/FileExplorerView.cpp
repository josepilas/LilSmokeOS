// LilSmoke Explorer - lightweight file manager inspired by 351Files.

#include "FileExplorerView.h"
#include "Log.h"
#include "ProcessLaunch.h"
#include "MediaPaths.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#ifndef _WIN32
#include <sys/statvfs.h>
#endif
#include <unistd.h>

namespace lilsmoke {
namespace {

#ifdef _WIN32
#define lstat stat
#define LILSMOKE_MKDIR(path, mode) mkdir(path)
#else
#define LILSMOKE_MKDIR(path, mode) mkdir(path, mode)
#endif

static const char *kPromptChars =
    " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-+()[]{}!@#$%^&=,;:'\"";

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return s;
}

static std::string joinPath(const std::string &a, const std::string &b) {
    if (a.empty() || a == "/") return "/" + b;
    return a + "/" + b;
}

static std::string baseName(const std::string &path) {
    if (path == "/") return "/";
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static std::string dirName(const std::string &path) {
    if (path.empty() || path == "/") return "/";
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return "/";
    return path.substr(0, slash);
}

static bool startsWith(const std::string &s, const std::string &prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

static bool managedDrivePath(const std::string &path, bool container = false) {
    std::error_code error;
    const std::string p = std::filesystem::weakly_canonical(path, error).string();
    if (error) return true;
    if (p == "/lilos" || startsWith(p, "/lilos/")) return true;
    if (container && p == "/storage") return true;
    if (p == "/storage" || startsWith(p, "/storage/")) {
        if (std::filesystem::exists("/run/lilsmoke-storage/exporting", error)) return true;
        // Consult the kernel's backing-file association, not a stale UI setting.
        std::ifstream backing("/sys/kernel/config/usb_gadget/lilsmoke/functions/mass_storage.0/lun.0/file");
        std::string value;
        if (std::getline(backing, value) && !value.empty()) return true;
    }
    return false;
}

static bool hasExt(const std::string &path, const std::vector<std::string> &exts) {
    std::string lower = toLower(path);
    for (const auto &ext : exts) {
        if (lower.size() >= ext.size() &&
            lower.compare(lower.size() - ext.size(), ext.size(), ext) == 0)
            return true;
    }
    return false;
}

static bool isArchivePath(const std::string &path) {
    return hasExt(path, {
        ".zip", ".rar", ".7z", ".tar", ".tgz", ".tar.gz", ".tbz", ".tbz2",
        ".tar.bz2", ".txz", ".tar.xz", ".gz", ".bz2", ".xz", ".cab",
        ".cpio", ".rpm", ".deb", ".iso",
    });
}

static bool isImagePath(const std::string &path) {
    return hasExt(path, {".png", ".jpg", ".jpeg", ".webp", ".bmp"});
}

static bool isTextPath(const std::string &path) {
    return hasExt(path, {
        ".txt", ".md", ".cfg", ".conf", ".ini", ".map", ".xml", ".json",
        ".log", ".sh", ".cue", ".m3u", ".lst", ".nfo",
    });
}

static std::string shellQuote(const std::string &s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\"'\"'";
        else out += c;
    }
    out += "'";
    return out;
}

static std::string formatSize(long long size) {
    const char *units[] = {"B", "KB", "MB", "GB"};
    double v = (double)size;
    int unit = 0;
    while (v >= 1024.0 && unit < 3) {
        v /= 1024.0;
        unit++;
    }
    char buf[48];
    if (unit == 0) snprintf(buf, sizeof(buf), "%lld B", size);
    else snprintf(buf, sizeof(buf), "%.1f %s", v, units[unit]);
    return buf;
}

static std::string archiveStem(const std::string &path) {
    std::string name = baseName(path);
    std::string lower = toLower(name);
    for (const char *ext : {".tar.gz", ".tar.bz2", ".tar.xz", ".tgz", ".tbz2", ".txz",
                            ".zip", ".rar", ".7z", ".tar", ".gz", ".bz2", ".xz"}) {
        std::string e(ext);
        if (lower.size() >= e.size() &&
            lower.compare(lower.size() - e.size(), e.size(), e) == 0) {
            name.erase(name.size() - e.size());
            break;
        }
    }
    return name.empty() ? "extracted" : name;
}

static std::string uniquePath(const std::string &path) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) return path;
    std::string parent = dirName(path);
    std::string name = baseName(path);
    std::string stem = name;
    std::string ext;
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
        stem = name.substr(0, dot);
        ext = name.substr(dot);
    }
    for (int i = 1; i < 1000; i++) {
        std::ostringstream ss;
        ss << stem << " copy";
        if (i > 1) ss << " " << i;
        ss << ext;
        std::string candidate = joinPath(parent, ss.str());
        if (lstat(candidate.c_str(), &st) != 0) return candidate;
    }
    return path + ".copy";
}

static bool copyFile(const std::string &src, const std::string &dst) {
    struct stat st;
    if (stat(src.c_str(), &st) != 0) return false;
    int in = open(src.c_str(), O_RDONLY);
    if (in < 0) return false;
    int out = open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777);
    if (out < 0) {
        close(in);
        return false;
    }

    char buf[32768];
    bool ok = true;
    ssize_t n;
    while ((n = read(in, buf, sizeof(buf))) > 0) {
        char *p = buf;
        ssize_t left = n;
        while (left > 0) {
            ssize_t w = write(out, p, left);
            if (w <= 0) {
                ok = false;
                break;
            }
            p += w;
            left -= w;
        }
        if (!ok) break;
    }
    if (n < 0) ok = false;
    close(out);
    close(in);
    return ok;
}

static bool copyPath(const std::string &src, const std::string &dst);

static bool copyDir(const std::string &src, const std::string &dst) {
    if (startsWith(dst, src + "/")) return false;
    if (LILSMOKE_MKDIR(dst.c_str(), 0755) != 0 && errno != EEXIST) return false;
    DIR *d = opendir(src.c_str());
    if (!d) return false;
    bool ok = true;
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (!copyPath(joinPath(src, ent->d_name), joinPath(dst, ent->d_name))) {
            ok = false;
            break;
        }
    }
    closedir(d);
    return ok;
}

static bool copyPath(const std::string &src, const std::string &dst) {
    if (managedDrivePath(src) || managedDrivePath(dst)) return false;
    struct stat st;
    if (lstat(src.c_str(), &st) != 0) return false;
    if (S_ISDIR(st.st_mode)) return copyDir(src, dst);
    if (S_ISREG(st.st_mode)) return copyFile(src, dst);
    return false;
}

static bool deletePath(const std::string &path) {
    if (managedDrivePath(path, true)) return false;
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) return false;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path.c_str());
        if (!d) return false;
        bool ok = true;
        struct dirent *ent;
        while ((ent = readdir(d)) != nullptr) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;
            if (!deletePath(joinPath(path, ent->d_name))) ok = false;
        }
        closedir(d);
        return ok && rmdir(path.c_str()) == 0;
    }
    return unlink(path.c_str()) == 0;
}

static long long recursiveSize(const std::string &path, int &files, int &dirs) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) return 0;
    if (!S_ISDIR(st.st_mode)) {
        files++;
        return (long long)st.st_size;
    }
    dirs++;
    long long total = 0;
    DIR *d = opendir(path.c_str());
    if (!d) return 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        total += recursiveSize(joinPath(path, ent->d_name), files, dirs);
    }
    closedir(d);
    return total;
}

static std::string readCommand(const std::string &cmd) {
    FILE *fp = popen(cmd.c_str(), "r");
    if (!fp) return "";
    std::string out;
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
        if (out.size() < 65536) out += buf;
    }
    pclose(fp);
    return out;
}

static void splitLines(const std::string &s, std::vector<std::string> &out) {
    out.clear();
    std::istringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    if (out.empty()) out.push_back("");
}

static std::string fitText(Font &font, const std::string &text, int maxW) {
    int w = 0, h = 0;
    font.textSize(text, w, h);
    if (w <= maxW) return text;
    std::string out = text;
    while (!out.empty()) {
        out.pop_back();
        font.textSize(out + "...", w, h);
        if (w <= maxW) return out + "...";
    }
    return "...";
}

static void drawFit(Renderer &r, Font &font, const std::string &text,
                    int x, int y, int maxW, uint32_t color) {
    font.render(r, fitText(font, text, maxW), x, y, color);
}

static std::string pathFreeText(const std::string &path) {
#ifdef _WIN32
    (void)path;
    return "";
#else
    struct statvfs st;
    if (statvfs(path.c_str(), &st) != 0) return "";
    long long freeBytes = (long long)st.f_bavail * (long long)st.f_frsize;
    return "free " + formatSize(freeBytes);
#endif
}

}  // namespace

FileExplorerView::FileExplorerView(std::string startPath)
    : currentPath_(std::move(startPath)) {}

void FileExplorerView::onEnter() {
    refresh();
}

FileExplorerView::Entry *FileExplorerView::currentEntry() {
    if (entries_.empty() || selected_ < 0 || selected_ >= (int)entries_.size())
        return nullptr;
    return &entries_[selected_];
}

const FileExplorerView::Entry *FileExplorerView::currentEntry() const {
    if (entries_.empty() || selected_ < 0 || selected_ >= (int)entries_.size())
        return nullptr;
    return &entries_[selected_];
}

void FileExplorerView::focusPath(const std::string &path) {
    refresh();
    for (size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].path == path) { selected_ = static_cast<int>(i); scroll_ = selected_; break; }
}

void FileExplorerView::refresh() {
    entries_.clear();
    if (managedDrivePath(currentPath_)) {
        currentPath_ = "/storage";
        status_ = "LILDRIVE is shared with the PC";
    }
    DIR *d = opendir(currentPath_.c_str());
    if (!d) {
        currentPath_ = "/storage";
        d = opendir(currentPath_.c_str());
    }
    if (!d) {
        status_ = "Cannot open /storage";
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        Entry e;
        e.name = ent->d_name;
        e.path = joinPath(currentPath_, e.name);
        if (managedDrivePath(e.path)) continue;
        struct stat st;
        if (lstat(e.path.c_str(), &st) != 0) continue;
        e.isDir = S_ISDIR(st.st_mode);
        e.size = S_ISREG(st.st_mode) ? (long long)st.st_size : 0;
        e.mtime = (unsigned long)st.st_mtime;
        e.isArchive = !e.isDir && isArchivePath(e.path);
        e.isImage = !e.isDir && isImagePath(e.path);
        e.isText = !e.isDir && isTextPath(e.path);
        entries_.push_back(std::move(e));
    }
    closedir(d);

    std::sort(entries_.begin(), entries_.end(), [](const Entry &a, const Entry &b) {
        if (a.isDir != b.isDir) return a.isDir > b.isDir;
        return toLower(a.name) < toLower(b.name);
    });

    if (selected_ >= (int)entries_.size()) selected_ = (int)entries_.size() - 1;
    if (selected_ < 0) selected_ = 0;
    if (scroll_ > selected_) scroll_ = selected_;
    dirty_ = false;
}

std::vector<std::string> FileExplorerView::activePaths() const {
    if (!marked_.empty()) return std::vector<std::string>(marked_.begin(), marked_.end());
    const Entry *e = currentEntry();
    if (!e) return {};
    return {e->path};
}

void FileExplorerView::openParent() {
    if (currentPath_ == "/" || currentPath_ == "/storage") {
        if (onBack_) onBack_();
        return;
    }
    currentPath_ = dirName(currentPath_);
    selected_ = 0;
    scroll_ = 0;
    refresh();
}

void FileExplorerView::openCurrent() {
    Entry *e = currentEntry();
    if (!e) return;
    openEntry(*e);
}

void FileExplorerView::openEntry(const Entry &entry) {
    if (managedDrivePath(entry.path)) {
        showMessage("LILDRIVE is shared with the PC");
        return;
    }
    if (entry.isDir) {
        currentPath_ = entry.path;
        selected_ = 0;
        scroll_ = 0;
        refresh();
        return;
    }
    if (hasExt(entry.path,{".lilpck"})) {
        if (onApplicationPackage_) onApplicationPackage_(entry.path);
        else showMessage("Application installer unavailable");
        return;
    }
    if (entry.isArchive) {
        if (!openArchiveListing(entry.path))
            showMessage("No archive lister available for " + entry.name);
        return;
    }
    if (hasExt(entry.path, {".syspck"})) {
        runSystemPackage(entry.path, "check");
        return;
    }
    if (entry.isImage) {
        if (access("/usr/lib/lilsmoke/lilsmoke-media-open", X_OK) == 0) {
            requestProcessLaunch({"/usr/lib/lilsmoke/lilsmoke-media-open",
                                  {"lilsmoke-media-open", entry.path, currentPath_}});
            return;
        }
        imagePath_ = entry.path;
        imageFit_ = true;
        mode_ = Mode::ImageView;
        return;
    }
    if (media::kind(entry.path) != media::Kind::Unknown &&
        access("/usr/lib/lilsmoke/lilsmoke-media-open", X_OK) == 0) {
        if (media::kind(entry.path) == media::Kind::Playlist && media::collection(entry.path).empty()) {
            loadText(entry.path, false);
            return;
        }
        requestProcessLaunch({"/usr/lib/lilsmoke/lilsmoke-media-open",
                              {"lilsmoke-media-open", entry.path, currentPath_}});
        return;
    }
    if (entry.isText) {
        loadText(entry.path, false);
        return;
    }
    showMessage(propertiesFor(entry.path));
}

void FileExplorerView::openContextMenu() {
    menu_.clear();
    const Entry *e = currentEntry();
    bool hasEntry = e != nullptr;
    bool hasClip = !clipboard_.empty();
    bool selected = hasEntry && marked_.count(e->path) > 0;

    if (hasEntry) {
        menu_.push_back({"Open", MenuAction::Open});
        menu_.push_back({selected ? "Unselect" : "Select", MenuAction::ToggleSelect});
        menu_.push_back({"Copy", MenuAction::Copy});
        menu_.push_back({"Move", MenuAction::Cut});
    }
    if (hasClip) menu_.push_back({"Paste", MenuAction::Paste});
    if (hasEntry) {
        menu_.push_back({"Rename", MenuAction::Rename});
        menu_.push_back({"Delete", MenuAction::Delete});
    }
    menu_.push_back({"New folder", MenuAction::NewDir});
    menu_.push_back({"New file", MenuAction::NewFile});
    if (hasEntry && e->isArchive) {
        menu_.push_back({"List archive", MenuAction::Open});
        menu_.push_back({"Extract here", MenuAction::ExtractHere});
        menu_.push_back({"Extract to folder", MenuAction::ExtractToFolder});
    }
    if (hasEntry && hasExt(e->path, {".syspck"})) {
        menu_.push_back({"Validate package", MenuAction::CheckPackage});
        menu_.push_back({"Stage package", MenuAction::StagePackage});
        menu_.push_back({"Apply package settings", MenuAction::ApplyPackage});
    }
    if (hasEntry && e->isText) {
        menu_.push_back({"View text", MenuAction::ViewText});
        menu_.push_back({"Edit text", MenuAction::EditText});
    }
    if (hasEntry) menu_.push_back({"Properties", MenuAction::Properties});
    menu_.push_back({"Refresh", MenuAction::Refresh});
    menu_.push_back({"Go to /storage", MenuAction::GoStorage});
    menu_.push_back({"Go to /persist", MenuAction::GoPersist});
    menu_.push_back({"Go to /", MenuAction::GoRoot});
    menu_.push_back({"Cancel", MenuAction::Cancel});
    menuSelected_ = 0;
    mode_ = Mode::Menu;
}

void FileExplorerView::openTextMenu() {
    menu_ = {
        {"Save", MenuAction::SaveText},
        {"Edit line", MenuAction::EditText},
        {"Insert line", MenuAction::InsertLine},
        {"Delete line", MenuAction::DeleteLine},
        {"Close", MenuAction::Close},
    };
    menuSelected_ = 0;
    mode_ = Mode::TextMenu;
}

void FileExplorerView::runMenuAction(MenuAction action) {
    Entry *e = currentEntry();
    Mode menuMode = mode_;
    mode_ = Mode::Browser;
    switch (action) {
        case MenuAction::Open:
            openCurrent();
            break;
        case MenuAction::ToggleSelect:
            if (e) {
                if (marked_.count(e->path)) marked_.erase(e->path);
                else marked_.insert(e->path);
            }
            break;
        case MenuAction::Copy:
            clipboard_ = activePaths();
            clipboardCut_ = false;
            status_ = "Copied " + std::to_string((int)clipboard_.size()) + " item(s)";
            break;
        case MenuAction::Cut:
            clipboard_ = activePaths();
            clipboardCut_ = true;
            status_ = "Move queued for " + std::to_string((int)clipboard_.size()) + " item(s)";
            break;
        case MenuAction::Paste:
            if (!pasteClipboard(false))
                startConfirm(ConfirmAction::OverwritePaste, "Some files exist. Overwrite?");
            break;
        case MenuAction::Rename:
            if (e) startPrompt(PromptAction::Rename, "Rename", e->name);
            break;
        case MenuAction::Delete:
            startConfirm(ConfirmAction::Delete, "Delete selected item(s)?");
            break;
        case MenuAction::NewFile:
            startPrompt(PromptAction::NewFile, "New file", "New file.txt");
            break;
        case MenuAction::NewDir:
            startPrompt(PromptAction::NewDir, "New folder", "New folder");
            break;
        case MenuAction::ExtractHere:
            if (e && extractArchive(e->path, currentPath_)) {
                status_ = "Extracted";
                refresh();
            }
            break;
        case MenuAction::CheckPackage:
            if (e) runSystemPackage(e->path, "check");
            break;
        case MenuAction::StagePackage:
            if (e) runSystemPackage(e->path, "stage");
            break;
        case MenuAction::ApplyPackage:
            if (e) startConfirm(ConfirmAction::ApplyPackage, "Apply System Package settings?");
            break;
        case MenuAction::ExtractToFolder:
            if (e) {
                std::string dest = uniquePath(joinPath(currentPath_, archiveStem(e->path)));
                LILSMOKE_MKDIR(dest.c_str(), 0755);
                if (extractArchive(e->path, dest)) {
                    status_ = "Extracted to " + baseName(dest);
                    refresh();
                }
            }
            break;
        case MenuAction::ViewText:
            if (e) loadText(e->path, false);
            break;
        case MenuAction::EditText:
            if (menuMode == Mode::TextMenu) {
                mode_ = Mode::TextView;
                if (textLine_ >= 0 && textLine_ < (int)textLines_.size())
                    startPrompt(PromptAction::EditLine, "Edit line", textLines_[textLine_]);
            } else if (e) {
                loadText(e->path, true);
            }
            break;
        case MenuAction::Properties:
            if (e) showMessage(propertiesFor(e->path));
            break;
        case MenuAction::Refresh:
            refresh();
            break;
        case MenuAction::GoStorage:
            currentPath_ = "/storage";
            selected_ = scroll_ = 0;
            refresh();
            break;
        case MenuAction::GoPersist:
            currentPath_ = "/persist";
            selected_ = scroll_ = 0;
            refresh();
            break;
        case MenuAction::GoRoot:
            currentPath_ = "/";
            selected_ = scroll_ = 0;
            refresh();
            break;
        case MenuAction::SaveText:
            mode_ = Mode::TextView;
            if (saveText()) status_ = "Saved " + baseName(viewerPath_);
            break;
        case MenuAction::InsertLine:
            mode_ = Mode::TextView;
            if (textLine_ < 0) textLine_ = 0;
            if (textLine_ > (int)textLines_.size()) textLine_ = textLines_.size();
            textLines_.insert(textLines_.begin() + textLine_, "");
            textDirty_ = true;
            startPrompt(PromptAction::EditLine, "New line", "");
            break;
        case MenuAction::DeleteLine:
            mode_ = Mode::TextView;
            if (!textLines_.empty()) {
                textLines_.erase(textLines_.begin() + textLine_);
                if (textLines_.empty()) textLines_.push_back("");
                if (textLine_ >= (int)textLines_.size()) textLine_ = textLines_.size() - 1;
                textDirty_ = true;
            }
            break;
        case MenuAction::Close:
        case MenuAction::Cancel:
            mode_ = (action == MenuAction::Close) ? Mode::Browser : Mode::Browser;
            break;
    }
}

void FileExplorerView::startPrompt(PromptAction action, const std::string &title,
                                   const std::string &value) {
    promptAction_ = action;
    promptTitle_ = title;
    promptValue_ = value;
    promptCursor_ = promptValue_.size();
    returnMode_ = mode_;
    mode_ = Mode::Prompt;
}

void FileExplorerView::finishPrompt() {
    std::string value = promptValue_;
    mode_ = returnMode_;
    if (value.empty()) return;
    switch (promptAction_) {
        case PromptAction::Rename:
            renameCurrent(value);
            break;
        case PromptAction::NewFile:
            createFile(value);
            break;
        case PromptAction::NewDir:
            createDir(value);
            break;
        case PromptAction::EditLine:
            if (textLine_ >= 0 && textLine_ < (int)textLines_.size()) {
                textLines_[textLine_] = value;
                textDirty_ = true;
            }
            mode_ = Mode::TextView;
            break;
        case PromptAction::Search:
            for (int i = 0; i < (int)entries_.size(); i++) {
                if (toLower(entries_[i].name).find(toLower(value)) != std::string::npos) {
                    selected_ = i;
                    if (scroll_ > selected_) scroll_ = selected_;
                    break;
                }
            }
            break;
        default:
            break;
    }
    promptAction_ = PromptAction::None;
}

void FileExplorerView::startConfirm(ConfirmAction action, const std::string &title) {
    confirmAction_ = action;
    confirmTitle_ = title;
    returnMode_ = mode_;
    mode_ = Mode::Confirm;
}

void FileExplorerView::finishConfirm() {
    mode_ = returnMode_;
    switch (confirmAction_) {
        case ConfirmAction::Delete:
            deletePaths(activePaths());
            break;
        case ConfirmAction::OverwritePaste:
            pasteClipboard(true);
            break;
        case ConfirmAction::ApplyPackage:
            if (const Entry *entry = currentEntry()) runSystemPackage(entry->path, "apply");
            break;
        default:
            break;
    }
    confirmAction_ = ConfirmAction::None;
}

void FileExplorerView::showMessage(const std::string &message, Mode returnMode) {
    splitLines(message, textLines_);
    viewerPath_ = "Message";
    textTop_ = textLine_ = 0;
    textEditable_ = false;
    textDirty_ = false;
    returnMode_ = returnMode;
    mode_ = Mode::Message;
}

void FileExplorerView::runSystemPackage(const std::string &path, const char *action) {
    if (managedDrivePath(path) || !hasExt(path, {".syspck"})) {
        showMessage("System Package unavailable");
        return;
    }
    // Only this native command can interpret a package. No archive script runs.
    std::string command = "/usr/bin/lilsmoke-syspck " + std::string(action) + " " +
                          shellQuote(path) + " 2>&1";
    FILE *pipe = popen(command.c_str(), "r");
    if (!pipe) { showMessage("Cannot start System Package validator"); return; }
    char buffer[256];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        if (output.size() < 4096) output.append(buffer, std::min<size_t>(strlen(buffer), 4096 - output.size()));
    }
    int status = pclose(pipe);
    if (status == 0 && std::string(action) == "apply" && onPackageApplied_) onPackageApplied_();
    if (output.empty()) output = status == 0 ? "System Package complete" : "System Package failed";
    showMessage(output);
}

bool FileExplorerView::loadText(const std::string &path, bool editable) {
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) {
        showMessage("Cannot open " + path);
        return false;
    }
    textLines_.clear();
    std::string current;
    int c;
    size_t bytes = 0;
    bool truncated = false;
    while ((c = fgetc(fp)) != EOF) {
        bytes++;
        if (bytes > 262144) {
            truncated = true;
            break;
        }
        if (c == '\n') {
            textLines_.push_back(current);
            current.clear();
        } else if (c != '\r') {
            current.push_back((char)c);
        }
    }
    fclose(fp);
    if (!current.empty() || textLines_.empty()) textLines_.push_back(current);
    if (truncated) textLines_.push_back("[truncated at 256 KB]");
    viewerPath_ = path;
    textTop_ = textLine_ = 0;
    textEditable_ = editable && !truncated;
    textDirty_ = false;
    mode_ = Mode::TextView;
    return true;
}

bool FileExplorerView::saveText() {
    if (viewerPath_.empty() || !textEditable_) return false;
    FILE *fp = fopen(viewerPath_.c_str(), "wb");
    if (!fp) return false;
    for (size_t i = 0; i < textLines_.size(); i++) {
        fwrite(textLines_[i].data(), 1, textLines_[i].size(), fp);
        fputc('\n', fp);
    }
    fclose(fp);
    textDirty_ = false;
    return true;
}

bool FileExplorerView::openArchiveListing(const std::string &path) {
    std::string lower = toLower(path);
    std::string cmd;
    if (hasExt(lower, {".rar"})) {
        cmd = "if command -v unrar >/dev/null 2>&1; then unrar l " + shellQuote(path) +
              "; elif command -v 7za >/dev/null 2>&1; then 7za l " + shellQuote(path) +
              "; elif command -v 7z >/dev/null 2>&1; then 7z l " + shellQuote(path) +
              "; else exit 127; fi";
    } else {
        cmd = "if command -v 7za >/dev/null 2>&1; then 7za l " + shellQuote(path) +
              "; elif command -v 7z >/dev/null 2>&1; then 7z l " + shellQuote(path) +
              "; elif command -v 7zr >/dev/null 2>&1; then 7zr l " + shellQuote(path) +
              "; elif command -v tar >/dev/null 2>&1; then tar -tf " + shellQuote(path) +
              "; else exit 127; fi";
    }
    std::string out = readCommand(cmd + " 2>&1");
    if (out.empty()) return false;
    splitLines(out, textLines_);
    viewerPath_ = "Archive: " + baseName(path);
    textTop_ = textLine_ = 0;
    textEditable_ = false;
    textDirty_ = false;
    mode_ = Mode::TextView;
    return true;
}

bool FileExplorerView::extractArchive(const std::string &path, const std::string &destDir) {
    if (managedDrivePath(path) || managedDrivePath(destDir)) return false;
    LILSMOKE_MKDIR(destDir.c_str(), 0755);
    std::string lower = toLower(path);
    std::string cmd;
    if (hasExt(lower, {".rar"})) {
        cmd = "if command -v unrar >/dev/null 2>&1; then unrar x -o+ " +
              shellQuote(path) + " " + shellQuote(destDir + "/") +
              "; elif command -v 7za >/dev/null 2>&1; then 7za x -y -o" +
              shellQuote(destDir) + " " + shellQuote(path) +
              "; elif command -v 7z >/dev/null 2>&1; then 7z x -y -o" +
              shellQuote(destDir) + " " + shellQuote(path) + "; else exit 127; fi";
    } else if (hasExt(lower, {".tar.gz", ".tgz"})) {
        cmd = "if command -v tar >/dev/null 2>&1; then tar -xzf " +
              shellQuote(path) + " -C " + shellQuote(destDir) +
              "; elif command -v 7za >/dev/null 2>&1; then 7za x -y -o" +
              shellQuote(destDir) + " " + shellQuote(path) + "; else exit 127; fi";
    } else if (hasExt(lower, {".tar.bz2", ".tbz", ".tbz2"})) {
        cmd = "if command -v tar >/dev/null 2>&1; then tar -xjf " +
              shellQuote(path) + " -C " + shellQuote(destDir) +
              "; elif command -v 7za >/dev/null 2>&1; then 7za x -y -o" +
              shellQuote(destDir) + " " + shellQuote(path) + "; else exit 127; fi";
    } else if (hasExt(lower, {".tar.xz", ".txz"})) {
        cmd = "if command -v tar >/dev/null 2>&1; then tar -xJf " +
              shellQuote(path) + " -C " + shellQuote(destDir) +
              "; elif command -v 7za >/dev/null 2>&1; then 7za x -y -o" +
              shellQuote(destDir) + " " + shellQuote(path) + "; else exit 127; fi";
    } else if (hasExt(lower, {".tar"})) {
        cmd = "tar -xf " + shellQuote(path) + " -C " + shellQuote(destDir);
    } else {
        cmd = "if command -v 7za >/dev/null 2>&1; then 7za x -y -o" +
              shellQuote(destDir) + " " + shellQuote(path) +
              "; elif command -v 7z >/dev/null 2>&1; then 7z x -y -o" +
              shellQuote(destDir) + " " + shellQuote(path) +
              "; elif command -v 7zr >/dev/null 2>&1; then 7zr x -y -o" +
              shellQuote(destDir) + " " + shellQuote(path) +
              "; elif command -v unzip >/dev/null 2>&1; then unzip -o " + shellQuote(path) +
              " -d " + shellQuote(destDir) +
              "; else tar -xf " + shellQuote(path) + " -C " + shellQuote(destDir) + "; fi";
    }

    int rc = std::system((cmd + " >/tmp/lilsmoke-explorer-archive.log 2>&1").c_str());
    if (rc != 0) {
        showMessage("Extract failed. See /tmp/lilsmoke-explorer-archive.log");
        return false;
    }
    return true;
}

bool FileExplorerView::pasteClipboard(bool overwrite) {
    if (managedDrivePath(currentPath_)) return false;
    if (clipboard_.empty()) return true;
    bool collision = false;
    bool ok = true;
    for (const auto &src : clipboard_) {
        std::string dst = joinPath(currentPath_, baseName(src));
        struct stat st;
        if (lstat(dst.c_str(), &st) == 0) {
            if (!overwrite) {
                collision = true;
                continue;
            }
            deletePath(dst);
        } else {
            dst = uniquePath(dst);
        }
        if (clipboardCut_) {
            if (rename(src.c_str(), dst.c_str()) != 0) {
                if (copyPath(src, dst)) deletePath(src);
                else ok = false;
            }
        } else if (!copyPath(src, dst)) {
            ok = false;
        }
    }
    if (collision) return false;
    if (ok && clipboardCut_) clipboard_.clear();
    marked_.clear();
    refresh();
    status_ = ok ? "Paste complete" : "Paste failed";
    return ok;
}

bool FileExplorerView::deletePaths(const std::vector<std::string> &paths) {
    bool ok = true;
    for (const auto &path : paths) {
        if (path == "/" || path == "/storage" || path == "/persist" ||
            managedDrivePath(path, true)) {
            ok = false;
            continue;
        }
        if (!deletePath(path)) ok = false;
    }
    marked_.clear();
    refresh();
    status_ = ok ? "Deleted" : "Delete failed";
    return ok;
}

bool FileExplorerView::renameCurrent(const std::string &newName) {
    Entry *e = currentEntry();
    if (!e || newName.find('/') != std::string::npos) return false;
    std::string dst = joinPath(currentPath_, newName);
    if (managedDrivePath(e->path, true) || managedDrivePath(dst, true)) return false;
    bool ok = rename(e->path.c_str(), dst.c_str()) == 0;
    status_ = ok ? "Renamed" : "Rename failed";
    refresh();
    return ok;
}

bool FileExplorerView::createFile(const std::string &name) {
    if (managedDrivePath(currentPath_)) return false;
    if (name.find('/') != std::string::npos) return false;
    std::string path = uniquePath(joinPath(currentPath_, name));
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    bool ok = fd >= 0;
    if (fd >= 0) close(fd);
    status_ = ok ? "File created" : "Create file failed";
    refresh();
    return ok;
}

bool FileExplorerView::createDir(const std::string &name) {
    if (managedDrivePath(currentPath_)) return false;
    if (name.find('/') != std::string::npos) return false;
    std::string path = uniquePath(joinPath(currentPath_, name));
    bool ok = LILSMOKE_MKDIR(path.c_str(), 0755) == 0;
    status_ = ok ? "Folder created" : "Create folder failed";
    refresh();
    return ok;
}

std::string FileExplorerView::propertiesFor(const std::string &path) const {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) return "Cannot stat " + path;
    int files = 0, dirs = 0;
    long long total = recursiveSize(path, files, dirs);
    std::ostringstream ss;
    ss << "Name: " << baseName(path) << "\n";
    ss << "Path: " << path << "\n";
    ss << "Type: " << (S_ISDIR(st.st_mode) ? "directory" : "file") << "\n";
    ss << "Size: " << formatSize(S_ISDIR(st.st_mode) ? total : (long long)st.st_size) << "\n";
    if (S_ISDIR(st.st_mode)) ss << "Contains: " << files << " files, " << dirs << " folders\n";
    ss << "Mode: " << std::oct << (st.st_mode & 0777) << std::dec << "\n";
    return ss.str();
}

View *FileExplorerView::handleInput(InputButton btn, InputEvent evt) {
    if (evt != InputEvent::Pressed && evt != InputEvent::Repeat) return nullptr;
    if (evt == InputEvent::Repeat && (btn == InputButton::A || btn == InputButton::Start))
        return nullptr;

    if (dirty_) refresh();

    if (mode_ == Mode::Prompt) {
        if (btn == InputButton::B) {
            mode_ = returnMode_;
            return nullptr;
        }
        if (btn == InputButton::A || btn == InputButton::Start) {
            finishPrompt();
            return nullptr;
        }
        if (btn == InputButton::Left) {
            if (promptCursor_ > 0) promptCursor_--;
            return nullptr;
        }
        if (btn == InputButton::Right) {
            if (promptCursor_ < promptValue_.size()) promptCursor_++;
            return nullptr;
        }
        if (btn == InputButton::X) {
            promptValue_.insert(promptCursor_, 1, ' ');
            if (promptCursor_ + 1 < promptValue_.size()) promptCursor_++;
            return nullptr;
        }
        if (btn == InputButton::Y) {
            if (!promptValue_.empty()) {
                promptValue_.erase(promptCursor_, 1);
                if (promptCursor_ >= promptValue_.size() && promptCursor_ > 0) promptCursor_--;
            }
            return nullptr;
        }
        if (btn == InputButton::Up || btn == InputButton::Down) {
            if (promptValue_.empty()) promptValue_ = "a";
            if (promptCursor_ >= promptValue_.size()) promptCursor_ = promptValue_.size() - 1;
            const char *p = strchr(kPromptChars, promptValue_[promptCursor_]);
            int idx = p ? (int)(p - kPromptChars) : 1;
            int len = (int)strlen(kPromptChars);
            idx += (btn == InputButton::Up) ? 1 : -1;
            if (idx < 0) idx = len - 1;
            if (idx >= len) idx = 0;
            promptValue_[promptCursor_] = kPromptChars[idx];
            return nullptr;
        }
        return nullptr;
    }

    if (mode_ == Mode::Confirm) {
        if (btn == InputButton::A || btn == InputButton::Start) finishConfirm();
        else if (btn == InputButton::B) mode_ = returnMode_;
        return nullptr;
    }

    if (mode_ == Mode::Menu || mode_ == Mode::TextMenu) {
        if (btn == InputButton::B) {
            mode_ = (mode_ == Mode::TextMenu) ? Mode::TextView : Mode::Browser;
            return nullptr;
        }
        if (btn == InputButton::Up && menuSelected_ > 0) menuSelected_--;
        else if (btn == InputButton::Down && menuSelected_ < (int)menu_.size() - 1) menuSelected_++;
        else if ((btn == InputButton::A || btn == InputButton::Start) && !menu_.empty())
            runMenuAction(menu_[menuSelected_].action);
        return nullptr;
    }

    if (mode_ == Mode::TextView || mode_ == Mode::Message) {
        int visible = 19;
        if (btn == InputButton::B) {
            if (textDirty_ && textEditable_) {
                openTextMenu();
            } else {
                mode_ = Mode::Browser;
            }
            return nullptr;
        }
        if (btn == InputButton::X && textEditable_) {
            openTextMenu();
            return nullptr;
        }
        if ((btn == InputButton::A || btn == InputButton::Start) && textEditable_) {
            if (textLine_ >= 0 && textLine_ < (int)textLines_.size())
                startPrompt(PromptAction::EditLine, "Edit line", textLines_[textLine_]);
            return nullptr;
        }
        if (btn == InputButton::Up && textLine_ > 0) textLine_--;
        else if (btn == InputButton::Down && textLine_ < (int)textLines_.size() - 1) textLine_++;
        else if (btn == InputButton::L1) textLine_ = std::max(0, textLine_ - visible);
        else if (btn == InputButton::R1) textLine_ = std::min((int)textLines_.size() - 1, textLine_ + visible);
        if (textLine_ < textTop_) textTop_ = textLine_;
        if (textLine_ >= textTop_ + visible) textTop_ = textLine_ - visible + 1;
        return nullptr;
    }

    if (mode_ == Mode::ImageView) {
        if (btn == InputButton::B) mode_ = Mode::Browser;
        else if (btn == InputButton::A || btn == InputButton::Start) imageFit_ = !imageFit_;
        else if (btn == InputButton::X) showMessage(propertiesFor(imagePath_), Mode::ImageView);
        return nullptr;
    }

    int visibleRows = 17;
    switch (btn) {
        case InputButton::Up:
            if (selected_ > 0) selected_--;
            break;
        case InputButton::Down:
            if (selected_ < (int)entries_.size() - 1) selected_++;
            break;
        case InputButton::L1:
        case InputButton::L2:
            selected_ = std::max(0, selected_ - visibleRows);
            break;
        case InputButton::R1:
        case InputButton::R2:
            selected_ = std::min((int)entries_.size() - 1, selected_ + visibleRows);
            break;
        case InputButton::Left:
        case InputButton::B:
            openParent();
            break;
        case InputButton::Right:
        case InputButton::A:
        case InputButton::Start:
            openCurrent();
            break;
        case InputButton::X:
            openContextMenu();
            break;
        case InputButton::Y:
            if (Entry *e = currentEntry()) {
                if (marked_.count(e->path)) marked_.erase(e->path);
                else marked_.insert(e->path);
            }
            break;
        case InputButton::Select:
            startPrompt(PromptAction::Search, "Find", "");
            break;
        default:
            break;
    }
    if (selected_ < scroll_) scroll_ = selected_;
    if (selected_ >= scroll_ + visibleRows) scroll_ = selected_ - visibleRows + 1;
    return nullptr;
}

void FileExplorerView::handleText(const std::string &text) {
    if (mode_ != Mode::Prompt) return;
    promptCursor_ = std::min(promptCursor_, promptValue_.size());
    for (unsigned char c : text) {
        if (c == '\b') {
            if (promptCursor_) promptValue_.erase(--promptCursor_, 1);
        } else if (c == 127) {
            if (promptCursor_ < promptValue_.size()) promptValue_.erase(promptCursor_, 1);
        } else if (c >= 32 && c < 127 && promptValue_.size() < 255) {
            promptValue_.insert(promptCursor_++, 1, static_cast<char>(c));
        }
    }
}

View *FileExplorerView::handlePointer(const PointerEvent &event) {
    bool hit = false;
    if (event.type == PointerEventType::Moved ||
        event.type == PointerEventType::Pressed) {
        if (mode_ == Mode::Browser && event.x >= 8 && event.x < renderWidth_ - 8 &&
            event.y >= 42 && event.y < 42 + ((renderHeight_ - 76) / 22) * 22) {
            int index = scroll_ + (event.y - 42) / 22;
            if (index >= 0 && index < static_cast<int>(entries_.size())) {
                selected_ = index;
                hit = true;
            }
        } else if ((mode_ == Mode::Menu || mode_ == Mode::TextMenu) &&
                   event.x >= (renderWidth_ - 330) / 2 &&
                   event.x < (renderWidth_ - 330) / 2 + 330 &&
                   event.y >= 84 && event.y < 84 + std::max(1, (renderHeight_ - 112) / 24) * 24) {
            int index = menuScroll_ + (event.y - 84) / 24;
            if (index >= 0 && index < static_cast<int>(menu_.size())) {
                menuSelected_ = index;
                hit = true;
            }
        } else if ((mode_ == Mode::TextView || mode_ == Mode::Message) &&
                   event.y >= 60 && event.y < renderHeight_ - 28) {
            int index = textTop_ + (event.y - 60) / 20;
            if (index >= 0 && index < static_cast<int>(textLines_.size()))
                textLine_ = index;
        }
    }
    if (event.type == PointerEventType::Pressed) {
        if (event.button == PointerButton::Secondary && mode_ == Mode::Browser) {
            openContextMenu();
            return nullptr;
        }
        if (event.button == PointerButton::Primary) {
            if ((mode_ == Mode::Browser || mode_ == Mode::Menu || mode_ == Mode::TextMenu) && !hit)
                return nullptr;
            // Confirmation must be explicit; clicking its backdrop never accepts.
            if (mode_ == Mode::Confirm) {
                if (event.y >= 222 && event.y < 256) {
                    if (event.x >= 82 && event.x < 145) finishConfirm();
                    else if (event.x >= 146 && event.x < 218) mode_ = returnMode_;
                }
                return nullptr;
            }
            if (mode_ == Mode::Prompt) {
                if (event.y >= 260 && event.y < 288) {
                    if (event.x >= 78 && event.x < 170) finishPrompt();
                    else if (event.x >= 171 && event.x < 290) mode_ = returnMode_;
                }
                return nullptr;
            }
        }
    }
    return View::handlePointer(event);
}

CursorKind FileExplorerView::cursorAt(int x, int y) const {
    if (mode_ == Mode::Prompt) return CursorKind::Text;
    if (mode_ == Mode::Confirm) return CursorKind::Help;
    if (mode_ == Mode::ImageView) return CursorKind::Precision;
    if (mode_ == Mode::TextView || mode_ == Mode::Message)
        return CursorKind::Text;
    if (mode_ == Mode::Menu || mode_ == Mode::TextMenu)
        return CursorKind::Hand;
    if (mode_ != Mode::Browser) return CursorKind::Normal;
    if (y < 42 || y >= renderHeight_ - 34)
        return y >= renderHeight_ - 24 ? CursorKind::Help
                                       : CursorKind::Normal;
    int index = scroll_ + (y - 42) / 22;
    if (index < 0 || index >= static_cast<int>(entries_.size()))
        return CursorKind::NotAllowed;
    if (clipboardCut_) return CursorKind::Move;
    const Entry &entry = entries_[static_cast<size_t>(index)];
    if (entry.isArchive) return CursorKind::Link;
    if (entry.isText) return CursorKind::Text;
    if (entry.isImage) return CursorKind::Precision;
    return CursorKind::Hand;
}

void FileExplorerView::render(Renderer &r, Font &font, const Theme &theme) {
    (void)theme;
    renderWidth_ = r.width();
    renderHeight_ = r.height();
    r.clear(0x20242AFF);
    r.fillRect(0, 0, r.width(), 34, 0x183047FF);
    font.render(r, "LilSmoke Explorer", 12, 8, 0xF2E8C9FF);
    drawFit(r, font, currentPath_, 190, 8, r.width() - 330, 0xC9D7E8FF);
    std::string freeText = pathFreeText(currentPath_);
    if (!freeText.empty()) drawFit(r, font, freeText, r.width() - 128, 8, 116, 0x9FB2C8FF);

    if (mode_ == Mode::Browser) {
        int y = 42;
        int rowH = 22;
        int visible = (r.height() - 76) / rowH;
        int end = std::min((int)entries_.size(), scroll_ + visible);
        if (entries_.empty()) {
            font.render(r, "Empty folder", 20, y + 20, 0xC9D7E8FF);
        }
        for (int i = scroll_; i < end; i++) {
            bool sel = i == selected_;
            const Entry &e = entries_[i];
            uint32_t bg = sel ? 0x28533AFF : ((i & 1) ? 0x262B32FF : 0x22272EFF);
            r.fillRect(8, y, r.width() - 16, rowH - 1, bg);
            const char *kind = e.isDir ? "[D]" : (e.isArchive ? "[A]" : (e.isImage ? "[I]" : "[F]"));
            font.render(r, marked_.count(e.path) ? "*" : " ", 14, y + 3, 0xF2E8C9FF);
            font.render(r, kind, 30, y + 3, e.isDir ? 0x8FD7A5FF : 0x9FB2C8FF);
            drawFit(r, font, e.name, 68, y + 3, r.width() - 195, 0xF2E8C9FF);
            if (!e.isDir) {
                std::string size = formatSize(e.size);
                int tw = 0, th = 0;
                font.textSize(size, tw, th);
                font.render(r, size, r.width() - tw - 18, y + 3, 0x9FB2C8FF);
            }
            y += rowH;
        }
        if (!status_.empty()) drawFit(r, font, status_, 12, r.height() - 42, r.width() - 24, 0xF2E8C9CC);
        r.fillRect(0, r.height() - 24, r.width(), 24, 0x183047FF);
        font.render(r, "A open  B back  X menu  Y mark  Select find  L/R page", 12, r.height() - 19, 0xC9D7E8FF);
        return;
    }

    if (mode_ == Mode::Menu || mode_ == Mode::TextMenu) {
        int boxW = std::min(330, r.width() - 16);
        int visible = std::max(1, (r.height() - 112) / 24);
        if (menuSelected_ < menuScroll_) menuScroll_ = menuSelected_;
        if (menuSelected_ >= menuScroll_ + visible) menuScroll_ = menuSelected_ - visible + 1;
        int boxH = 34 + std::min(visible, (int)menu_.size()) * 24;
        int x = (r.width() - boxW) / 2;
        int y = 54;
        r.fillRect(x, y, boxW, boxH, 0x15191FCC);
        r.drawRect(x, y, boxW, boxH, 0xF2E8C9FF);
        font.render(r, mode_ == Mode::TextMenu ? "Text actions" : "File actions", x + 10, y + 8, 0xF2E8C9FF);
        int rowY = y + 32;
        for (int i = menuScroll_; i < std::min((int)menu_.size(), menuScroll_ + visible); i++) {
            if (i == menuSelected_) r.fillRect(x + 6, rowY - 2, boxW - 12, 22, 0x28533AFF);
            drawFit(r, font, menu_[i].label, x + 14, rowY, boxW - 28, 0xC9D7E8FF);
            rowY += 24;
        }
        return;
    }

    if (mode_ == Mode::Prompt) {
        r.fillRect(62, 138, r.width() - 124, 180, 0x15191FE6);
        r.drawRect(62, 138, r.width() - 124, 180, 0xF2E8C9FF);
        font.render(r, promptTitle_, 78, 154, 0xF2E8C9FF);
        r.fillRect(78, 190, r.width() - 156, 28, 0x20242AFF);
        drawFit(r, font, promptValue_, 86, 196, r.width() - 172, 0xC9D7E8FF);
        int cx = 86;
        if (!promptValue_.empty()) {
            std::string left = promptValue_.substr(0, promptCursor_);
            int w = 0, h = 0;
            font.textSize(left, w, h);
            cx += w;
        }
        r.fillRect(std::min(cx, r.width() - 96), 219, 10, 2, 0xF2E8C9FF);
        font.render(r, "Up/Down char  Left/Right cursor  X space  Y delete", 78, 242, 0x9FB2C8FF);
        font.render(r, "A accept  B cancel", 78, 266, 0x9FB2C8FF);
        return;
    }

    if (mode_ == Mode::Confirm) {
        r.fillRect(70, 160, r.width() - 140, 120, 0x15191FE6);
        r.drawRect(70, 160, r.width() - 140, 120, 0xF2E8C9FF);
        drawFit(r, font, confirmTitle_, 88, 182, r.width() - 176, 0xF2E8C9FF);
        font.render(r, "A yes    B no", 88, 230, 0xC9D7E8FF);
        return;
    }

    if (mode_ == Mode::TextView || mode_ == Mode::Message) {
        drawFit(r, font, viewerPath_ + (textDirty_ ? " *" : ""), 12, 38, r.width() - 24, 0xC9D7E8FF);
        int rowH = 20;
        int y = 62;
        int visible = (r.height() - 92) / rowH;
        int end = std::min((int)textLines_.size(), textTop_ + visible);
        for (int i = textTop_; i < end; i++) {
            bool sel = textEditable_ && i == textLine_;
            if (sel) r.fillRect(8, y - 2, r.width() - 16, rowH, 0x28533AFF);
            drawFit(r, font, textLines_[i], 14, y, r.width() - 28, sel ? 0xF2E8C9FF : 0xC9D7E8FF);
            y += rowH;
        }
        r.fillRect(0, r.height() - 24, r.width(), 24, 0x183047FF);
        if (textEditable_)
            font.render(r, "A edit line  X actions  B back", 12, r.height() - 19, 0xC9D7E8FF);
        else
            font.render(r, "Up/Down scroll  B back", 12, r.height() - 19, 0xC9D7E8FF);
        return;
    }

    if (mode_ == Mode::ImageView) {
        SDL_Texture *tex = r.loadImage(imagePath_);
        drawFit(r, font, imagePath_, 12, 38, r.width() - 24, 0xC9D7E8FF);
        if (!tex) {
            font.render(r, "Cannot load image", 20, 84, 0xF2E8C9FF);
        } else {
            int iw = 0, ih = 0;
            SDL_QueryTexture(tex, nullptr, nullptr, &iw, &ih);
            int maxW = r.width() - 32;
            int maxH = r.height() - 92;
            int dw = iw;
            int dh = ih;
            if (imageFit_ && iw > 0 && ih > 0) {
                float scale = std::min((float)maxW / (float)iw, (float)maxH / (float)ih);
                if (scale > 1.0f) scale = 1.0f;
                dw = std::max(1, (int)(iw * scale));
                dh = std::max(1, (int)(ih * scale));
            }
            r.drawTexture(tex, (r.width() - dw) / 2, 70 + (maxH - dh) / 2, dw, dh);
        }
        r.fillRect(0, r.height() - 24, r.width(), 24, 0x183047FF);
        font.render(r, "A fit/native  X properties  B back", 12, r.height() - 19, 0xC9D7E8FF);
    }
}

}  // namespace lilsmoke
