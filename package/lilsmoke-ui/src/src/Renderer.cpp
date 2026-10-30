// LilSmokeUI — Renderer.cpp

#include "Renderer.h"
#include "Font.h"
#include "Log.h"
#include "ApplicationIcon.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <new>
#include <cerrno>
#include <limits.h>

namespace lilsmoke {

namespace {
using DamagePixel = uint32_t __attribute__((__may_alias__));

// Keep the aligned scan out of the rectangle builder's register-heavy loop.
// Small memcpy temporaries otherwise become lwl/lwr plus stack spills on MIPS.
__attribute__((noinline)) bool changedRGBAligned(const DamagePixel *a, const DamagePixel *b, int count) {
    while (count >= 4) {
        const uint32_t different = (a[0] ^ b[0]) | (a[1] ^ b[1]) |
                                   (a[2] ^ b[2]) | (a[3] ^ b[3]);
        if (different & 0x00FFFFFFU) return true;
        a += 4; b += 4; count -= 4;
    }
    while (count--) if ((*a++ ^ *b++) & 0x00FFFFFFU) return true;
    return false;
}

static uint32_t multiplyRB(uint32_t rb, unsigned alpha) {
    const uint32_t product = rb * alpha;
    return ((product + 0x00010001U + ((product >> 8) & 0x00FF00FFU)) >> 8) & 0x00FF00FFU;
}

static unsigned divide255(unsigned value) { return (value + 1U + (value >> 8)) >> 8; }

template<bool SwapRB, bool Opaque>
void scaledBlend(const uint8_t *source, int sourcePitch, uint8_t *dest, int destPitch,
                 int sourceW, int sourceH, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 alpha,
                 bool destAlpha, bool copy) {
    std::array<unsigned, 1920> columns;
    const unsigned stepX = (static_cast<unsigned>(sourceW) << 16) / w;
    const unsigned stepY = (static_cast<unsigned>(sourceH) << 16) / h;
    unsigned position = stepX / 2;
    for (int x = 0; x < w; ++x, position += stepX) columns[x] = position >> 16;
    const bool colorMod = r != 255 || g != 255 || b != 255;
    uint8_t red[256], green[256], blue[256];
    if (colorMod) for (unsigned i = 0; i < 256; ++i) {
        red[i] = divide255(i * r); green[i] = divide255(i * g); blue[i] = divide255(i * b);
    }
    position = stepY / 2;
    for (int y = 0; y < h; ++y, position += stepY) {
        const auto *row = reinterpret_cast<const DamagePixel *>(source + (position >> 16) * sourcePitch);
        auto *out = reinterpret_cast<DamagePixel *>(dest + y * destPitch);
        for (int x = 0; x < w; ++x) {
            uint32_t p = row[columns[x]];
            if (SwapRB) p = (p & 0xFF00FF00U) | ((p & 0xFFU) << 16) | ((p >> 16) & 0xFFU);
            const unsigned a = divide255((Opaque ? 255 : (p >> 24)) * alpha);
            if (!a && !copy) continue;
            if (colorMod) p = (uint32_t(red[(p >> 16) & 255]) << 16) |
                              (uint32_t(green[(p >> 8) & 255]) << 8) | blue[p & 255];
            if (copy) { out[x] = (p & 0xFFFFFFU) | (destAlpha ? a << 24 : 0); continue; }
            if (a == 255) { out[x] = (p & 0xFFFFFFU) | (destAlpha ? 0xFF000000U : 0); continue; }
            const uint32_t d = out[x];
            const uint32_t rb = multiplyRB(p & 0x00FF00FFU, a) + multiplyRB(d & 0x00FF00FFU, 255 - a);
            const unsigned valueG = divide255(((p >> 8) & 255) * a) + divide255(((d >> 8) & 255) * (255 - a));
            const unsigned valueA = destAlpha ? a + divide255((d >> 24) * (255 - a)) : 0;
            out[x] = rb | (valueG << 8) | (valueA << 24);
        }
    }
}
}

Renderer::Renderer() {
    if (const char *enabled = std::getenv("LILSMOKE_UI_FAST_BLITS"))
        fastSoftwareBlits_ = std::strcmp(enabled, "0") != 0;
}

Renderer::~Renderer() {
    if (damageWatchAdded_) SDL_DelEventWatch(damageEventWatch, this);
    resetDamageShadow();
    for (const auto &entry : textCache_) SDL_DestroyTexture(entry.second.texture);
    textCache_.clear();
    for (const auto &entry : scaledCache_) SDL_DestroyTexture(entry.texture);
    scaledCache_.clear();
    for (auto &kv : textureCache_) {
        if (kv.second.texture) SDL_DestroyTexture(kv.second.texture);
    }
    textureCache_.clear();
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    if (ownedSurface_) SDL_FreeSurface(ownedSurface_);
    SDL_Quit();
}

void Renderer::setTextureCacheLimitMB(int megabytes) {
    if (megabytes < 4) megabytes = 4;
    if (megabytes > 32) megabytes = 32;
    textureCacheLimitBytes_ = static_cast<size_t>(megabytes) * 1024U * 1024U;
}

void Renderer::evictTextureCache(size_t incomingBytes) {
    static const size_t MAX_CACHE_ENTRIES = 256;
    while (!scaledCache_.empty() && textureCacheBytes_ + incomingBytes > textureCacheLimitBytes_) {
        const auto oldest = std::min_element(scaledCache_.begin(), scaledCache_.end(),
            [](const ScaledEntry &a, const ScaledEntry &b) { return a.lastFrame < b.lastFrame; });
        discardScaled(static_cast<size_t>(oldest - scaledCache_.begin()));
    }
    while (!textureCache_.empty() &&
           (textureCacheBytes_ + incomingBytes > textureCacheLimitBytes_ ||
            textureCache_.size() >= MAX_CACHE_ENTRIES)) {
        auto oldest = textureCache_.begin();
        for (auto it = textureCache_.begin(); it != textureCache_.end(); ++it) {
            if (it->second.lastUse < oldest->second.lastUse) oldest = it;
        }
        // A scan just larger than the cache must not evict its entire working
        // set every frame. Keep reused images; rotate a consumed probationary
        // slot instead. An image returned but not drawn is never preferred.
        auto probationary = textureCache_.end();
        if (oldest->second.lastFrame + 1 >= renderFrame_) {
            for (auto it = textureCache_.begin(); it != textureCache_.end(); ++it) {
                const auto &entry = it->second;
                if (entry.reused || entry.drawnUse < entry.lastUse) continue;
                if (probationary == textureCache_.end() || entry.lastUse > probationary->second.lastUse)
                    probationary = it;
            }
        }
        if (probationary != textureCache_.end()) oldest = probationary;
        discardScaledFor(oldest->second.texture);
        imageEntries_.erase(oldest->second.texture);
        if (oldest->second.texture) SDL_DestroyTexture(oldest->second.texture);
        textureCacheBytes_ -= oldest->second.bytes;
        textureCache_.erase(oldest);
    }
}

void Renderer::markImageDrawn(SDL_Texture *texture) {
    const auto entry = imageEntries_.find(texture);
    if (entry != imageEntries_.end()) entry->second->drawnUse = entry->second->lastUse;
}

bool Renderer::initOffscreen(int width, int height) {
    if (renderer_ || window_ || width < 1 || height < 1 || width > 1920 || height > 1080)
        return false;
    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) < 0) return false;
    ownedSurface_ = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!ownedSurface_) return false;
    renderer_ = SDL_CreateSoftwareRenderer(ownedSurface_);
    if (!renderer_) return false;
    width_ = outputWidth_ = width; height_ = outputHeight_ = height;
    softwareSurface_ = ownedSurface_; softwareWindowRenderer_ = true;
    SDL_RenderSetLogicalSize(renderer_, width, height);
    if (!(IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG) & IMG_INIT_PNG)) return false;
    LOG_INFO("Renderer: RAM ARGB canvas %dx%d (no video driver/window)", width, height);
    return true;
}

