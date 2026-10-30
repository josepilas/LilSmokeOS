#pragma once
#include "ApplicationArtwork.h"
#include "AppsProtocol.h"
#include "View.h"
#include <deque>
#include <functional>

namespace lilsmoke {
class ApplicationJob {
public:
  ~ApplicationJob();
  void start(const std::vector<std::string> &arguments);
  void poll();
  void cancel();
  bool running() const { return pid_ > 0; }
  std::deque<AppJson> messages;

private:
  int pid_ = -1, fd_ = -1;
  bool eof_ = false;
  std::string pending_;
};
class ApplicationInstallerView : public View {
  friend struct ApplicationsTestAccess;

public:
  explicit ApplicationInstallerView(std::string package);
  ~ApplicationInstallerView() override;
  void onEnter() override;
  bool allowAutoBackPop() const override { return false; }
  View *handleInput(InputButton, InputEvent) override;
  View *handlePointer(const PointerEvent &) override;
  void render(Renderer &, Font &, const Theme &) override;
  void setOnBack(std::function<View *()> fn) { back_ = std::move(fn); }

private:
  enum class State { Reading, Ready, Installing, Done, Error };
  State state_ = State::Reading;
  ApplicationJob job_;
  ApplicationArtwork artwork_;
  std::string package_, preview_, name_, version_, icon_, sha_, relation_,
      message_;
  uint64_t processed_ = 0, total_ = 0, percent_ = 0;
  int choice_ = 1, width_ = 640, height_ = 480;
  bool entered_ = false;
  std::function<View *()> back_;
  void update();
};
class ApplicationsView : public View {
  friend struct ApplicationsTestAccess;

public:
  void onEnter() override;
  bool allowAutoBackPop() const override { return false; }
  bool acceptsText() const override { return mode_ == Mode::Rename; }
  std::string textTitle() const override { return "Rename Application"; }
  std::string textValue() const override { return rename_; }
  void handleText(const std::string &) override;
  View *handleInput(InputButton, InputEvent) override;
  View *handlePointer(const PointerEvent &) override;
  void render(Renderer &, Font &, const Theme &) override;
  void setOnBack(std::function<View *()> fn) { back_ = std::move(fn); }

private:
  struct Application {
    std::string name, version, icon;
  };
  enum class Mode { Grid, Options, Rename, Confirm, Busy, Message };
  Mode mode_ = Mode::Grid;
  std::vector<Application> apps_;
  ApplicationJob job_;
  ApplicationArtwork artwork_;
  size_t selected_ = 0;
  int choice_ = 0, columns_ = 4, rows_ = 2, width_ = 640, height_ = 480;
  std::string rename_, message_;
  bool invalidateIcons_ = false;
  std::function<View *()> back_;
  void refresh();
  void update();
};
} // namespace lilsmoke
