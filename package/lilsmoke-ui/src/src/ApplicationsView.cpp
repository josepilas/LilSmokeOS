#include "ApplicationsView.h"
#include "ProcessLaunch.h"
#include "Log.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <webp/decode.h>

namespace lilsmoke {
namespace {
constexpr uint32_t Ink = 0x243D50FF, Blue = 0x168CEBFF, Muted = 0x657F93FF,
                   White = 0xFFFFFFFF;
static void text(Renderer &r, Font &f, const std::string &s, int x, int y,
                 int w, int h, int size = 16, uint32_t color = Ink,
                 const char *align = "left", bool wrap = false) {
  f.renderStyled(r, s, x, y, w, h, color, "", size, align, "center", wrap);
}
static void background(ApplicationArtwork &art, Renderer &r, Font &f,
                       const std::string &title) {
  art.background(r);
  r.fillRect(0, 0, r.width(), 51, 0xFFFFFFA8);
  r.fillRect(18, 50, r.width() - 36, 1, 0xAECFE280);
  text(r, f, title, 18, 10, r.width() - 36, 31, 21, 0x315C83FF);
  r.fillRect(0, r.height() - 45, r.width(), 1, 0xBAD9EA80);
}
static void button(ApplicationArtwork &art, Renderer &r, Font &f,
                   const std::string &label, int x, int y, int w, int h,
                   bool selected, bool danger = false) {
  art.panel(r, x, y, w, h,
            selected ? (danger ? ApplicationSurface::Danger
                               : ApplicationSurface::SelectedCommand)
                     : ApplicationSurface::Command);
  text(r, f, label, x + 4, y, w - 8, h, 16, selected ? White : Ink, "center");
}
static bool hit(const PointerEvent &p, int x, int y, int w, int h) {
  return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h;
}
struct DialogLayout {
  int x, y, width, height, padding, step, buttonHeight, confirmY;
};
static DialogLayout dialogLayout(int width, int height) {
  bool compact = height < 330;
  int w = std::min(420, width - 32), h = compact ? 132 : 166;
  return {(width - w) / 2,
          std::max(58, (height - 44 - h + 52) / 2),
          w,
          h,
          compact ? 8 : 16,
          compact ? 38 : 44,
          compact ? 32 : 36,
          compact ? 88 : 104};
}
static uint32_t be32(const unsigned char *p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | p[3];
}
static uint32_t le32(const unsigned char *p) {
  return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
         (uint32_t(p[3]) << 24);
}
static SDL_Texture *iconTexture(Renderer &r, const std::string &path) {
  if (path.empty())
    return nullptr;
  std::error_code error;
  auto bytes = std::filesystem::file_size(path, error);
  if (error || bytes > 2 * 1024 * 1024)
    return nullptr;
  static std::map<std::string, bool> validated;
  auto time = std::filesystem::last_write_time(path, error);
  if (error)
    return nullptr;
  auto key = path + ":" + std::to_string(bytes) + ":" +
             std::to_string(time.time_since_epoch().count());
  auto known = validated.find(key);
  if (known != validated.end())
    return known->second ? r.loadImage(path, true) : nullptr;
  if (validated.size() > 64)
    validated.clear();
  std::ifstream in(path, std::ios::binary);
  std::array<unsigned char, 65536> header{};
  in.read(reinterpret_cast<char *>(header.data()), header.size());
  size_t n = in.gcount();
  int w = 0, h = 0, maxSide = 512;
  if (n >= 24 && !std::memcmp(header.data(), "\x89PNG\r\n\x1a\n", 8)) {
    w = be32(header.data() + 16);
    h = be32(header.data() + 20);
  } else if (n >= 30 && !std::memcmp(header.data(), "RIFF", 4) &&
             !std::memcmp(header.data() + 8, "WEBP", 4)) {
    WebPGetInfo(header.data(), n, &w, &h);
    maxSide = 2048;
  }
  else if (n >= 26 && header[0] == 'B' && header[1] == 'M') {
    w = le32(header.data() + 18);
    auto raw = le32(header.data() + 22);
    h = raw == 0x80000000u ? 0 : std::abs(static_cast<int>(raw));
  } else if (n >= 4 && header[0] == 255 && header[1] == 216) {
    size_t at = 2;
    while (at + 4 < n) {
      if (header[at++] != 255)
        break;
      unsigned marker = header[at++];
      if (marker == 216 || marker == 217)
        continue;
      size_t length = (header[at] << 8) | header[at + 1];
      if (length < 2 || at + length > n)
        break;
      if ((marker >= 192 && marker <= 195) ||
          (marker >= 197 && marker <= 199) ||
          (marker >= 201 && marker <= 203) ||
          (marker >= 205 && marker <= 207)) {
        if (length >= 7) {
          h = (header[at + 3] << 8) | header[at + 4];
          w = (header[at + 5] << 8) | header[at + 6];
        }
        break;
      }
      at += length;
    }
  }
  bool valid = w > 0 && h > 0 && w <= maxSide && h <= maxSide;
  validated[key] = valid;
  if (!valid)
    return nullptr;
  return r.loadImage(path, true);
}
static void icon(Renderer &r, Font &f, const std::string &path, int x, int y,
                 int size) {
  if (auto image = iconTexture(r, path)) {
    int w, h;
    if (!r.textureSize(image, w, h))
      return;
    float ratio = std::min(float(size) / w, float(size) / h);
    int iw = w * ratio, ih = h * ratio;
    r.drawTexture(image, x + (size - iw) / 2, y + (size - ih) / 2, iw, ih);
  } else
    text(r, f, "?", x, y, size, size, 48, 0x8BA7BDFF, "center");
}
static void cube(ApplicationArtwork &art, Renderer &r, Font &font,
                 const std::string &path, int cx, int cy, int size) {
  bool valid = iconTexture(r, path) != nullptr;
  art.cube(r, path, valid, cx, cy, size);
  if (!valid)
    text(r, font, "?", cx - size / 4, cy - size / 3, size / 2, size * 2 / 3, 48,
         0xF4FBFFFF, "center");
}
} // namespace
ApplicationJob::~ApplicationJob() { cancel(); }
void ApplicationJob::cancel() {
  if (pid_ > 0) {
    kill(pid_, SIGTERM);
    for (int i = 0; i < 50; ++i) {
      if (waitpid(pid_, nullptr, WNOHANG) == pid_) {
        pid_ = -1;
        break;
      }
      usleep(10000);
    }
    if (pid_ > 0) {
      kill(pid_, SIGKILL);
      while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {
      }
      pid_ = -1;
    }
  }
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
  pending_.clear();
}
void ApplicationJob::start(const std::vector<std::string> &args) {
  cancel();
  messages.clear();
  eof_ = false;
  int fds[2];
  if (pipe(fds))
    throw std::runtime_error("Cannot start application service");
  const char *env = getenv("LILSMOKE_APPS_HELPER");
  std::string helper = env ? env : "/usr/lib/lilsmoke/lilsmoke-apps";
  pid_ = fork();
  if (pid_ == 0) {
    dup2(fds[1], STDOUT_FILENO);
    close(fds[0]);
    close(fds[1]);
    int null = open("/dev/null", O_RDWR);
    if (null >= 0) {
      dup2(null, STDIN_FILENO);
      dup2(null, STDERR_FILENO);
      close(null);
    }
    std::vector<char *> argv{const_cast<char *>(helper.c_str())};
    for (auto &s : args)
      argv.push_back(const_cast<char *>(s.c_str()));
    argv.push_back(nullptr);
    execv(helper.c_str(), argv.data());
    _exit(127);
  }
  close(fds[1]);
  if (pid_ < 0) {
    close(fds[0]);
    throw std::runtime_error("Cannot fork application service");
  }
  fd_ = fds[0];
  fcntl(fd_, F_SETFL, O_NONBLOCK);
}
void ApplicationJob::poll() {
  if (pid_ < 0)
    return;
  char buffer[4096];
  for (int i = 0; i < 32 && fd_ >= 0; ++i) {
    ssize_t n = read(fd_, buffer, sizeof(buffer));
    if (n > 0)
      pending_.append(buffer, n);
    else {
      if (!n)
        eof_ = true;
      break;
    }
  }
  if (pending_.size() > 2 * 1024 * 1024) {
    cancel();
    auto e = appJson(json_object_new_object());
    appSet(e.get(), "type", "error");
    appSet(e.get(), "message", "Application service response too large");
    messages.push_back(e);
    return;
  }
  size_t end;
  while ((end = pending_.find('\n')) != std::string::npos) {
    auto line = pending_.substr(0, end);
    pending_.erase(0, end + 1);
    try {
      messages.push_back(parseAppJson(line));
    } catch (...) {
      auto e = appJson(json_object_new_object());
      appSet(e.get(), "type", "error");
      appSet(e.get(), "message", "Invalid application service response");
      messages.push_back(e);
    }
    if (messages.size() > 128)
      messages.pop_front();
  }
  int status = 0;
  if (eof_ && waitpid(pid_, &status, WNOHANG) == pid_) {
    pid_ = -1;
    close(fd_);
    fd_ = -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      auto e = appJson(json_object_new_object());
      appSet(e.get(), "type", "error");
      appSet(e.get(), "message", "Application operation failed");
      if (messages.empty() ||
          appString(messages.back().get(), "type") != "error")
        messages.push_back(e);
    }
  }
}
ApplicationInstallerView::ApplicationInstallerView(std::string p)
    : package_(std::move(p)) {}