bool Renderer::init(int width, int height) {
    width_ = width;
    height_ = height;
    outputWidth_ = width;
    outputHeight_ = height;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    // SDL_VIDEODRIVER=directfb is set in the environment by /etc/profile.d/lilsmoke.sh.
    // Host smoke tests can request a small hidden/windowed surface.
    Uint32 flags = getenv("LILSMOKE_UI_WINDOWED") ? 0 : SDL_WINDOW_FULLSCREEN;
    if (getenv("LILSMOKE_UI_HIDDEN")) flags |= SDL_WINDOW_HIDDEN;
    window_ = SDL_CreateWindow("LilSmokeUI",
                                SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                width, height, flags);
    if (!window_) {
        LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    // Software renderer (no GPU). SDL2 will use DirectFB's surface blitter.
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer_) {
        LOG_ERROR("SDL_CreateRenderer failed: %s", SDL_GetError());
        return false;
    }

    SDL_RenderSetLogicalSize(renderer_, width, height);

    SDL_RendererInfo info{};
    softwareWindowRenderer_ = SDL_GetRendererInfo(renderer_, &info) == 0 &&
        info.name && std::strcmp(info.name, "software") == 0 &&
        (info.flags & SDL_RENDERER_SOFTWARE) && !(info.flags & SDL_RENDERER_ACCELERATED);
    if (softwareWindowRenderer_) softwareSurface_ = SDL_GetWindowSurface(window_);
    const char *profile = std::getenv("LILSMOKE_UI_PROFILE");
    profilePresent_ = profile && std::strcmp(profile, "0") != 0;
    const char *damageEnabled = std::getenv("LILSMOKE_UI_NATIVE_DAMAGE");
    if (softwareWindowRenderer_ && (!damageEnabled || std::strcmp(damageEnabled, "0") != 0)) {
        nativePresentDamage_ = reinterpret_cast<NativePresentDamage>(
            dlsym(RTLD_DEFAULT, "lilsmoke_present_damage"));
        if (nativePresentDamage_) {
            damageWindowID_ = SDL_GetWindowID(window_);
            SDL_AddEventWatch(damageEventWatch, this);
            damageWatchAdded_ = true;
            LOG_INFO("Renderer: native damage API available (software, %u rect cap)", maxDamageRects_);
        }
    }

    // Initialize SDL_image. Themes commonly use PNG and JPEG, and some
    // newer theme sets ship WebP. Missing optional loaders are tolerated.
    int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG;
#ifdef IMG_INIT_WEBP
    imgFlags |= IMG_INIT_WEBP;
#endif
#ifdef IMG_INIT_SVG
    imgFlags |= IMG_INIT_SVG;
#endif
    int initialized = IMG_Init(imgFlags);
    if (!(initialized & IMG_INIT_PNG)) {
        LOG_ERROR("IMG_Init failed to initialize PNG support: %s", IMG_GetError());
        return false;
    }
    if (!(initialized & IMG_INIT_JPG))
        LOG_WARN("IMG_Init: JPEG support unavailable; JPG theme art will be skipped");
#ifdef IMG_INIT_WEBP
    if (!(initialized & IMG_INIT_WEBP))
        LOG_WARN("IMG_Init: WebP support unavailable; WebP theme/cover art will be skipped");
#endif

    LOG_INFO("Renderer: initialized %dx%d", width, height);
    return true;
}

void Renderer::setSafeAreaPercent(int percent) {
    const int oldInsetX = insetX_, oldInsetY = insetY_;
    percent = std::clamp(percent, 0, 10);
    insetX_ = std::min(outputWidth_ * percent / 100,
                       std::max(0, (outputWidth_ - 320) / 2));
    insetY_ = std::min(outputHeight_ * percent / 100,
                       std::max(0, (outputHeight_ - 240) / 2));
    width_ = outputWidth_ - 2 * insetX_;
    height_ = outputHeight_ - 2 * insetY_;
    if (oldInsetX != insetX_ || oldInsetY != insetY_) {
        resetDamageShadow();
        nativeDamageFailed_ = false;
        nativeRetryAtMs_ = 0;
        nativeRetryDelayMs_ = 0;
    }
    if (renderer_) {
        SDL_Rect viewport = {insetX_, insetY_, width_, height_};
        SDL_RenderSetViewport(renderer_, &viewport);
        SDL_RenderSetClipRect(renderer_, nullptr);
    }
}

void Renderer::clear(uint32_t color) {
    ++renderFrame_;
    uint8_t r = (color >> 24) & 0xFF;
    uint8_t g = (color >> 16) & 0xFF;
    uint8_t b = (color >> 8) & 0xFF;
    uint8_t a = color & 0xFF;
    int outputW = 0, outputH = 0;
    SDL_Rect viewport;
    SDL_RenderGetViewport(renderer_, &viewport);
    if (!insetX_ && !insetY_ && !viewport.x && !viewport.y &&
        SDL_GetRendererOutputSize(renderer_, &outputW, &outputH) == 0 &&
        viewport.w == width_ && viewport.h == height_ && outputW == width_ && outputH == height_) {
        // No overscan border: the second full-surface fill only rewrote this clear.
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer_, r, g, b, a);
        SDL_RenderClear(renderer_);
        return;
    }
    // RenderClear ignores the viewport. Keep the overscan border black.
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer_, r, g, b, a);
    SDL_Rect clip;
    const bool clipped = SDL_RenderIsClipEnabled(renderer_);
    SDL_RenderGetClipRect(renderer_, &clip);
    SDL_RenderSetClipRect(renderer_, nullptr);
    SDL_Rect rect = {0, 0, width_, height_};
    SDL_RenderFillRect(renderer_, &rect);
    if (clipped) SDL_RenderSetClipRect(renderer_, &clip);
}

