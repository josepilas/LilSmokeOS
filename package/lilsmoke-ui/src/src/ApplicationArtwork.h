#pragma once
#include "Renderer.h"
#include <memory>
#include <string>

namespace lilsmoke {
enum class ApplicationSurface {
  Channel,
  Command,
  SelectedCommand,
  Danger,
  Track,
  Progress
};
class ApplicationArtwork {
public:
  ApplicationArtwork();
  ~ApplicationArtwork();
  ApplicationArtwork(const ApplicationArtwork &) = delete;
  ApplicationArtwork &operator=(const ApplicationArtwork &) = delete;
  void background(Renderer &);
  void panel(Renderer &, int x, int y, int width, int height,
             ApplicationSurface);
  void cube(Renderer &, const std::string &icon, bool safeIcon, int cx, int cy,
            int size);
  void focus(Renderer &, int x, int y, int width, int height);
  size_t retainedPixelBytes() const;
  unsigned cubeRedraws() const;

private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace lilsmoke
