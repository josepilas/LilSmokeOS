// LilSmokeUI — Renderer.h
//
// Thin wrapper around SDL2's DirectFB backend. Provides:
//   - Window + renderer init (640x480, 32bpp BGRA)
//   - Clear / present
//   - Fill rectangle (with alpha)
//   - Draw image texture (PNG/JPEG/WebP, cached)
//   - Draw text (via Font)

#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Surface;
union SDL_Event;
struct _TTF_Font;
typedef struct _TTF_Font TTF_Font;

namespace lilsmoke {

class Font;

class Renderer {
public:
    Renderer();
    ~Renderer();

    // Initialize SDL2 + create window + renderer. Returns false on failure.
    bool init(int width = 640, int height = 480);
    // CPU-only ARGB canvas for a native video plane's HUD. No video driver/window.
    bool initOffscreen(int width, int height);

    // Inset the UI without another framebuffer or a GPU render target.
    void setSafeAreaPercent(int percent);
    int outputWidth() const { return outputWidth_; }
    int outputHeight() const { return outputHeight_; }
    int insetX() const { return insetX_; }
    int insetY() const { return insetY_; }

    // Bound decoded artwork held in RAM. This is an approximate software
    // texture budget; it deliberately excludes the framebuffer itself.
    void setTextureCacheLimitMB(int megabytes);

    // Clear the screen to a solid color (0xRRGGBBAA).
    void clear(uint32_t color);

    // Present the backbuffer.
    void present();
    // Retain only the exact software window surface last accepted by native presentation.
    // Caller must still call present() each loop; SDL fallback never grants reuse.
    bool canReuseFrame() const;

    struct PresentStats {
        uint64_t flushTicks = 0, compareTicks = 0, nativeTicks = 0;
        uint64_t nativeBytes = 0, copyRows = 0;
        uint64_t opaqueFillPixels = 0, blendedFillPixels = 0;
        unsigned rects = 0;
        bool native = false, fallback = false, fullFrame = false;
        bool nativeAttempted = false;
        bool submitted = false;
        int nativeResult = 0, nativeError = 0;
    };
    const PresentStats &presentStats() const { return presentStats_; }

    // Fill a rectangle (x,y,w,h in pixels, color 0xRRGGBBAA).
    void fillRect(int x, int y, int w, int h, uint32_t color);

    // Draw a rectangle outline (1px border).
    void drawRect(int x, int y, int w, int h, uint32_t color);

    // Load an image texture (cached by path). Returns nullptr on failure.
    // Caller should NOT free — cache owns it.
    SDL_Texture *loadImage(const std::string &path, bool applicationIcon = false);
    void invalidateImage(const std::string &path);

    // Draw a texture at (x,y) with optional scaling (w,h). Pass 0,0 for native size.
    void drawTexture(SDL_Texture *tex, int x, int y, int w = 0, int h = 0);

    // Draw with rotation/flipping, or repeat an image inside a rectangle.
    void drawTextureEx(SDL_Texture *tex, int x, int y, int w, int h,
                       double angle, int centerX, int centerY,
                       bool flipX = false, bool flipY = false);
    void drawTextureTiled(SDL_Texture *tex, int x, int y, int w, int h);

    // Clip subsequent drawing to a pixel rectangle. A non-positive size clears it.
    void setClipRect(int x, int y, int w, int h);
    void clearClipRect();

    bool textureSize(SDL_Texture *tex, int &w, int &h) const;

    // Draw text using a Font.
    void drawText(Font &font, const std::string &text, int x, int y, uint32_t color);

    int width() const { return width_; }
    int height() const { return height_; }

    // Access to the underlying SDL_Renderer (for Font to create textures from surfaces).
    // Returns void* to avoid pulling SDL.h into Renderer.h. Cast to SDL_Renderer* at call site.
    void *sdlRenderer() { return renderer_; }

private:
    friend class Font;
    friend struct RendererTestAccess;
    SDL_Window *window_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    SDL_Surface *softwareSurface_ = nullptr;
    SDL_Surface *ownedSurface_ = nullptr;
#ifdef __mips__
    bool fastSoftwareBlits_ = true;
#else
    bool fastSoftwareBlits_ = false;
#endif
    int width_ = 0, height_ = 0;
    int outputWidth_ = 0, outputHeight_ = 0;
    int insetX_ = 0, insetY_ = 0;

    struct CacheEntry {
        SDL_Texture *texture = nullptr;
        size_t bytes = 0;
        uint64_t lastUse = 0;
        bool solidOpaque = false;
        uint32_t solidColor = 0;
        uint64_t lastFrame = 0;
        uint64_t drawnUse = 0;
        bool reused = false;
        int scaleWidth = 0, scaleHeight = 0;
        unsigned stableScaleFrames = 0;
        uint64_t scaleFrame = 0;
    };