bool Renderer::canReuseFrame() const {
    return window_ && canReuseSurface(SDL_GetWindowSurface(window_));
}

bool Renderer::canReuseSurface(SDL_Surface *surface) const {
    if (!softwareWindowRenderer_ || !nativePresentDamage_ || nativeDamageFailed_ ||
        !presentStats_.native || !damageShadowValid_ || damageInvalidated_.load(std::memory_order_relaxed) ||
        SDL_GetRenderTarget(renderer_) != nullptr) return false;
    return surface && surface == softwareSurface_ && surface == damageSurface_ &&
        surface->pixels == damagePixels_ && surface->w == damageWidth_ && surface->h == damageHeight_ &&
        surface->pitch == damagePitch_ && surface->format &&
        surface->format->BytesPerPixel == 4 && surface->format->BitsPerPixel == 32 &&
        surface->format->Rmask == 0x00FF0000U && surface->format->Gmask == 0x0000FF00U &&
        surface->format->Bmask == 0x000000FFU &&
        surface->format->Amask == damageAlphaMask_;
}

void Renderer::present() {
    presentStats_ = {};
    presentStats_.opaqueFillPixels = pendingOpaqueFillPixels_;
    presentStats_.blendedFillPixels = pendingBlendedFillPixels_;
    pendingOpaqueFillPixels_ = pendingBlendedFillPixels_ = 0;
    if (damageInvalidated_.exchange(false, std::memory_order_relaxed)) {
        resetDamageShadow();
        nativeDamageFailed_ = false;
        nativeRetryAtMs_ = 0;
        nativeRetryDelayMs_ = 0;
    }
    if (!presentNativeDamage()) {
        resetDamageShadow();
        presentStats_.fallback = true;
        SDL_ClearError();
        SDL_RenderPresent(renderer_);
        presentStats_.submitted = SDL_GetError()[0] == '\0';
    } else presentStats_.submitted = true;
    if (profilePresent_) recordPresentProfile();
}

int Renderer::damageEventWatch(void *context, SDL_Event *event) {
    auto *renderer = static_cast<Renderer *>(context);
    bool invalid = event->type == SDL_RENDER_TARGETS_RESET || event->type == SDL_RENDER_DEVICE_RESET;
    if (event->type == SDL_WINDOWEVENT && event->window.windowID == renderer->damageWindowID_) {
        switch (event->window.event) {
            case SDL_WINDOWEVENT_EXPOSED:
            case SDL_WINDOWEVENT_RESIZED:
            case SDL_WINDOWEVENT_SIZE_CHANGED:
            case SDL_WINDOWEVENT_SHOWN:
            case SDL_WINDOWEVENT_RESTORED:
            case SDL_WINDOWEVENT_FOCUS_GAINED:
            case SDL_WINDOWEVENT_DISPLAY_CHANGED:
                invalid = true;
                break;
            default: break;
        }
    }
    // Event watches can run on SDL's input thread; only the render thread frees memory.
    if (invalid) renderer->damageInvalidated_.store(true, std::memory_order_relaxed);
    return 0;
}

void Renderer::resetDamageShadow() {
    damageShadow_.reset();
    damageShadowBytes_ = 0;
    damageSurface_ = nullptr;
    damagePixels_ = nullptr;
    damageWidth_ = damageHeight_ = damagePitch_ = 0;
    damageShadowValid_ = false;
    damageRectCount_ = 0;
}

bool Renderer::presentNativeDamage() {
    if (!nativePresentDamage_ || nativeDamageFailed_ || !softwareWindowRenderer_ ||
        SDL_GetRenderTarget(renderer_) != nullptr) return false;
    if (nativeRetryAtMs_ && SDL_GetTicks64() < nativeRetryAtMs_) return false;
    const uint64_t began = profilePresent_ ? SDL_GetPerformanceCounter() : 0;
    const int flushed = SDL_RenderFlush(renderer_);
    if (profilePresent_) presentStats_.flushTicks = SDL_GetPerformanceCounter() - began;
    if (flushed < 0) {
        resetDamageShadow();
        nativeDamageFailed_ = true;
        return false;
    }
    return presentNativeSurface(SDL_GetWindowSurface(window_));
}

bool Renderer::presentNativeSurface(SDL_Surface *surface) {
    const auto unsupported = [this]() {
        resetDamageShadow();
        // Retry only after a surface/window/presentation change, not on every frame.
        nativeDamageFailed_ = true;
        return false;
    };
    if (!surface || !surface->format || !surface->pixels || surface->w <= 0 || surface->h <= 0 ||
        surface->w > 1920 || surface->h > 1080 || surface->pitch < surface->w * 4 ||
        surface->pitch > 16384 || surface->pitch % 4 != 0 ||
        surface->format->BitsPerPixel != 32 || surface->format->BytesPerPixel != 4 ||
        surface->format->Rmask != 0x00FF0000U || surface->format->Gmask != 0x0000FF00U ||
        surface->format->Bmask != 0x000000FFU ||
        (surface->format->Amask != 0 && surface->format->Amask != 0xFF000000U)) return unsupported();
    const bool mustLock = SDL_MUSTLOCK(surface);
    if (mustLock && SDL_LockSurface(surface) != 0) return unsupported();
    const auto unlock = [surface, mustLock]() { if (mustLock) SDL_UnlockSurface(surface); };
    const auto *pixels = static_cast<const uint8_t *>(surface->pixels);
    const size_t bytes = static_cast<size_t>(surface->w) * surface->h * 4U;
    if (bytes > maxDamageShadowBytes_) { unlock(); return unsupported(); }
    if (damageSurface_ != surface || damagePixels_ != pixels || damageWidth_ != surface->w ||
        damageHeight_ != surface->h || damagePitch_ != surface->pitch ||
        damageAlphaMask_ != surface->format->Amask) resetDamageShadow();
    if (!damageShadow_) {
        damageShadow_.reset(new (std::nothrow) uint8_t[bytes]);
        if (!damageShadow_) { unlock(); return unsupported(); }
        damageShadowBytes_ = bytes;
        damageSurface_ = surface;
        damagePixels_ = pixels;
        damageWidth_ = surface->w;
        damageHeight_ = surface->h;
        damagePitch_ = surface->pitch;
        damageAlphaMask_ = surface->format->Amask;
    }
    const uint64_t compareStart = profilePresent_ ? SDL_GetPerformanceCounter() : 0;
    buildDamageRects(pixels, surface->pitch, surface->w, surface->h);
    if (profilePresent_) presentStats_.compareTicks = SDL_GetPerformanceCounter() - compareStart;
    presentStats_.rects = damageRectCount_;
    for (unsigned i = 0; i < damageRectCount_; ++i) {
        const int32_t *rect = damageRects_.data() + i * 4;
        presentStats_.nativeBytes += static_cast<uint64_t>(rect[2]) * rect[3] * 4U;
        presentStats_.copyRows += static_cast<unsigned>(rect[3]);
    }
    const uint64_t nativeStart = profilePresent_ ? SDL_GetPerformanceCounter() : 0;
    errno = 0;
    presentStats_.nativeAttempted = true;
    const int result = nativePresentDamage_(pixels, static_cast<unsigned>(surface->w),
        static_cast<unsigned>(surface->h), static_cast<unsigned>(surface->pitch),
        damageRects_.data(), damageRectCount_);
    const int nativeError = errno;
    presentStats_.nativeResult = result;
    presentStats_.nativeError = nativeError;
    if (profilePresent_) presentStats_.nativeTicks = SDL_GetPerformanceCounter() - nativeStart;
    if (result != 1) {
        unlock();
        nativeRetryDelayMs_ = nativeRetryDelayMs_ ? std::min(nativeRetryDelayMs_ * 2U, 2000U) : 100U;
        nativeRetryAtMs_ = SDL_GetTicks64() + nativeRetryDelayMs_;
        const char *hcfb = std::getenv("LILSMOKE_HCFB");
        LOG_WARN("Renderer: native damage rejected result=%d errno=%d source=%dx%d pitch=%d format=%s rects=%u hcfb=%s; SDL fallback, retry_ms=%u",
                 result, nativeError, surface->w, surface->h, surface->pitch,
                 SDL_GetPixelFormatName(surface->format->format), damageRectCount_,
                 hcfb ? hcfb : "unset", nativeRetryDelayMs_);
        resetDamageShadow();
        return false;
    }
    nativeRetryAtMs_ = 0;
    nativeRetryDelayMs_ = 0;
    if (!nativeActiveLogged_) {
        LOG_INFO("Renderer: native damage active source=%dx%d pitch=%d format=%s",
                 surface->w, surface->h, surface->pitch, SDL_GetPixelFormatName(surface->format->format));
        nativeActiveLogged_ = true;
    }
    // This is the last successfully presented visible frame, not merely the last render.
    commitDamage(pixels, surface->pitch, surface->w);
    damageShadowValid_ = true;
    presentStats_.native = true;
    unlock();
    return true;
}

