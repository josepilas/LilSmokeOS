#pragma once

#include <array>
#include <cstddef>
#include <string>

struct SDL_Texture;

namespace lilsmoke {

class Renderer;

// Semantic order matches the supplied 5x3 WebP atlas, left-to-right and
// top-to-bottom (the final row contains only Hand).
enum class CursorKind {
    Normal = 0,
    Link,
    Text,
    Busy,
    Help,
    Move,
    Precision,
    ResizeVertical,
    ResizeHorizontal,
    NotAllowed,
    Hand,
    Count,
};

const char *cursorKindName(CursorKind kind);

class CursorAtlas {
public:
    CursorAtlas() = default;
    ~CursorAtlas();

    CursorAtlas(const CursorAtlas &) = delete;
    CursorAtlas &operator=(const CursorAtlas &) = delete;

    bool load(Renderer &renderer, const std::string &path);
    void draw(Renderer &renderer, CursorKind kind, int x, int y,
              int size) const;
    bool loaded() const { return loaded_; }

private:
    std::array<SDL_Texture *, static_cast<size_t>(CursorKind::Count)> textures_{};
    std::array<float, static_cast<size_t>(CursorKind::Count)> hotspotX_{};
    std::array<float, static_cast<size_t>(CursorKind::Count)> hotspotY_{};
    bool loaded_ = false;
};

}  // namespace lilsmoke