    void evictTextureCache(size_t incomingBytes);
    void markImageDrawn(SDL_Texture *texture);
    bool drawSoftwareScaled(SDL_Texture *texture, int x, int y, int w, int h);
    struct ScaledEntry {
        SDL_Texture *source, *texture;
        int width, height;
        size_t bytes;
        uint64_t lastFrame;
    };
    std::vector<ScaledEntry> scaledCache_;
    size_t scaledCacheBytes_ = 0;
    uint64_t renderFrame_ = 0;
    void discardScaled(size_t index);
    void discardScaledFor(SDL_Texture *source);
    SDL_Texture *scaledTexture(SDL_Texture *source, int sourceW, int sourceH, int w, int h, uint32_t format);

    std::unordered_map<std::string, CacheEntry> textureCache_;
    std::unordered_map<SDL_Texture *, CacheEntry *> imageEntries_;
    std::unordered_map<std::string, std::string> imageAliases_;
    size_t textureCacheBytes_ = 0;
    size_t textureCacheLimitBytes_ = 8U * 1024U * 1024U;
    uint64_t textureCacheClock_ = 0;

    struct TextKey {
        uint64_t font;
        uint32_t color;
        std::string text;
        bool operator==(const TextKey &other) const {
            return font == other.font && color == other.color && text == other.text;
        }
    };
    struct TextHash {
        size_t operator()(const TextKey &key) const {
            return std::hash<std::string>{}(key.text) ^
                   std::hash<uint64_t>{}(key.font) ^ std::hash<uint32_t>{}(key.color);
        }
    };
    struct TextEntry {
        SDL_Texture *texture;
        int width, height;
        size_t bytes;
        uint64_t lastUse;
    };
    SDL_Texture *textTexture(uint64_t fontID, TTF_Font *font,
                             const std::string &text, uint32_t color,
                             int &width, int &height, bool &temporary);
    std::unordered_map<TextKey, TextEntry, TextHash> textCache_;
    size_t textCacheBytes_ = 0;
    // Reserve 64 KiB of the existing 1 MiB text budget for Font layout/metrics.
    static constexpr size_t textCacheLimitBytes_ = 960U * 1024U;
    static constexpr size_t textCacheLimitEntries_ = 128;
    uint64_t textCacheClock_ = 0;

    using NativePresentDamage = int (*)(const void *, unsigned, unsigned, unsigned,
                                        const int32_t *, unsigned);
    NativePresentDamage nativePresentDamage_ = nullptr;
    bool softwareWindowRenderer_ = false, nativeDamageFailed_ = false;
    uint64_t nativeRetryAtMs_ = 0;
    unsigned nativeRetryDelayMs_ = 0;
    bool nativeActiveLogged_ = false;
    uint64_t pendingOpaqueFillPixels_ = 0, pendingBlendedFillPixels_ = 0;
    bool damageWatchAdded_ = false, profilePresent_ = false;
    uint32_t damageWindowID_ = 0;
    std::atomic<bool> damageInvalidated_{false};
    static int damageEventWatch(void *context, SDL_Event *event);
    static constexpr unsigned maxDamageRects_ = 512;
    static constexpr int damageRunPixels_ = 64;
    static constexpr size_t maxDamageShadowBytes_ = 8U * 1024U * 1024U;
    std::unique_ptr<uint8_t[]> damageShadow_;
    size_t damageShadowBytes_ = 0;
    SDL_Surface *damageSurface_ = nullptr;
    const void *damagePixels_ = nullptr;
    int damageWidth_ = 0, damageHeight_ = 0, damagePitch_ = 0;
    uint32_t damageAlphaMask_ = 0;
    bool damageShadowValid_ = false;
    std::array<int32_t, maxDamageRects_ * 4> damageRects_{};
    unsigned damageRectCount_ = 0;
    PresentStats presentStats_, profilePresentStats_;
    unsigned profilePresentFrames_ = 0, profileNativeFrames_ = 0;
    unsigned profileFallbackFrames_ = 0, profileFullFrames_ = 0;
    unsigned profileNativeAttempts_ = 0, profileNativeRejects_ = 0;
    void resetDamageShadow();
    bool presentNativeDamage();
    bool presentNativeSurface(SDL_Surface *surface);
    void buildDamageRects(const uint8_t *pixels, int pitch, int width, int height);
    void commitDamage(const uint8_t *pixels, int pitch, int width);
    void recordPresentProfile();
    bool canReuseSurface(SDL_Surface *surface) const;
};

}  // namespace lilsmoke