void Renderer::buildDamageRects(const uint8_t *pixels, int pitch, int width, int height) {
    damageRectCount_ = 0;
    const auto fullFrame = [this, width, height]() {
        damageRectCount_ = 1;
        damageRects_[0] = damageRects_[1] = 0;
        damageRects_[2] = width; damageRects_[3] = height;
        presentStats_.fullFrame = true;
    };
    if (!damageShadowValid_) { fullFrame(); return; }
    constexpr size_t maxRuns = (1920 + damageRunPixels_ - 1) / damageRunPixels_;
    std::array<unsigned, maxRuns> previous{}, current{};
    size_t previousCount = 0;
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    const uint64_t densePixels = (static_cast<uint64_t>(width) * height + 1) / 2;
    uint64_t dirtyPixels = 0;
    for (int y = 0; y < height; ++y) {
        const uint8_t *row = pixels + static_cast<size_t>(y) * pitch;
        const uint8_t *old = damageShadow_.get() + static_cast<size_t>(y) * rowBytes;
        size_t currentCount = 0;
        const auto changed = [row, old](int x, int count) {
            const uint8_t *a = row + x * 4, *b = old + x * 4;
            if (((reinterpret_cast<uintptr_t>(a) | reinterpret_cast<uintptr_t>(b)) & 3U) == 0)
                return changedRGBAligned(reinterpret_cast<const DamagePixel *>(a),
                                         reinterpret_cast<const DamagePixel *>(b), count);
            // Read RGB once in word-sized groups, including alpha-only frames.
            // memcpy also keeps deliberately unaligned SDL surfaces well-defined.
            while (count >= 4) {
                uint32_t a0, a1, a2, a3, b0, b1, b2, b3;
                std::memcpy(&a0, a, 4); std::memcpy(&b0, b, 4);
                std::memcpy(&a1, a + 4, 4); std::memcpy(&b1, b + 4, 4);
                std::memcpy(&a2, a + 8, 4); std::memcpy(&b2, b + 8, 4);
                std::memcpy(&a3, a + 12, 4); std::memcpy(&b3, b + 12, 4);
                if (((a0 ^ b0) | (a1 ^ b1) | (a2 ^ b2) | (a3 ^ b3)) & 0x00FFFFFFU) return true;
                a += 16; b += 16; count -= 4;
            }
            while (count--) {
                uint32_t av, bv;
                std::memcpy(&av, a, 4); std::memcpy(&bv, b, 4);
                if ((av ^ bv) & 0x00FFFFFFU) return true;
                a += 4; b += 4;
            }
            return false;
        };
        for (int x = 0; x < width;) {
            int runWidth = std::min(damageRunPixels_, width - x);
            if (!changed(x, runWidth)) { x += runWidth; continue; }
            const int left = x;
            x += runWidth;
            while (x < width && changed(x, std::min(damageRunPixels_, width - x)))
                x += std::min(damageRunPixels_, width - x);
            runWidth = x - left;
            dirtyPixels += runWidth;
            if (dirtyPixels >= densePixels) { fullFrame(); return; }
            unsigned index = damageRectCount_;
            for (size_t p = 0; p < previousCount; ++p) {
                const unsigned candidate = previous[p];
                const int32_t *rect = damageRects_.data() + candidate * 4;
                if (rect[0] == left && rect[2] == runWidth) { index = candidate; break; }
            }
            if (index == damageRectCount_) {
                if (damageRectCount_ == maxDamageRects_) { fullFrame(); return; }
                int32_t *rect = damageRects_.data() + damageRectCount_++ * 4;
                rect[0] = left; rect[1] = y; rect[2] = runWidth; rect[3] = 1;
            } else ++damageRects_[index * 4 + 3];
            current[currentCount++] = index;
        }
        std::copy_n(current.begin(), currentCount, previous.begin());
        previousCount = currentCount;
    }
}

void Renderer::commitDamage(const uint8_t *pixels, int pitch, int width) {
    const size_t shadowPitch = static_cast<size_t>(width) * 4;
    for (unsigned i = 0; i < damageRectCount_; ++i) {
        const int32_t *rect = damageRects_.data() + i * 4;
        for (int y = rect[1]; y < rect[1] + rect[3]; ++y) {
            std::memcpy(damageShadow_.get() + static_cast<size_t>(y) * shadowPitch + rect[0] * 4,
                pixels + static_cast<size_t>(y) * pitch + rect[0] * 4, static_cast<size_t>(rect[2]) * 4);
        }
    }
}