ApplicationInstallerView::~ApplicationInstallerView() {
  job_.cancel();
  if (!preview_.empty()) {
    std::error_code e;
    std::filesystem::remove_all(preview_, e);
  }
}
void ApplicationInstallerView::onEnter() {
  if (entered_)
    return;
  entered_ = true;
  char temp[] = "/tmp/lilsmoke-app-preview-XXXXXX";
  char *p = mkdtemp(temp);
  if (!p) {
    state_ = State::Error;
    message_ = "Cannot create package preview";
    return;
  }
  preview_ = p;
  try {
    job_.start({"inspect", package_, preview_});
  } catch (const std::exception &e) {
    state_ = State::Error;
    message_ = e.what();
  }
}
void ApplicationInstallerView::update() {
  job_.poll();
  while (!job_.messages.empty()) {
    auto j = job_.messages.front();
    job_.messages.pop_front();
    auto type = appString(j.get(), "type");
    if (type == "package") {
      name_ = appString(j.get(), "name");
      version_ = appString(j.get(), "version");
      icon_ = appString(j.get(), "icon");
      sha_ = appString(j.get(), "sha256");
      relation_ = appString(j.get(), "relation");
      total_ = json_object_get_uint64(appField(j.get(), "bytes"));
      if (relation_ == "same") {
        state_ = State::Done;
        message_ = "Already installed.";
      } else {
        state_ = State::Ready;
        message_ = relation_ == "new" ? ""
                                      : "This package is a different version "
                                        "from yours. Would you like to update?";
      }
    } else if (type == "progress" || type == "done") {
      processed_ = json_object_get_uint64(appField(j.get(), "processed"));
      total_ = json_object_get_uint64(appField(j.get(), "total"));
      percent_ = json_object_get_uint64(appField(j.get(), "percent"));
      if (type == "done") {
        state_ = State::Done;
        message_ = "Installed.";
      }
    } else if (type == "error") {
      state_ = State::Error;
      message_ = appString(j.get(), "message");
    }
  }
}
View *ApplicationInstallerView::handleInput(InputButton b, InputEvent e) {
  if (e != InputEvent::Pressed && e != InputEvent::Repeat)
    return nullptr;
  if (state_ == State::Installing)
    return nullptr;
  if (b == InputButton::B) {
    job_.cancel();
    return back_ ? back_() : nullptr;
  }
  if (state_ == State::Ready &&
      (b == InputButton::Left || b == InputButton::Right)) {
    choice_ = 1 - choice_;
    return nullptr;
  }
  if (e == InputEvent::Pressed &&
      (b == InputButton::A || b == InputButton::Start)) {
    if (state_ == State::Ready) {
      if (choice_ == 1)
        return back_ ? back_() : nullptr;
      state_ = State::Installing;
      message_.clear();
      try {
        job_.start({"install", package_, sha_});
      } catch (const std::exception &err) {
        state_ = State::Error;
        message_ = err.what();
      }
    } else if (state_ == State::Done || state_ == State::Error)
      return back_ ? back_() : nullptr;
  }
  return nullptr;
}
View *ApplicationInstallerView::handlePointer(const PointerEvent &p) {
  int y = height_ - 40, w = std::min(180, (width_ - 48) / 2),
      x = (width_ - 2 * w - 16) / 2;
  if (state_ == State::Ready) {
    if (hit(p, x, y, w, 32))
      choice_ = 0;
    else if (hit(p, x + w + 16, y, w, 32))
      choice_ = 1;
    else
      return View::handlePointer(p);
  }
  if (p.type == PointerEventType::Pressed && p.button == PointerButton::Primary)
    return handleInput(InputButton::A, InputEvent::Pressed);
  return View::handlePointer(p);
}
void ApplicationInstallerView::render(Renderer &r, Font &f, const Theme &) {
  update();
  width_ = r.width();
  height_ = r.height();
  background(artwork_, r, f, "LilSmoke Package Installer");
  bool compact = height_ < 330;
  int cubeSize = compact ? 58
                         : std::min(state_ == State::Installing ? 144 : 158,
                                    (height_ - 170) / 2);
  int cy = compact ? 90 : 66 + cubeSize * 71 / 100;
  cube(artwork_, r, f, icon_, compact ? 76 : width_ / 2, cy, cubeSize);
  int titleY = compact ? 66 : cy + cubeSize * 72 / 100 + 7;
  int titleX = compact ? 132 : 16,
      titleW = compact ? width_ - 148 : width_ - 32;
  text(r, f, name_.empty() ? "Reading package..." : name_, titleX, titleY,
       titleW, compact ? 35 : 30, compact ? 16 : 20, Ink,
       compact ? "left" : "center", true);
  if (!version_.empty())
    text(r, f, version_, titleX, titleY + (compact ? 36 : 30), titleW, 22, 14,
         Muted, compact ? "left" : "center");
  if (state_ == State::Installing) {
    int y = compact ? 132 : std::max(titleY + 58, height_ - 126);
    int w = width_ - 64;
    int barHeight = compact ? 14 : 20;
    artwork_.panel(r, 32, y, w, barHeight, ApplicationSurface::Track);
    int filled = (w - 4) * std::min<uint64_t>(100, percent_) / 100;
    if (filled > 0) {
      r.setClipRect(34, y + 2, filled, barHeight - 4);
      artwork_.panel(r, 34, y + 2, w - 4, barHeight - 4,
                     ApplicationSurface::Progress);
      r.clearClipRect();
    }
    text(r, f, std::to_string(percent_) + "%", 16, y + (compact ? 16 : 22),
         width_ - 32, compact ? 24 : 32, compact ? 20 : 26, Ink, "center");
    char amount[96];
    snprintf(amount, sizeof(amount), "%.1f MB / %.1f MB",
             processed_ / 1000000.0, total_ / 1000000.0);
    text(r, f, amount, 16, y + (compact ? 39 : 54), width_ - 32, 24,
         compact ? 13 : 16, Muted, "center");
  } else {
    int messageY = compact ? 128 : titleY + 58;
    if (!message_.empty())
      text(r, f, message_, 20, messageY, width_ - 40,
           std::max(24, height_ - 50 - messageY), compact ? 13 : 15, Ink,
           "center", true);
    int w = std::min(180, (width_ - 48) / 2), x = (width_ - 2 * w - 16) / 2,
        y = height_ - 40;
    if (state_ == State::Ready) {
      button(artwork_, r, f, "INSTALL", x, y, w, 32, choice_ == 0);
      button(artwork_, r, f, "CANCEL", x + w + 16, y, w, 32, choice_ == 1);
    } else if (state_ == State::Done || state_ == State::Error)
      button(artwork_, r, f, "BACK", (width_ - w) / 2, y, w, 32, true);
  }
}
void ApplicationsView::refresh() {
  invalidateIcons_ = true;
  try {
    job_.start({"list"});
  } catch (const std::exception &e) {
    mode_ = Mode::Message;
    message_ = e.what();
  }
}
void ApplicationsView::onEnter() {
  if (mode_ == Mode::Grid)
    refresh();
}
void ApplicationsView::update() {
  job_.poll();
  while (!job_.messages.empty()) {
    auto j = job_.messages.front();
    job_.messages.pop_front();
    auto type = appString(j.get(), "type");
    if (type == "catalog") {
      invalidateIcons_ = true;
      apps_.clear();
      auto a = appField(j.get(), "apps");
      if (a && json_object_is_type(a, json_type_array))
        for (size_t i = 0; i < json_object_array_length(a); ++i) {
          auto v = json_object_array_get_idx(a, i);
          apps_.push_back({appString(v, "name"), appString(v, "version"),
                           appString(v, "icon")});
        }
      if (selected_ >= apps_.size())
        selected_ = apps_.empty() ? 0 : apps_.size() - 1;
      LOG_INFO("Applications: catalog ready (%zu)",apps_.size());
    } else if (type == "done") {
      mode_ = Mode::Grid;
      refresh();
    } else if (type == "error") {
      mode_ = Mode::Message;
      message_ = appString(j.get(), "message");
    }
  }
}
void ApplicationsView::handleText(const std::string &s) {
  if (mode_ != Mode::Rename)
    return;
  for (char c : s) {
    if (c == '\b') {
      if (!rename_.empty()) {
        size_t at = rename_.size() - 1;
        while (at > 0 &&
               (static_cast<unsigned char>(rename_[at]) & 0xc0) == 0x80)
          --at;
        rename_.resize(at);
      }
    } else if (rename_.size() < 120 && static_cast<unsigned char>(c) >= 32)
      rename_ += c;
  }
}
View *ApplicationsView::handleInput(InputButton b, InputEvent e) {
  if (e != InputEvent::Pressed && e != InputEvent::Repeat)
    return nullptr;
  if (mode_ == Mode::Busy)
    return nullptr;
  if (b == InputButton::B) {
    if (mode_ == Mode::Grid)
      return back_ ? back_() : nullptr;
    mode_ = Mode::Grid;
    return nullptr;
  }
  if (mode_ == Mode::Rename) {
    if (b == InputButton::Start && e == InputEvent::Pressed) {
      try {
        job_.start({"rename", apps_[selected_].name, rename_});
        mode_ = Mode::Busy;
      } catch (const std::exception &err) {
        message_ = err.what();
        mode_ = Mode::Message;
      }
    }
    return nullptr;
  }
  if (mode_ == Mode::Message) {
    if (b == InputButton::A || b == InputButton::Start) {
      mode_ = Mode::Grid;
      refresh();
    }
    return nullptr;
  }
  if (mode_ == Mode::Confirm) {
    if (b == InputButton::Left || b == InputButton::Right)
      choice_ = 1 - choice_;
    else if (e == InputEvent::Pressed &&
             (b == InputButton::A || b == InputButton::Start)) {
      if (choice_ == 0) {
        job_.start({"uninstall", apps_[selected_].name});
        mode_ = Mode::Busy;
      } else
        mode_ = Mode::Grid;
    }
    return nullptr;
  }
  if (mode_ == Mode::Options) {
    if (b == InputButton::Up)
      choice_ = std::max(0, choice_ - 1);
    else if (b == InputButton::Down)
      choice_ = std::min(2, choice_ + 1);
    else if (e == InputEvent::Pressed &&
             (b == InputButton::A || b == InputButton::Start)) {
      if (choice_ == 0) {
        rename_ = apps_[selected_].name;
        mode_ = Mode::Rename;
      } else if (choice_ == 1) {
        choice_ = 1;
        mode_ = Mode::Confirm;
      } else
        mode_ = Mode::Grid;
    }
    return nullptr;
  }
  if (b == InputButton::X) {
    refresh();
    return nullptr;
  }
  if (apps_.empty())
    return nullptr;
  size_t per = columns_ * rows_;
  if (b == InputButton::Left && selected_ > 0)
    --selected_;
  else if (b == InputButton::Right && selected_ + 1 < apps_.size())
    ++selected_;
  else if (b == InputButton::Up && selected_ >= static_cast<size_t>(columns_))
    selected_ -= columns_;
  else if (b == InputButton::Down)
    selected_ = std::min(apps_.size() - 1, selected_ + columns_);
  else if (b == InputButton::L1)
    selected_ = selected_ >= per ? selected_ - per : 0;
  else if (b == InputButton::R1)
    selected_ = std::min(apps_.size() - 1, selected_ + per);
  else if (e == InputEvent::Pressed && b == InputButton::Y) {
    choice_ = 0;
    mode_ = Mode::Options;
  } else if (e == InputEvent::Pressed &&
             (b == InputButton::A || b == InputButton::Start))
    requestProcessLaunch({"/usr/lib/lilsmoke/lilsmoke-apps",
                          {"lilsmoke-apps", "run", apps_[selected_].name}});
  return nullptr;
}
View *ApplicationsView::handlePointer(const PointerEvent &p) {
  auto layout = dialogLayout(width_, height_);
  int mw = layout.width, mx = layout.x, my = layout.y;
  if (mode_ == Mode::Options) {
    for (int i = 0; i < 3; ++i)
      if (hit(p, mx + 16, my + layout.padding + i * layout.step, mw - 32,
              layout.buttonHeight)) {
        choice_ = i;
        if (p.type == PointerEventType::Pressed &&
            p.button == PointerButton::Primary)
          return handleInput(InputButton::A, InputEvent::Pressed);
        return nullptr;
      }
    return View::handlePointer(p);
  }
  if (mode_ == Mode::Confirm) {
    if (hit(p, mx + 12, my + layout.confirmY, (mw - 36) / 2,
            layout.buttonHeight))
      choice_ = 0;
    else if (hit(p, mx + 24 + (mw - 36) / 2, my + layout.confirmY,
                 (mw - 36) / 2, layout.buttonHeight))
      choice_ = 1;
    else
      return View::handlePointer(p);
    if (p.type == PointerEventType::Pressed &&
        p.button == PointerButton::Primary)
      return handleInput(InputButton::A, InputEvent::Pressed);
    return nullptr;
  }
  if (mode_ != Mode::Grid)
    return View::handlePointer(p);
  if (p.y >= height_ - 44 && p.type == PointerEventType::Pressed &&
      p.button == PointerButton::Primary) {
    if (p.x < 116)
      return handleInput(InputButton::B, InputEvent::Pressed);
    if (p.x > width_ - 116)
      return handleInput(InputButton::Y, InputEvent::Pressed);
    size_t per = columns_ * rows_, page = selected_ / per,
           pages = std::max<size_t>(1, (apps_.size() + per - 1) / per);
    size_t first = page > 3 ? page - 3 : 0,
           shown = std::min<size_t>(7, pages - first);
    int left = width_ / 2 - int(shown - 1) * 5 - 5;
    if (p.x >= left && p.x < left + int(shown) * 10 && !apps_.empty())
      selected_ =
          std::min(apps_.size() - 1, (first + size_t((p.x - left) / 10)) * per);
    return nullptr;
  }
  int cw = (width_ - 32) / columns_, ch = (height_ - 112) / rows_;
  size_t page = selected_ / (columns_ * rows_);
  if (p.x >= 16 && p.x < width_ - 16 && p.y >= 58 && p.y < height_ - 54) {
    size_t i =
        page * columns_ * rows_ + (p.y - 58) / ch * columns_ + (p.x - 16) / cw;
    if (i < apps_.size()) {
      selected_ = i;
      if (p.type == PointerEventType::Pressed) {
        if (p.button == PointerButton::Primary)
          return handleInput(InputButton::A, InputEvent::Pressed);
        if (p.button == PointerButton::Middle)
          return handleInput(InputButton::Y, InputEvent::Pressed);
      }
    }
    return nullptr;
  }
  return View::handlePointer(p);
}
void ApplicationsView::render(Renderer &r, Font &f, const Theme &) {
  update();
  width_ = r.width();
  height_ = r.height();
  columns_ = std::clamp(width_ / 180, 2, 4);
  rows_ = std::clamp((height_ - 112) / 116, 1, 3);
  background(artwork_, r, f, "LilSmoke Applications");
  if (invalidateIcons_) {
    for (auto &a : apps_)
      if (!a.icon.empty())
        r.invalidateImage(a.icon);
    invalidateIcons_ = false;
  }
  int cw = (width_ - 32) / columns_, ch = (height_ - 112) / rows_,
      size = std::clamp(ch - 64, 52, 108);
  size_t per = columns_ * rows_, page = selected_ / per;
  for (size_t slot = 0; slot < per && page * per + slot < apps_.size();
       ++slot) {
    size_t i = page * per + slot;
    int x = 16 + (slot % columns_) * cw, y = 58 + (slot / columns_) * ch,
        bx = x + (cw - size) / 2;
    artwork_.panel(r, bx, y + 5, size, size, ApplicationSurface::Channel);
    if (i == selected_) {
      artwork_.focus(r, x + 10, y + 1, cw - 20, ch - 4);
    }
    icon(r, f, apps_[i].icon, bx + 8, y + 13, size - 16);
    text(r, f, apps_[i].name, x + 14, y + size + 11, cw - 28, 28, 15, Ink,
         "center", true);
    text(r, f, apps_[i].version, x + 14, y + size + 39, cw - 28, 17, 12, Muted,
         "center");
  }
  if (apps_.empty())
    text(r, f, job_.running() ? "Loading..." : "No applications installed", 16,
         80, width_ - 32, height_ - 150, 18, Muted, "center");
  artwork_.panel(r, 10, height_ - 39, 106, 32, ApplicationSurface::Command);
  artwork_.panel(r, width_ - 116, height_ - 39, 106, 32,
                 ApplicationSurface::Command);
  text(r, f, "Back", 40, height_ - 38, 65, 30, 16);
  text(r, f, "Options", width_ - 108, height_ - 38, 75, 30, 16, Ink, "left");
  auto renderer = static_cast<SDL_Renderer *>(r.sdlRenderer());
  SDL_SetRenderDrawColor(renderer, 49, 102, 145, 255);
  SDL_Point arrow[] = {
      {29, height_ - 31}, {21, height_ - 23}, {29, height_ - 15}};
  SDL_RenderDrawLines(renderer, arrow, 3);
  SDL_RenderDrawLine(renderer, 22, height_ - 23, 35, height_ - 23);
  for (int i = 0; i < 4; ++i)
    r.fillRect(width_ - 28 + (i % 2) * 7, height_ - 29 + (i / 2) * 7, 5, 5,
               0x468ABEFF);
  size_t pages = std::max<size_t>(1, (apps_.size() + per - 1) / per);
  size_t first = page > 3 ? page - 3 : 0,
         shown = std::min<size_t>(7, pages - first);
  int dx = width_ / 2 - int(shown - 1) * 5;
  for (size_t i = 0; i < shown; ++i) {
    auto color = first + i == page ? Blue : 0x98B9CFFF;
    r.fillRect(dx + int(i) * 10 - 2, height_ - 26, 4, 6, color);
    r.fillRect(dx + int(i) * 10 - 3, height_ - 25, 6, 4, color);
  }
  if (mode_ != Mode::Grid) {
    r.fillRect(0, 52, width_, height_ - 96, 0xD8E8F4D8);
    auto layout = dialogLayout(width_, height_);
    int w = layout.width, x = layout.x, y = layout.y;
    artwork_.panel(r, x, y, w, layout.height, ApplicationSurface::Channel);
    if (mode_ == Mode::Options) {
      const char *options[] = {"Rename", "Uninstall", "Cancel"};
      for (int i = 0; i < 3; ++i)
        button(artwork_, r, f, options[i], x + 16,
               y + layout.padding + i * layout.step, w - 32,
               layout.buttonHeight, choice_ == i, i == 1);
    } else if (mode_ == Mode::Rename) {
      text(r, f, "Rename Application", x + 16, y + 10, w - 32, 30, 20);
      text(r, f, rename_, x + 16, y + 46, w - 32, 64, 16, Ink, "left", true);
    } else if (mode_ == Mode::Confirm) {
      text(r, f, "Uninstall " + apps_[selected_].name + "?", x + 12, y + 12,
           w - 24, layout.confirmY - 20, 17, Ink, "center", true);
      button(artwork_, r, f, "UNINSTALL", x + 12, y + layout.confirmY,
             (w - 36) / 2, layout.buttonHeight, choice_ == 0, true);
      button(artwork_, r, f, "CANCEL", x + 24 + (w - 36) / 2,
             y + layout.confirmY, (w - 36) / 2, layout.buttonHeight,
             choice_ == 1);
    } else
      text(r, f, mode_ == Mode::Busy ? "Processing..." : message_, x + 16,
           y + 16, w - 32, layout.height - 32, 16, Ink, "center", true);
  }
}
} // namespace lilsmoke
