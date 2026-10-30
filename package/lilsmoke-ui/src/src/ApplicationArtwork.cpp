#include "ApplicationArtwork.h"
#include "ApplicationIcon.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <tuple>

namespace lilsmoke {
namespace {
struct Point {
  float x, y, z;
};
using Quad = std::array<Point, 4>;
static SDL_Color mix(SDL_Color a, SDL_Color b, float t) {
  return {Uint8(a.r + (b.r - a.r) * t), Uint8(a.g + (b.g - a.g) * t),
          Uint8(a.b + (b.b - a.b) * t), Uint8(a.a + (b.a - a.a) * t)};
}
static void quad(SDL_Renderer *r, const Quad &p,
                 const std::array<SDL_Color, 4> &colors,
                 SDL_Texture *image = nullptr) {
  SDL_Vertex v[4];
  for (int i = 0; i < 4; ++i)
    v[i] = {{p[i].x, p[i].y},
            colors[i],
            {i == 1 || i == 2 ? 1.f : 0.f, i >= 2 ? 1.f : 0.f}};
  const int index[] = {0, 1, 2, 0, 2, 3};
  SDL_RenderGeometry(r, image, v, 4, index, 6);
}
static Quad inset(Quad p, float amount) {
  float x = 0, y = 0;
  for (auto q : p) {
    x += q.x * .25f;
    y += q.y * .25f;
  }
  for (auto &q : p) {
    q.x += (x - q.x) * amount;
    q.y += (y - q.y) * amount;
  }
  return p;
}
static Point along(Point a, Point b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, 0};
}
static Point onFace(const Quad &p, float u, float v) {
  return along(along(p[0], p[1], u), along(p[3], p[2], u), v);
}
static Uint32 pixel(SDL_PixelFormat *f, SDL_Color c) {
  return SDL_MapRGBA(f, c.r, c.g, c.b, c.a);
}
static SDL_Surface *surface(int w, int h) {
  return SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
}
} // namespace
struct ApplicationArtwork::State {
  std::map<std::tuple<int, int, int>, SDL_Texture *> panels;
  SDL_Texture *background = nullptr, *cubeOutput = nullptr, *cubeIcon = nullptr;
  SDL_Surface *cubeSurface = nullptr;
  SDL_Renderer *cubeRenderer = nullptr;
  int backgroundW = 0, backgroundH = 0, cubeSide = 0;
  uint32_t cubeTick = 0;
  bool cubeReady = false;
  size_t iconBytes = 0;
  unsigned redraws = 0;
  std::string iconPath;
  ~State() {
    for (auto &p : panels)
      SDL_DestroyTexture(p.second);
    SDL_DestroyTexture(background);
    SDL_DestroyTexture(cubeOutput);
    SDL_DestroyTexture(cubeIcon);
    if (cubeRenderer)
      SDL_DestroyRenderer(cubeRenderer);
    if (cubeSurface)
      SDL_FreeSurface(cubeSurface);
  }
};
ApplicationArtwork::ApplicationArtwork() : state_(new State) {}
ApplicationArtwork::~ApplicationArtwork() = default;
size_t ApplicationArtwork::retainedPixelBytes() const {
  const auto &s = *state_;
  size_t total =
      size_t(std::min(s.backgroundW, 854)) * std::min(s.backgroundH, 480) * 4;
  total += size_t(s.cubeSide) * s.cubeSide * 8 + s.iconBytes;
  for (const auto &p : s.panels)
    total += size_t(std::get<0>(p.first)) * std::get<1>(p.first) * 4;
  return total;
}
unsigned ApplicationArtwork::cubeRedraws() const { return state_->redraws; }
void ApplicationArtwork::background(Renderer &r) {
  auto &s = *state_;
  int w = r.width(), h = r.height();
  if (!s.background || s.backgroundW != w || s.backgroundH != h) {
    SDL_DestroyTexture(s.background);
    s.background = nullptr;
    int bw = std::min(w, 854), bh = std::min(h, 480);
    auto image = surface(bw, bh);
    if (!image) {
      r.clear(0xF4FAFFFF);
      return;
    }
    for (int x = 0; x < bw; ++x) {
      float u = float(x) / std::max(1, bw - 1);
      int wave = int(bh * (.70f + .14f * std::cos(u * 3.8f))),
          second = wave + int(bh * .075f);
      for (int y = 0; y < bh; ++y) {
        auto c = mix({252, 254, 255, 255}, {237, 247, 252, 255},
                     float(y) / std::max(1, bh - 1));
        if (y > wave && y < second)
          c = mix(c, {171, 221, 247, 255}, .22f);
        if (y >= second && y < second + int(bh * .025f))
          c = mix(c, {213, 238, 251, 255}, .35f);
        reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(image->pixels) +
                                   y * image->pitch)[x] =
            pixel(image->format, c);
      }
    }
    s.background = SDL_CreateTextureFromSurface(
        static_cast<SDL_Renderer *>(r.sdlRenderer()), image);
    if(s.background)SDL_SetTextureBlendMode(s.background,SDL_BLENDMODE_NONE);
    SDL_FreeSurface(image);
    s.backgroundW = w;
    s.backgroundH = h;
  }
  r.clear(0xF4FAFFFF);
  r.drawTexture(s.background, 0, 0, w, h);
}
void ApplicationArtwork::panel(Renderer &r, int x, int y, int w, int h,
                               ApplicationSurface style) {
  if (w < 1 || h < 1)
    return;
  auto &s = *state_;
  auto key = std::make_tuple(w, h, int(style));
  auto found = s.panels.find(key);
  if (found == s.panels.end()) {
    if (s.panels.size() >= 24) {
      for (auto &p : s.panels)
        SDL_DestroyTexture(p.second);
      s.panels.clear();
    }
    auto image = surface(w, h);
    if (!image)
      return;
    SDL_FillRect(image, nullptr, 0);
    int radius = std::min({8, w / 2, h / 2});
    bool blue = style == ApplicationSurface::SelectedCommand ||
                style == ApplicationSurface::Progress;
    SDL_Color top =
        blue ? SDL_Color{78, 190, 255, 255} : SDL_Color{255, 255, 255, 255};
    SDL_Color bottom =
        blue ? SDL_Color{13, 113, 212, 255} : SDL_Color{215, 235, 248, 255};
    SDL_Color edge =
        blue ? SDL_Color{30, 126, 215, 255} : SDL_Color{145, 180, 207, 255};
    if (style == ApplicationSurface::Danger) {
      top = {238, 124, 136, 255};
      bottom = {175, 40, 62, 255};
      edge = {148, 45, 61, 255};
    }
    if (style == ApplicationSurface::Track) {
      top = {212, 228, 242, 255};
      bottom = {247, 252, 255, 255};
    }
    for (int yy = 0; yy < h; ++yy)
      for (int xx = 0; xx < w; ++xx) {
        int dx = xx < radius        ? radius - xx
                 : xx >= w - radius ? xx - (w - radius - 1)
                                    : 0;
        int dy = yy < radius        ? radius - yy
                 : yy >= h - radius ? yy - (h - radius - 1)
                                    : 0;
        if (dx * dx + dy * dy > radius * radius)
          continue;
        auto c = mix(top, bottom, float(yy) / std::max(1, h - 1));
        int d = std::min({xx, yy, w - 1 - xx, h - 1 - yy});
        if (d == 0 || dx * dx + dy * dy > (radius - 1) * (radius - 1))
          c = edge;
        else if (d == 1 || yy == 2)
          c = blue ? SDL_Color{171, 228, 255, 255}
                   : SDL_Color{255, 255, 255, 255};
        else if (yy < h / 2 && style != ApplicationSurface::Track)
          c = mix(c, {255, 255, 255, 255}, blue ? .15f : .25f);
        reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(image->pixels) +
                                   yy * image->pitch)[xx] =
            pixel(image->format, c);
      }
    auto texture = SDL_CreateTextureFromSurface(
        static_cast<SDL_Renderer *>(r.sdlRenderer()), image);
    SDL_FreeSurface(image);
    if (!texture)
      return;
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    found = s.panels.emplace(key, texture).first;
  }
  r.drawTexture(found->second, x, y, w, h);
}
void ApplicationArtwork::focus(Renderer &r, int x, int y, int w, int h) {
  auto renderer = static_cast<SDL_Renderer *>(r.sdlRenderer());
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  const SDL_Color colors[] = {
      {151, 222, 255, 105}, {19, 156, 241, 255}, {221, 248, 255, 255}};
  for (int i = 0; i < 3; ++i) {
    SDL_SetRenderDrawColor(renderer, colors[i].r, colors[i].g, colors[i].b,
                           colors[i].a);
    int a = x + i, b = y + i, c = x + w - 1 - i, d = y + h - 1 - i, k = 8 - i;
    SDL_Point line[] = {{a + k, b}, {c - k, b}, {c, b + k},
                        {c, d - k}, {c - k, d}, {a + k, d},
                        {a, d - k}, {a, b + k}, {a + k, b}};
    SDL_RenderDrawLines(renderer, line, 9);
  }
}
void ApplicationArtwork::cube(Renderer &r, const std::string &path,
                              bool safeIcon, int cx, int cy, int size) {
  auto &s = *state_;
  int side = size * 3 / 2 + 24;
  if (s.cubeSide != side) {
    SDL_DestroyTexture(s.cubeOutput);
    SDL_DestroyTexture(s.cubeIcon);
    s.cubeIcon = nullptr;
    s.iconBytes = 0;
    if (s.cubeRenderer)
      SDL_DestroyRenderer(s.cubeRenderer);
    if (s.cubeSurface)
      SDL_FreeSurface(s.cubeSurface);
    s.cubeSurface = surface(side, side);
    s.cubeRenderer =
        s.cubeSurface ? SDL_CreateSoftwareRenderer(s.cubeSurface) : nullptr;
    s.cubeOutput = SDL_CreateTexture(
        static_cast<SDL_Renderer *>(r.sdlRenderer()), SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, side, side);
    if (s.cubeOutput)
      SDL_SetTextureBlendMode(s.cubeOutput, SDL_BLENDMODE_BLEND);
    s.cubeSide = side;
    s.iconPath.clear();
    s.cubeTick = 0;
    s.cubeReady = false;
  }
  if (!s.cubeRenderer || !s.cubeOutput)
    return;
  bool iconChanged = s.iconPath != path;
  if (iconChanged) {
    SDL_DestroyTexture(s.cubeIcon);
    s.cubeIcon = nullptr;
    s.iconBytes = 0;
    s.iconPath = path;
    if (safeIcon) {
      auto image = loadApplicationIcon(path);
      if (image) {
        s.cubeIcon = SDL_CreateTextureFromSurface(s.cubeRenderer, image);
        if (s.cubeIcon)
          s.iconBytes = size_t(image->w) * image->h * 4;
        SDL_FreeSurface(image);
      }
    }
  }
  uint32_t now = SDL_GetTicks();
  if (!s.cubeReady || uint32_t(now - s.cubeTick) >= 16 || iconChanged) {
    ++s.redraws;
    s.cubeTick = now;
    s.cubeReady = true;
    auto renderer = s.cubeRenderer;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    float seconds = now * .001f;
    float yaw = -.23f + .16f * std::sin(seconds * .24f), pitch = .16f,
          roll = .025f * std::sin(seconds * .18f);
    auto project = [&](float x, float y, float z) {
      float rx = x * std::cos(yaw) + z * std::sin(yaw),
            rz = -x * std::sin(yaw) + z * std::cos(yaw);
      float ry = y * std::cos(pitch) - rz * std::sin(pitch);
      rz = y * std::sin(pitch) + rz * std::cos(pitch);
      float px = rx * std::cos(roll) - ry * std::sin(roll),
            py = rx * std::sin(roll) + ry * std::cos(roll);
      float perspective = 4.8f / (4.8f + rz);
      return Point{side * .5f + px * size * .48f * perspective,
                   side * .45f + py * size * .48f * perspective, rz};
    };
    std::array<Point, 8> p;
    for (int i = 0; i < 8; ++i)
      p[i] =
          project(i & 1 ? 1.f : -1.f, i & 2 ? 1.f : -1.f, i & 4 ? 1.f : -1.f);
    const std::array<std::array<int, 4>, 6> ids = {{{0, 1, 3, 2},
                                                    {5, 4, 6, 7},
                                                    {4, 0, 2, 6},
                                                    {1, 5, 7, 3},
                                                    {4, 5, 1, 0},
                                                    {2, 3, 7, 6}}};
    std::array<int, 6> order = {0, 1, 2, 3, 4, 5};
    std::sort(order.begin(), order.end(), [&](int a, int b) {
      float az = 0, bz = 0;
      for (int i : ids[a])
        az += p[i].z;
      for (int i : ids[b])
        bz += p[i].z;
      return az > bz;
    });
    // Layered glass facets and prismatic bevels are rasterized into a small CPU
    // cache.
    for (int f : order) {
      Quad face;
      for (int i = 0; i < 4; ++i)
        face[i] = p[ids[f][i]];
      if (f == 0 && s.cubeIcon) {
        Quad art = {project(-.65f, -.65f, -.55f), project(.65f, -.65f, -.55f),
                    project(.65f, .65f, -.55f), project(-.65f, .65f, -.55f)};
        quad(renderer, art,
             {{{255, 255, 255, 255},
               {255, 255, 255, 255},
               {255, 255, 255, 255},
               {255, 255, 255, 255}}},
             s.cubeIcon);
      }
      auto inner = inset(face, .09f);
      SDL_Color a =
          f == 0 ? SDL_Color{167, 216, 222, 43} : SDL_Color{107, 167, 183, 58};
      SDL_Color b =
          f == 0 ? SDL_Color{122, 133, 170, 56} : SDL_Color{112, 114, 156, 73};
      quad(renderer, inner,
           {a, mix(a, {211, 225, 234, 255}, .12f), b,
            mix(b, {134, 198, 176, 255}, .1f)});
      for (int i = 0; i < 4; ++i) {
        int n = (i + 1) % 4;
        SDL_Color tint = i == 0   ? SDL_Color{235, 249, 255, 164}
                         : i == 1 ? SDL_Color{126, 190, 183, 135}
                         : i == 2 ? SDL_Color{135, 124, 188, 128}
                                  : SDL_Color{88, 140, 205, 144};
        Quad bevel = {face[i], face[n], inner[n], inner[i]};
        quad(renderer, bevel,
             {tint, tint, mix(tint, {255, 255, 255, 210}, .56f),
              mix(tint, {73, 104, 128, 70}, .4f)});
      }
      if (f == 0) {
        Quad shine = {onFace(inner, 0, .08f), onFace(inner, 1, .025f),
                      onFace(inner, 1, .15f), onFace(inner, 0, .25f)};
        quad(renderer, shine,
             {{{255, 255, 255, 18},
               {255, 255, 255, 54},
               {255, 255, 255, 6},
               {255, 255, 255, 5}}});
      }
    }
    SDL_RenderPresent(renderer);
    // The transparent software canvas contains premultiplied RGB. Streaming
    // textures use straight alpha; avoid applying the glass opacity twice.
    for (int y = 0; y < side; ++y) {
      auto row = reinterpret_cast<Uint32 *>(
          static_cast<Uint8 *>(s.cubeSurface->pixels) +
          y * s.cubeSurface->pitch);
      for (int x = 0; x < side; ++x) {
        auto value = row[x];
        unsigned a = value >> 24;
        if (a && a < 255) {
          unsigned red =
              std::min(255u, ((value >> 16 & 255) * 255 + a / 2) / a);
          unsigned green =
                       std::min(255u, ((value >> 8 & 255) * 255 + a / 2) / a),
                   blue = std::min(255u, ((value & 255) * 255 + a / 2) / a);
          row[x] = (a << 24) | (red << 16) | (green << 8) | blue;
        }
      }
    }
    SDL_UpdateTexture(s.cubeOutput, nullptr, s.cubeSurface->pixels,
                      s.cubeSurface->pitch);
  }
  auto renderer = static_cast<SDL_Renderer *>(r.sdlRenderer());
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  for (int i = -5; i <= 5; ++i) {
    float t = float(i) / 6;
    int half = size * .48f * std::sqrt(1 - t * t);
    SDL_SetRenderDrawColor(renderer, 62, 95, 131, Uint8(12 * (1 - t * t)));
    SDL_Rect shadow = {cx - half, cy + size * 69 / 100 + i, half * 2, 1};
    SDL_RenderFillRect(renderer, &shadow);
  }
  r.drawTexture(s.cubeOutput, cx - side / 2, cy - int(side * .45f), side, side);
}
} // namespace lilsmoke