void Renderer::recordPresentProfile() {
    ++profilePresentFrames_;
    profileNativeFrames_ += presentStats_.native;
    profileFallbackFrames_ += presentStats_.fallback;
    profileFullFrames_ += presentStats_.fullFrame;
    profileNativeAttempts_ += presentStats_.nativeAttempted;
    profileNativeRejects_ += presentStats_.nativeAttempted && !presentStats_.native;
    profilePresentStats_.flushTicks += presentStats_.flushTicks;
    profilePresentStats_.compareTicks += presentStats_.compareTicks;
    profilePresentStats_.nativeTicks += presentStats_.nativeTicks;
    profilePresentStats_.nativeBytes += presentStats_.nativeBytes;
    profilePresentStats_.copyRows += presentStats_.copyRows;
    profilePresentStats_.rects += presentStats_.rects;
    profilePresentStats_.opaqueFillPixels += presentStats_.opaqueFillPixels;
    profilePresentStats_.blendedFillPixels += presentStats_.blendedFillPixels;
    if (profilePresentFrames_ < 120) return;
    const double divisor = static_cast<double>(SDL_GetPerformanceFrequency()) * profilePresentFrames_ / 1000.0;
    LOG_INFO("UI_DAMAGE_PROFILE frames=%u native_frames=%u sdl_frames=%u full_frames=%u rects=%u native_bytes_requested=%llu copy_rows_requested=%llu shadow_bytes=%zu flush_ms=%.3f compare_ms=%.3f native_copy_ms=%.3f",
        profilePresentFrames_, profileNativeFrames_, profileFallbackFrames_, profileFullFrames_,
        profilePresentStats_.rects, static_cast<unsigned long long>(profilePresentStats_.nativeBytes),
        static_cast<unsigned long long>(profilePresentStats_.copyRows), damageShadowBytes_,
        profilePresentStats_.flushTicks / divisor, profilePresentStats_.compareTicks / divisor,
        profilePresentStats_.nativeTicks / divisor);
    LOG_INFO("UI_DRAW_PROFILE frames=%u native_attempts=%u native_rejects=%u opaque_fill_pixels_requested=%llu blended_fill_pixels_requested=%llu",
        profilePresentFrames_, profileNativeAttempts_, profileNativeRejects_,
        static_cast<unsigned long long>(profilePresentStats_.opaqueFillPixels),
        static_cast<unsigned long long>(profilePresentStats_.blendedFillPixels));
    profilePresentFrames_ = profileNativeFrames_ = profileFallbackFrames_ = profileFullFrames_ = 0;
    profileNativeAttempts_ = profileNativeRejects_ = 0;
    profilePresentStats_ = {};
}

void Renderer::fillRect(int x, int y, int w, int h, uint32_t color) {
    uint8_t r = (color >> 24) & 0xFF;
    uint8_t g = (color >> 16) & 0xFF;
    uint8_t b = (color >> 8) & 0xFF;
    uint8_t a = color & 0xFF;
    // Opaque rectangles use SDL's bulk fill, not the scalar alpha-blend loop.
    SDL_SetRenderDrawBlendMode(renderer_, a == 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, r, g, b, a);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(renderer_, &rect);
    if (a == 255) SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    if (profilePresent_ && w > 0 && h > 0) {
        const uint64_t pixels = static_cast<uint64_t>(w) * h;
        if (a == 255) pendingOpaqueFillPixels_ += pixels;
        else pendingBlendedFillPixels_ += pixels;
    }
}

void Renderer::drawRect(int x, int y, int w, int h, uint32_t color) {
    uint8_t r = (color >> 24) & 0xFF;
    uint8_t g = (color >> 16) & 0xFF;
    uint8_t b = (color >> 8) & 0xFF;
    uint8_t a = color & 0xFF;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, r, g, b, a);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderDrawRect(renderer_, &rect);
}

void Renderer::invalidateImage(const std::string &path) {
    damageInvalidated_.store(true, std::memory_order_relaxed);
    const auto alias = imageAliases_.find(path);
    char resolved[PATH_MAX];
    const std::string key = alias != imageAliases_.end() ? alias->second :
        (realpath(path.c_str(), resolved) ? resolved : path);
    for (const std::string &variant : {key, key + "|app-icon-256"}) {
        auto it = textureCache_.find(variant);
        for (auto entry = imageAliases_.begin(); entry != imageAliases_.end();) {
            if (entry->second == variant) entry = imageAliases_.erase(entry);
            else ++entry;
        }
        if (it == textureCache_.end()) continue;
        discardScaledFor(it->second.texture);
        imageEntries_.erase(it->second.texture);
        if (it->second.texture) SDL_DestroyTexture(it->second.texture);
        textureCacheBytes_ -= it->second.bytes;
        textureCache_.erase(it);
    }
}

