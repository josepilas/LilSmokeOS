#include "Cursor.h"

#include "Log.h"
#include "Renderer.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include <algorithm>
#include <cstring>

namespace lilsmoke {

namespace {

constexpr int kAtlasColumns = 5;
constexpr int kTextureSize = 64;

static SDL_Rect opaqueBounds(SDL_Surface *surface, const SDL_Rect &cell) {
    SDL_Rect bounds = {cell.x + cell.w, cell.y + cell.h, 0, 0};
    int right = cell.x;
    int bottom = cell.y;
    bool found = false;

    SDL_LockSurface(surface);
    for (int y = cell.y; y < cell.y + cell.h; ++y) {
        const Uint8 *row = static_cast<const Uint8 *>(surface->pixels) +
                           y * surface->pitch;
        for (int x = cell.x; x < cell.x + cell.w; ++x) {
            Uint32 pixel = 0;
            std::memcpy(&pixel, row + x * surface->format->BytesPerPixel,
                        surface->format->BytesPerPixel);
            Uint8 red = 0, green = 0, blue = 0, alpha = 0;
            SDL_GetRGBA(pixel, surface->format, &red, &green, &blue, &alpha);
            (void)red;
            (void)green;
            (void)blue;
            if (alpha <= 8) continue;
            bounds.x = std::min(bounds.x, x);
            bounds.y = std::min(bounds.y, y);
            right = std::max(right, x + 1);
            bottom = std::max(bottom, y + 1);
            found = true;
        }
    }
    SDL_UnlockSurface(surface);

    if (!found) return cell;
    const int padding = 2;
    bounds.x = std::max(cell.x, bounds.x - padding);
    bounds.y = std::max(cell.y, bounds.y - padding);
    right = std::min(cell.x + cell.w, right + padding);
    bottom = std::min(cell.y + cell.h, bottom + padding);
    bounds.w = std::max(1, right - bounds.x);
    bounds.h = std::max(1, bottom - bounds.y);
    return bounds;
}

}  // namespace

const char *cursorKindName(CursorKind kind) {
    switch (kind) {
        case CursorKind::Normal: return "normal";
        case CursorKind::Link: return "link";
        case CursorKind::Text: return "text";
        case CursorKind::Busy: return "busy";
        case CursorKind::Help: return "help";
        case CursorKind::Move: return "move";
        case CursorKind::Precision: return "precision";
        case CursorKind::ResizeVertical: return "resize-vertical";
        case CursorKind::ResizeHorizontal: return "resize-horizontal";
        case CursorKind::NotAllowed: return "not-allowed";
        case CursorKind::Hand: return "hand";
        case CursorKind::Count: return "invalid";
    }
    return "normal";
}

CursorAtlas::~CursorAtlas() {
    for (SDL_Texture *texture : textures_) {
        if (texture) SDL_DestroyTexture(texture);
    }
}

bool CursorAtlas::load(Renderer &renderer, const std::string &path) {
    for (auto &texture : textures_) {
        if (texture) SDL_DestroyTexture(texture);
        texture = nullptr;
    }
    loaded_ = false;
    SDL_Surface *loaded = IMG_Load(path.c_str());
    if (!loaded) {
        LOG_WARN("Cursor: cannot load %s: %s", path.c_str(), IMG_GetError());
        return false;
    }
    SDL_Surface *atlas = SDL_ConvertSurfaceFormat(
        loaded, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded);
    if (!atlas) {
        LOG_WARN("Cursor: RGBA conversion failed: %s", SDL_GetError());
        return false;
    }
    if (atlas->w != 1536 || atlas->h != 1024) {
        SDL_FreeSurface(atlas);
        LOG_WARN("Cursor: expected the supplied 1536x1024 sprite sheet");
        return false;
    }
    SDL_SetSurfaceBlendMode(atlas, SDL_BLENDMODE_NONE);

    bool ok = true;
    for (size_t index = 0; index < textures_.size(); ++index) {
        int row = static_cast<int>(index) / kAtlasColumns;
        int column = static_cast<int>(index) % kAtlasColumns;
        // The supplied artwork is not an evenly spaced 5x3 grid.
        const int columns[] = {0, 320, 620, 910, 1200, 1536};
        const int rows[] = {140, 435, 730, 1024};
        int left = columns[column];
        int right = columns[column + 1];
        int top = rows[row];
        int bottom = rows[row + 1];
        SDL_Rect cell = {left, top, right - left, bottom - top};
        SDL_Rect source = opaqueBounds(atlas, cell);

        SDL_Surface *cursor = SDL_CreateRGBSurfaceWithFormat(
            0, kTextureSize, kTextureSize, 32, SDL_PIXELFORMAT_RGBA32);
        if (!cursor) {
            ok = false;
            break;
        }
        SDL_FillRect(cursor, nullptr,
                     SDL_MapRGBA(cursor->format, 0, 0, 0, 0));
        SDL_SetSurfaceBlendMode(atlas, SDL_BLENDMODE_NONE);
        float scale = std::min(
            static_cast<float>(kTextureSize) / source.w,
            static_cast<float>(kTextureSize) / source.h);
        int width = std::max(1, static_cast<int>(source.w * scale));
        int height = std::max(1, static_cast<int>(source.h * scale));
        SDL_Rect destination = {(kTextureSize - width) / 2,
                                (kTextureSize - height) / 2,
                                width, height};
        // Each variant retains the same upper-left arrow tip, including Text.
        hotspotX_[index] = (destination.x + width * 0.06f) / kTextureSize;
        hotspotY_[index] = (destination.y + height * 0.06f) / kTextureSize;
        if (SDL_BlitScaled(atlas, &source, cursor, &destination) != 0) {
            SDL_FreeSurface(cursor);
            ok = false;
            break;
        }
        textures_[index] = SDL_CreateTextureFromSurface(
            static_cast<SDL_Renderer *>(renderer.sdlRenderer()), cursor);
        SDL_FreeSurface(cursor);
        if (!textures_[index]) {
            ok = false;
            break;
        }
        SDL_SetTextureBlendMode(textures_[index], SDL_BLENDMODE_BLEND);
    }
    SDL_FreeSurface(atlas);
    loaded_ = ok;
    if (loaded_)
        LOG_INFO("Cursor: loaded all 11 sprites from %s", path.c_str());
    else
        LOG_WARN("Cursor: atlas was incomplete; using fallback cursor");
    return loaded_;
}

void CursorAtlas::draw(Renderer &renderer, CursorKind kind, int x, int y,
                       int size) const {
    size = std::max(16, std::min(size, 64));
    const size_t index = static_cast<size_t>(kind);
    if (loaded_ && index < textures_.size() && textures_[index]) {
        renderer.drawTexture(textures_[index],
                             x - static_cast<int>(hotspotX_[index] * size),
                             y - static_cast<int>(hotspotY_[index] * size),
                             size, size);
        return;
    }

    // Tiny framebuffer-only fallback if the optional WebP loader is absent.
    renderer.fillRect(x, y, 2, size / 2, 0xFFFFFFFF);
    renderer.fillRect(x, y, size / 3, 2, 0xFFFFFFFF);
}

}  // namespace lilsmoke