SDL_Texture *Renderer::loadImage(const std::string &path, bool applicationIcon) {
    const std::string suffix = applicationIcon ? "|app-icon-256" : "";
    const std::string key = path + suffix;
    auto it = textureCache_.find(key);
    std::string cachePath = key;
    if (it == textureCache_.end()) {
        auto alias = imageAliases_.find(key);
        if (alias != imageAliases_.end()) cachePath = alias->second;
        else {
            // ES includes spell the same shared artwork through each system's ../ path.
            // Resolve once per bounded alias, preserving real symlink semantics.
            char resolved[PATH_MAX];
            cachePath = (realpath(path.c_str(), resolved) ? std::string(resolved) : path) + suffix;
            if (cachePath != key) {
                if (imageAliases_.size() >= 256) imageAliases_.clear();
                imageAliases_.emplace(key, cachePath);
            }
        }
        it = textureCache_.find(cachePath);
    }
    if (it != textureCache_.end()) {
        it->second.lastUse = ++textureCacheClock_;
        it->second.lastFrame = renderFrame_;
        it->second.reused = true;
        return it->second.texture;
    }

    std::string loadedPath = path;
    SDL_Surface *surf = applicationIcon ? loadApplicationIcon(path) : IMG_Load(loadedPath.c_str());
    std::string lowerPath = path;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (!surf && lowerPath.size() > 4 &&
        lowerPath.compare(lowerPath.size() - 4, 4, ".svg") == 0) {
        std::string stem = path.substr(0, path.size() - 4);
        for (const char *extension : {".png", ".webp", ".jpg", ".jpeg"}) {
            loadedPath = stem + extension;
            surf = IMG_Load(loadedPath.c_str());
            if (surf) break;
        }
    }
    if (!surf) {
        LOG_DEBUG("loadImage: %s failed: %s", path.c_str(), IMG_GetError());
        evictTextureCache(0);
        textureCache_[cachePath] = {nullptr, 0, ++textureCacheClock_};
        return nullptr;
    }

    size_t decodedBytes = static_cast<size_t>(surf->w) *
                          static_cast<size_t>(surf->h) * 4U;
    bool solid = false;
    uint32_t solidColor = 0;
    Uint32 colorKey;
    if (surf->w <= 64 && surf->h <= 64 && SDL_GetColorKey(surf, &colorKey) != 0) {
        SDL_Surface *rgba = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGBA8888, 0);
        if (rgba) {
            std::memcpy(&solidColor, rgba->pixels, 4);
            solid = (solidColor & 0xFFU) == 255;
            for (int y = 0; solid && y < rgba->h; ++y)
                for (int x = 0; x < rgba->w; ++x) {
                    uint32_t p;
                    std::memcpy(&p, static_cast<uint8_t *>(rgba->pixels) + y * rgba->pitch + x * 4, 4);
                    if (p != solidColor) { solid = false; break; }
                }
            SDL_FreeSurface(rgba);
        }
    }
    SDL_Texture *tex = nullptr;
    // Expose software texture pixels through SDL's supported streaming lock API.
    // Other formats/colorkeys retain SDL's original conversion path.
    const Uint32 sourceFormat = surf->format->format;
    const bool rgb = sourceFormat == SDL_PIXELFORMAT_RGB888 || sourceFormat == SDL_PIXELFORMAT_RGB24 || sourceFormat == SDL_PIXELFORMAT_BGR24;
    if (fastSoftwareBlits_ && softwareWindowRenderer_ &&
        !surf->format->palette && SDL_GetColorKey(surf, &colorKey) != 0 &&
        (sourceFormat == SDL_PIXELFORMAT_ARGB8888 || sourceFormat == SDL_PIXELFORMAT_ABGR8888 || rgb)) {
        const Uint32 textureFormat = rgb ? static_cast<Uint32>(SDL_PIXELFORMAT_RGB888) : sourceFormat;
        tex = SDL_CreateTexture(renderer_, textureFormat, SDL_TEXTUREACCESS_STREAMING, surf->w, surf->h);
        if (tex) {
            const bool locked = SDL_MUSTLOCK(surf);
            const bool ready = !locked || SDL_LockSurface(surf) == 0;
            bool updated = false;
            if (ready && sourceFormat == textureFormat)
                updated = SDL_UpdateTexture(tex, nullptr, surf->pixels, surf->pitch) == 0;
            else if (ready) {
                void *pixels; int pitch;
                if (SDL_LockTexture(tex, nullptr, &pixels, &pitch) == 0) {
                    updated = SDL_ConvertPixels(surf->w, surf->h, sourceFormat, surf->pixels, surf->pitch,
                                                textureFormat, pixels, pitch) == 0;
                    SDL_UnlockTexture(tex);
                }
            }
            if (!updated) {
                SDL_DestroyTexture(tex); tex = nullptr;
            }
            if (locked && ready) SDL_UnlockSurface(surf);
            if (tex) {
                Uint8 r, g, b, a; SDL_BlendMode blend;
                SDL_GetSurfaceColorMod(surf, &r, &g, &b); SDL_GetSurfaceAlphaMod(surf, &a);
                SDL_GetSurfaceBlendMode(surf, &blend);
                SDL_SetTextureColorMod(tex, r, g, b); SDL_SetTextureAlphaMod(tex, a);
                SDL_SetTextureBlendMode(tex, blend);
            }
        }
    }
    if (!tex) tex = SDL_CreateTextureFromSurface(renderer_, surf);
    SDL_FreeSurface(surf);
    if (!tex) {
        LOG_WARN("loadImage: %s SDL_CreateTexture failed: %s", path.c_str(), SDL_GetError());
        evictTextureCache(0);
        textureCache_[cachePath] = {nullptr, 0, ++textureCacheClock_};
        return nullptr;
    }

    evictTextureCache(decodedBytes);
    auto &entry = textureCache_[cachePath];
    entry = {tex, decodedBytes, ++textureCacheClock_, solid, solidColor, renderFrame_, 0, false};
    imageEntries_[tex] = &entry;
    textureCacheBytes_ += decodedBytes;
    LOG_DEBUG("loadImage: %s OK%s", path.c_str(),
              loadedPath == path ? "" : (" via " + loadedPath).c_str());
    return tex;
}

void Renderer::drawTexture(SDL_Texture *tex, int x, int y, int w, int h) {
    if (!tex) return;
    markImageDrawn(tex);
    SDL_Rect dst = {x, y, w, h};
    if (w == 0 || h == 0) {
        SDL_QueryTexture(tex, nullptr, nullptr, &dst.w, &dst.h);
    }
    if (!drawSoftwareScaled(tex, dst.x, dst.y, dst.w, dst.h))
        SDL_RenderCopy(renderer_, tex, nullptr, &dst);
}

bool Renderer::drawSoftwareScaled(SDL_Texture *tex, int x, int y, int w, int h) {
    if (!fastSoftwareBlits_ || !softwareWindowRenderer_ || w <= 0 || h <= 0 || w > 1920 || h > 1080 ||
        static_cast<size_t>(w) * h < 4096 || SDL_GetRenderTarget(renderer_)) return false;
    Uint32 format; int access, sourceW, sourceH;
    if (SDL_QueryTexture(tex, &format, &access, &sourceW, &sourceH) != 0 || access != SDL_TEXTUREACCESS_STREAMING ||
        (format != SDL_PIXELFORMAT_ARGB8888 && format != SDL_PIXELFORMAT_ABGR8888 && format != SDL_PIXELFORMAT_RGB888) ||
        (sourceW == w && sourceH == h) || sourceW <= 0 || sourceH <= 0 || sourceW > 16384 || sourceH > 16384) return false;
    SDL_BlendMode blend; SDL_ScaleMode scaleMode;
    SDL_GetTextureBlendMode(tex, &blend); SDL_GetTextureScaleMode(tex, &scaleMode);
    if ((blend != SDL_BLENDMODE_BLEND && blend != SDL_BLENDMODE_NONE) || scaleMode != SDL_ScaleModeNearest) return false;
    float sx, sy; SDL_RenderGetScale(renderer_, &sx, &sy);
    if (sx != 1.0f || sy != 1.0f) return false;
    SDL_Rect viewport, clip;
    SDL_RenderGetViewport(renderer_, &viewport);
    clip = {0, 0, viewport.w, viewport.h};
    if (SDL_RenderIsClipEnabled(renderer_)) SDL_RenderGetClipRect(renderer_, &clip);
    SDL_Surface *surface = window_ ? SDL_GetWindowSurface(window_) : softwareSurface_;
    if (!surface) return false;
    const int64_t right = int64_t(x) + w, bottom = int64_t(y) + h;
    if (right <= clip.x || bottom <= clip.y || x >= clip.x + clip.w || y >= clip.y + clip.h) return true;
    bool stableScale = false;
    const auto owner = imageEntries_.find(tex);
    if (owner != imageEntries_.end()) {
        auto &entry = *owner->second;
        if (entry.scaleWidth != w || entry.scaleHeight != h) {
            entry.scaleWidth = w; entry.scaleHeight = h; entry.stableScaleFrames = 1;
        } else if (entry.scaleFrame != renderFrame_) {
            entry.stableScaleFrames = entry.scaleFrame + 1 == renderFrame_
                ? std::min(3U, entry.stableScaleFrames + 1) : 1;
        }
        entry.scaleFrame = renderFrame_;
        stableScale = entry.stableScaleFrames >= 3;
    }
    auto drawRaster = [&](bool opaqueCopy = false) {
        SDL_Texture *scaled = scaledTexture(tex, sourceW, sourceH, w, h, format);
        if (!scaled) return false;
        Uint8 r, g, b, a; SDL_GetTextureColorMod(tex, &r, &g, &b); SDL_GetTextureAlphaMod(tex, &a);
        SDL_SetTextureColorMod(scaled, r, g, b); SDL_SetTextureAlphaMod(scaled, a);
        SDL_SetTextureBlendMode(scaled, opaqueCopy ? SDL_BLENDMODE_NONE : blend);
        const SDL_Rect dst{x, y, w, h};
        return SDL_RenderCopy(renderer_, scaled, nullptr, &dst) == 0;
    };
    // SDL's off-window path first scales an unmodulated full-size temporary,
    // then performs the ordinary clipped blit. Reuse exactly that raster.
    if (x + viewport.x < 0 || y + viewport.y < 0 || right + viewport.x > surface->w || bottom + viewport.y > surface->h) {
        return drawRaster();
    }
    // Inside-window clip scaling has separate source-rounding rules; retain SDL.
    if (x < 0 || y < 0 || x < clip.x || y < clip.y || x + w > clip.x + clip.w ||
        y + h > clip.y + clip.h || x + w > viewport.w || y + h > viewport.h) return false;
    // Stable artwork is scaled once within the existing image budget. Animated
    // size changes retain the direct path instead of filling the cache per frame.
    if (stableScale && format == SDL_PIXELFORMAT_RGB888) {
        // Opaque, unmodulated rasters use SDL's fast plain copy. Alpha rasters
        // retain the original scaled blend: unscaled SDL rounding differs and
        // caching them increased the measured MIPS game-list cost.
        Uint8 r, g, b, a;
        SDL_GetTextureColorMod(tex, &r, &g, &b); SDL_GetTextureAlphaMod(tex, &a);
        if (r == 255 && g == 255 && b == 255 && a == 255 && drawRaster(true)) return true;
    }
    if (SDL_RenderFlush(renderer_) != 0) return false;
    if (!surface || (surface->format->format != SDL_PIXELFORMAT_RGB888 && surface->format->format != SDL_PIXELFORMAT_ARGB8888) ||
        x + viewport.x < 0 || y + viewport.y < 0 || x + viewport.x + w > surface->w || y + viewport.y + h > surface->h) return false;
    const bool mustLock = SDL_MUSTLOCK(surface);
    if (mustLock && SDL_LockSurface(surface) != 0) return false;
    void *source; int sourcePitch;
    if (SDL_LockTexture(tex, nullptr, &source, &sourcePitch) != 0) {
        if (mustLock) SDL_UnlockSurface(surface);
        return false;
    }
    auto *dest = static_cast<uint8_t *>(surface->pixels) + (y + viewport.y) * surface->pitch + (x + viewport.x) * 4;
    if ((reinterpret_cast<uintptr_t>(source) | reinterpret_cast<uintptr_t>(dest) | unsigned(sourcePitch) | unsigned(surface->pitch)) & 3U) {
        SDL_UnlockTexture(tex); if (mustLock) SDL_UnlockSurface(surface); return false;
    }
    Uint8 r, g, b, a; SDL_GetTextureColorMod(tex, &r, &g, &b); SDL_GetTextureAlphaMod(tex, &a);
    const bool destAlpha = surface->format->Amask != 0, copy = blend == SDL_BLENDMODE_NONE;
    if (format == SDL_PIXELFORMAT_ABGR8888)
        scaledBlend<true, false>(static_cast<uint8_t *>(source), sourcePitch, dest, surface->pitch, sourceW, sourceH, w, h,
            r, g, b, a, destAlpha, copy);
    else if (format == SDL_PIXELFORMAT_RGB888)
        scaledBlend<false, true>(static_cast<uint8_t *>(source), sourcePitch, dest, surface->pitch, sourceW, sourceH, w, h,
            r, g, b, a, destAlpha, copy);
    else scaledBlend<false, false>(static_cast<uint8_t *>(source), sourcePitch, dest, surface->pitch, sourceW, sourceH, w, h,
            r, g, b, a, destAlpha, copy);
    SDL_UnlockTexture(tex);
    if (mustLock) SDL_UnlockSurface(surface);
    return true;
}

void Renderer::discardScaled(size_t index) {
    auto &entry = scaledCache_[index];
    SDL_DestroyTexture(entry.texture);
    textureCacheBytes_ -= entry.bytes;
    scaledCacheBytes_ -= entry.bytes;
    scaledCache_.erase(scaledCache_.begin() + index);
}

void Renderer::discardScaledFor(SDL_Texture *source) {
    for (size_t i = 0; i < scaledCache_.size();)
        if (scaledCache_[i].source == source) discardScaled(i);
        else ++i;
}

SDL_Texture *Renderer::scaledTexture(SDL_Texture *source, int sourceW, int sourceH, int w, int h, uint32_t format) {
    for (auto &entry : scaledCache_) if (entry.source == source && entry.width == w && entry.height == h) {
        entry.lastFrame = renderFrame_;
        return entry.texture;
    }
    // Only immutable, Renderer-owned artwork may have reusable scale rasters.
    const auto owned = std::find_if(textureCache_.begin(), textureCache_.end(),
        [source](const auto &entry) { return entry.second.texture == source; });
    if (owned == textureCache_.end()) return nullptr;
    // Do not allocate a derivative during first-use image scans: later images
    // can immediately reclaim it, turning an optimization into extra scaling.
    if (renderFrame_ - owned->second.lastFrame < 2 && !owned->second.reused) return nullptr;
    const size_t bytes = static_cast<size_t>(w) * h * 4U;
    const size_t scaleLimit = textureCacheLimitBytes_ / 2;
    if (bytes > scaleLimit) return nullptr;
    while (!scaledCache_.empty() && (textureCacheBytes_ + bytes > textureCacheLimitBytes_ ||
           scaledCacheBytes_ + bytes > scaleLimit || scaledCache_.size() >= 16)) {
        const auto oldest = std::min_element(scaledCache_.begin(), scaledCache_.end(),
            [](const ScaledEntry &a, const ScaledEntry &b) { return a.lastFrame < b.lastFrame; });
        // Do not oscillate between oversized background layers every frame.
        if (renderFrame_ - oldest->lastFrame <= 2) return nullptr;
        discardScaled(static_cast<size_t>(oldest - scaledCache_.begin()));
    }
    if (textureCacheBytes_ + bytes > textureCacheLimitBytes_) return nullptr;
    void *pixels; int pitch;
    if (SDL_LockTexture(source, nullptr, &pixels, &pitch) != 0) return nullptr;
    SDL_Surface *input = SDL_CreateRGBSurfaceWithFormatFrom(pixels, sourceW, sourceH, 32, pitch, format);
    SDL_Texture *texture = input ? SDL_CreateTexture(renderer_, format, SDL_TEXTUREACCESS_STREAMING, w, h) : nullptr;
    void *scaledPixels = nullptr; int scaledPitch = 0;
    const bool locked = texture && SDL_LockTexture(texture, nullptr, &scaledPixels, &scaledPitch) == 0;
    SDL_Surface *output = locked ? SDL_CreateRGBSurfaceWithFormatFrom(scaledPixels, w, h, 32, scaledPitch, format) : nullptr;
    bool ready = false;
    if (input && output) {
        SDL_SetSurfaceBlendMode(input, SDL_BLENDMODE_NONE);
        ready = SDL_BlitScaled(input, nullptr, output, nullptr) == 0;
    }
    SDL_FreeSurface(output); SDL_FreeSurface(input);
    if (locked) SDL_UnlockTexture(texture);
    SDL_UnlockTexture(source);
    if (!ready) { SDL_DestroyTexture(texture); return nullptr; }
    try { scaledCache_.push_back({source, texture, w, h, bytes, renderFrame_}); }
    catch (const std::bad_alloc &) { SDL_DestroyTexture(texture); return nullptr; }
    scaledCacheBytes_ += bytes;
    textureCacheBytes_ += bytes;
    return texture;
}

void Renderer::drawTextureEx(SDL_Texture *tex, int x, int y, int w, int h,
                             double angle, int centerX, int centerY,
                             bool flipX, bool flipY) {
    if (!tex) return;
    markImageDrawn(tex);
    if (angle == 0.0 && !flipX && !flipY) {
        drawTexture(tex, x, y, w, h);
        return;
    }
    SDL_Rect dst = {x, y, w, h};
    if (w == 0 || h == 0)
        SDL_QueryTexture(tex, nullptr, nullptr, &dst.w, &dst.h);
    SDL_Point center = {centerX, centerY};
    SDL_RendererFlip flip = SDL_FLIP_NONE;
    if (flipX) flip = static_cast<SDL_RendererFlip>(flip | SDL_FLIP_HORIZONTAL);
    if (flipY) flip = static_cast<SDL_RendererFlip>(flip | SDL_FLIP_VERTICAL);
    SDL_RenderCopyEx(renderer_, tex, nullptr, &dst, angle, &center, flip);
}

void Renderer::drawTextureTiled(SDL_Texture *tex, int x, int y, int w, int h) {
    if (!tex || w <= 0 || h <= 0) return;
    markImageDrawn(tex);
    for (const auto &item : textureCache_) {
        const auto &entry = item.second;
        if (entry.texture != tex || !entry.solidOpaque) continue;
        Uint8 r, g, b, a;
        SDL_BlendMode blend;
        SDL_GetTextureAlphaMod(tex, &a);
        SDL_GetTextureBlendMode(tex, &blend);
        if (a != 255 || (blend != SDL_BLENDMODE_NONE && blend != SDL_BLENDMODE_BLEND)) break;
        SDL_GetTextureColorMod(tex, &r, &g, &b);
        const uint32_t color = entry.solidColor;
        fillRect(x, y, w, h,
            (((color >> 24) * r / 255) << 24) |
            ((((color >> 16) & 255) * g / 255) << 16) |
            ((((color >> 8) & 255) * b / 255) << 8) | 255U);
        return;
    }
    int tileW = 0, tileH = 0;
    if (SDL_QueryTexture(tex, nullptr, nullptr, &tileW, &tileH) != 0 ||
        tileW <= 0 || tileH <= 0) {
        return;
    }

    for (int yy = 0; yy < h; yy += tileH) {
        for (int xx = 0; xx < w; xx += tileW) {
            SDL_Rect src = {0, 0, std::min(tileW, w - xx),
                            std::min(tileH, h - yy)};
            SDL_Rect dst = {x + xx, y + yy, src.w, src.h};
            SDL_RenderCopy(renderer_, tex, &src, &dst);
        }
    }
}

void Renderer::setClipRect(int x, int y, int w, int h) {
    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(width_, x + w);
    const int bottom = std::min(height_, y + h);
    if (right <= left || bottom <= top) {
        SDL_RenderSetClipRect(renderer_, nullptr);
        return;
    }
    SDL_Rect rect = {left, top, right - left, bottom - top};
    SDL_RenderSetClipRect(renderer_, &rect);
}

void Renderer::clearClipRect() {
    SDL_RenderSetClipRect(renderer_, nullptr);
}

bool Renderer::textureSize(SDL_Texture *tex, int &w, int &h) const {
    w = 0;
    h = 0;
    return tex && SDL_QueryTexture(tex, nullptr, nullptr, &w, &h) == 0;
}

void Renderer::drawText(Font &font, const std::string &text, int x, int y, uint32_t color) {
    font.render(*this, text, x, y, color);
}

SDL_Texture *Renderer::textTexture(uint64_t fontID, TTF_Font *font,
                                   const std::string &text, uint32_t color,
                                   int &width, int &height, bool &temporary) {
    temporary = false;
    // Alpha changes every animation frame, but glyph rasters do not.
    TextKey key{fontID, color | 0xffU, text};
    auto found = textCache_.find(key);
    if (found != textCache_.end()) {
        found->second.lastUse = ++textCacheClock_;
        width = found->second.width;
        height = found->second.height;
        SDL_SetTextureAlphaMod(found->second.texture, color & 0xffU);
        return found->second.texture;
    }
    SDL_Color c = {static_cast<Uint8>(color >> 24), static_cast<Uint8>(color >> 16),
                   static_cast<Uint8>(color >> 8), 255};
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text.c_str(), c);
    if (!surface) return nullptr;
    width = surface->w;
    height = surface->h;
    const size_t bytes = static_cast<size_t>(surface->pitch) * surface->h;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_FreeSurface(surface);
    if (!texture) return nullptr;
    SDL_SetTextureAlphaMod(texture, color & 0xffU);
    // Oversized runs remain drawable, but cannot displace the whole UI cache.
    if (bytes > textCacheLimitBytes_ || text.size() > 4096) {
        temporary = true;
        return texture;
    }
    while (!textCache_.empty() &&
           (textCacheBytes_ + bytes > textCacheLimitBytes_ ||
            textCache_.size() >= textCacheLimitEntries_)) {
        auto oldest = textCache_.begin();
        for (auto it = textCache_.begin(); it != textCache_.end(); ++it)
            if (it->second.lastUse < oldest->second.lastUse) oldest = it;
        SDL_DestroyTexture(oldest->second.texture);
        textCacheBytes_ -= oldest->second.bytes;
        textCache_.erase(oldest);
    }
    textCache_.emplace(std::move(key), TextEntry{texture, width, height, bytes, ++textCacheClock_});
    textCacheBytes_ += bytes;
    return texture;
}

}  // namespace lilsmoke
