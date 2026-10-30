#include "bob_scene.hpp"
#include <SDL.h>
extern "C" {
#include <TGL/gl.h>
#include <zbuffer.h>
}
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>

using namespace portsmoke::sm64;
namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
struct Camera {
    float yaw = 0.65f, pitch = 0.58f, distance = 23500.f;
    float x = 0, y = 1400, z = -1200;
    void clamp() {
        pitch = std::clamp(pitch, -1.3f, 1.3f);
        distance = std::clamp(distance, 1200.f, 42000.f);
        x = std::clamp(x, -16000.f, 16000.f);
        y = std::clamp(y, -2000.f, 12000.f);
        z = std::clamp(z, -16000.f, 16000.f);
    }
};
struct Vec { float x, y, z; };
Vec unit(Vec v) {
    float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    require(length > 0.0001f && std::isfinite(length), "invalid camera vector");
    return {v.x / length, v.y / length, v.z / length};
}
Vec cross(Vec a, Vec b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }

class Rasterizer {
public:
    explicit Rasterizer(const SceneMesh& mesh) : mesh_(mesh) {
        buffer_ = ZB_open(320, 240, ZB_MODE_RGBA, nullptr);
        require(buffer_ != nullptr, "cannot allocate bounded software framebuffer");
        glInit(buffer_);
        glViewport(0, 0, 320, 240);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glFrontFace(GL_CCW);
        for (std::size_t index = 0; index < mesh.textures.size(); ++index) {
            const auto& image = mesh.textures[index].image;
            std::vector<std::uint8_t> rgb(std::size_t(image.width) * image.height * 3);
            for (std::size_t i = 0; i < rgb.size() / 3; ++i) {
                if (!image.rgba[i * 4 + 3]) {
                    rgb[i * 3] = 255; rgb[i * 3 + 1] = 0; rgb[i * 3 + 2] = 255;
                } else std::copy_n(image.rgba.data() + i * 4, 3, rgb.data() + i * 3);
            }
            glBindTexture(GL_TEXTURE_2D, GLuint(index + 1));
            glTexImage2D(GL_TEXTURE_2D, 0, 3, image.width, image.height, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
        }
    }
    ~Rasterizer() { glClose(); if (buffer_) ZB_close(buffer_); }
    Rasterizer(const Rasterizer&) = delete;
    void draw(const Camera& camera) {
        glClearColor(0.45f, 0.69f, 0.91f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        constexpr float nearPlane = 100.f, farPlane = 60000.f;
        float height = nearPlane * std::tan(0.5235987756f);
        glFrustum(-height * 4.f/3.f, height * 4.f/3.f, -height, height, nearPlane, farPlane);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        Vec eye{camera.x + camera.distance * std::cos(camera.pitch) * std::sin(camera.yaw),
                camera.y + camera.distance * std::sin(camera.pitch),
                camera.z + camera.distance * std::cos(camera.pitch) * std::cos(camera.yaw)};
        Vec forward = unit({camera.x - eye.x, camera.y - eye.y, camera.z - eye.z});
        Vec side = unit(cross(forward, {0, 1, 0})), up = cross(side, forward);
        float matrix[16]{side.x, up.x, -forward.x, 0, side.y, up.y, -forward.y, 0,
                         side.z, up.z, -forward.z, 0, 0, 0, 0, 1};
        glMultMatrixf(matrix); glTranslatef(-eye.x, -eye.y, -eye.z);
        for (const auto& batch : mesh_.batches) {
            require(batch.first <= mesh_.vertices.size() && batch.count <= mesh_.vertices.size() - batch.first,
                    "invalid compiled scene batch");
            if (batch.texture >= 0) {
                glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, GLuint(batch.texture + 1));
            } else glDisable(GL_TEXTURE_2D);
            if (batch.cullBack) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
            glBegin(GL_TRIANGLES);
            for (unsigned i = 0; i < batch.count; ++i) {
                const auto& v = mesh_.vertices[batch.first + i];
                glColor3f(v.color[0] / 255.f, v.color[1] / 255.f, v.color[2] / 255.f);
                glTexCoord2f(v.s, v.t); glVertex3f(v.x, v.y, v.z);
            }
            glEnd();
        }
        // Development identity uses TinyGL's bundled public-domain bitmap font.
        glDrawText(reinterpret_cast<const unsigned char *>("SM64 BOB SCENE DEVELOPMENT"), 4, 4, 0xffffff);
        glDrawText(reinterpret_cast<const unsigned char *>("NO MARIO GAMEPLAY / AUDIO"), 4, 14, 0xffffff);
    }
    void present(SDL_Surface *surface) {
        require(surface && surface->w == 320 && surface->h == 240, "SDL scene surface must be 320x240");
        require(SDL_LockSurface(surface) == 0, "cannot lock software SDL surface");
        int result = SDL_ConvertPixels(320, 240, SDL_PIXELFORMAT_ARGB8888, buffer_->pbuf, buffer_->linesize,
                                       surface->format->format, surface->pixels, surface->pitch);
        SDL_UnlockSurface(surface);
        require(result == 0, "software framebuffer conversion failed");
    }
    std::uint64_t hash() const {
        std::uint64_t value = 1469598103934665603ull;
        for (unsigned i = 0; i < 320 * 240; ++i) {
            auto pixel = buffer_->pbuf[i];
            for (unsigned shift : {16u, 8u, 0u}) { value ^= (pixel >> shift) & 255; value *= 1099511628211ull; }
        }
        return value;
    }
    unsigned coloredPixels() const {
        unsigned count = 0;
        auto background = buffer_->pbuf[320 * 239];
        for (unsigned y = 30; y < 240; ++y)
            for (unsigned x = 0; x < 320; ++x) count += buffer_->pbuf[y * 320 + x] != background;
        return count;
    }
private:
    const SceneMesh& mesh_;
    ZBuffer *buffer_ = nullptr;
};

bool event(const SDL_Event& e, Camera& camera) {
    if (e.type == SDL_QUIT) return false;
    if (e.type == SDL_KEYDOWN) {
        switch (e.key.keysym.sym) {
            case SDLK_ESCAPE: return false;
            case SDLK_LEFT: camera.yaw -= 0.07f; break;
            case SDLK_RIGHT: camera.yaw += 0.07f; break;
            case SDLK_UP: camera.pitch += 0.06f; break;
            case SDLK_DOWN: camera.pitch -= 0.06f; break;
            case SDLK_LCTRL: case SDLK_w: camera.distance *= 0.93f; break;
            case SDLK_LALT: case SDLK_s: camera.distance *= 1.07f; break;
            case SDLK_TAB: camera.y += 150; break;
            case SDLK_BACKSPACE: camera.y -= 150; break;
            case SDLK_RETURN: camera = Camera{}; break;
            default: break;
        }
    }
    if (e.type == SDL_MOUSEMOTION && (e.motion.state & SDL_BUTTON_LMASK)) {
        camera.yaw += e.motion.xrel * 0.006f; camera.pitch += e.motion.yrel * 0.006f;
    }
    if (e.type == SDL_MOUSEWHEEL) camera.distance *= e.wheel.y > 0 ? 0.9f : 1.1f;
    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) return false;
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_START) camera = Camera{};
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_A) camera.distance *= 0.93f;
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_B) camera.distance *= 1.07f;
    }
    if (e.type == SDL_CONTROLLERAXISMOTION && std::abs(int(e.caxis.value)) > 9000) {
        float value = e.caxis.value / 32768.f;
        if (e.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTX) camera.yaw += value * 0.045f;
        if (e.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY) camera.pitch -= value * 0.04f;
        if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) camera.distance *= 1.f + value * 0.04f;
    }
    camera.clamp();
    return true;
}
}

int main(int argc, char **argv) {
    SDL_Window *window = nullptr;
    SDL_GameController *controller = nullptr;
    SDL_Surface *offscreen = nullptr;
    try {
        std::filesystem::path original, pack, snapshot;
        bool headless = false, orbit = false, inputCheck = false, vmInput = false;
        unsigned frames = 0;
        Camera camera;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            auto value = [&]() -> const char * { require(i + 1 < argc, "missing scene option value"); return argv[++i]; };
            if (arg == "--original") original = value();
            else if (arg == "--pack") pack = value();
            else if (arg == "--snapshot") snapshot = value();
            else if (arg == "--frames") {
                auto number = std::stoul(value()); require(number >= 1 && number <= 600, "invalid frame limit"); frames = number;
            } else if (arg == "--yaw") {
                camera.yaw = std::stof(value()); require(std::isfinite(camera.yaw) && std::abs(camera.yaw) <= 100, "invalid camera yaw");
            } else if (arg == "--headless") headless = true;
            else if (arg == "--auto-orbit") orbit = true;
#ifdef SM64_SCENE_TEST
            else if (arg == "--input-check") inputCheck = true;
            else if (arg == "--vm-input") vmInput = true;
#endif
            else throw std::runtime_error("unknown scene option: " + arg);
        }
        require(!pack.empty(), "usage: sm64-scene-dev --original original/ --pack .sm64/bob-us-v1.pdat");
        if (!original.empty()) prepare(discoverRom(original, bobDefinition), pack, bobDefinition);
        auto mesh = loadBobScene(pack);
        std::cout << "SM64 ROM-derived Bob-omb Battlefield: " << mesh.vertices.size()/3 << " triangles, "
                  << mesh.textures.size() << " textures, " << mesh.commands << " checked F3D commands\n"
                  << "TinyGL c2e48591a6bfba1a85f1b87a78dcf1abf7dff57a static CPU; no system GL/context/GPU\n";
        SDL_SetHint("SDL_JOYSTICK_THREAD", "0");
        require(SDL_Init(headless ? SDL_INIT_EVENTS : SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0, SDL_GetError());
        if (headless) {
            offscreen = SDL_CreateRGBSurfaceWithFormat(0, 320, 240, 32, SDL_PIXELFORMAT_ARGB8888);
            std::cout << "ScenePresentation=SDL-offscreen-test-surface\n";
        } else {
            window = SDL_CreateWindow("SM64 Native Scene Development - NO GAMEPLAY", SDL_WINDOWPOS_UNDEFINED,
                                      SDL_WINDOWPOS_UNDEFINED, 320, 240, SDL_WINDOW_SHOWN);
            require(window != nullptr, SDL_GetError());
            std::cout << "ScenePresentation=SDL-window Driver=" << SDL_GetCurrentVideoDriver() << '\n';
        }
        auto *surface = headless ? offscreen : SDL_GetWindowSurface(window);
        require(surface != nullptr, SDL_GetError());
        for (int i = 0; !headless && i < SDL_NumJoysticks(); ++i)
            if (SDL_IsGameController(i)) { controller = SDL_GameControllerOpen(i); break; }
        {
            Rasterizer raster(mesh);
            unsigned drawn = 0, colored = 0; std::uint64_t firstHash = 0, lastHash = 0;
            unsigned cameraKeys = 0; bool quitSeen = false;
            bool running = true;
            while (running && (!frames || drawn < frames)) {
                if (inputCheck && drawn == 1) {
                    SDL_Event key{}; key.type = SDL_KEYDOWN; key.key.keysym.sym = SDLK_RIGHT;
                    require(SDL_PushEvent(&key) == 1, "cannot inject SDL input check");
                }
                if (inputCheck && drawn == 3) {
                    SDL_Event quit{}; quit.type = SDL_QUIT;
                    require(SDL_PushEvent(&quit) == 1, "cannot inject SDL quit check");
                }
                SDL_Event e{};
                while (SDL_PollEvent(&e)) {
                    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_RIGHT) ++cameraKeys;
                    if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) quitSeen = true;
                    if (e.type == SDL_CONTROLLERDEVICEADDED && !controller && SDL_IsGameController(e.cdevice.which))
                        controller = SDL_GameControllerOpen(e.cdevice.which);
                    if (e.type == SDL_CONTROLLERDEVICEREMOVED && controller &&
                        SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) == e.cdevice.which) {
                        SDL_GameControllerClose(controller); controller = nullptr;
                    }
                    running = event(e, camera) && running;
                }
                if (!running) break;
                if (orbit) camera.yaw += 0.03f;
                raster.draw(camera); raster.present(surface);
                if (window) require(SDL_UpdateWindowSurface(window) == 0, SDL_GetError());
                lastHash = raster.hash(); if (!drawn) firstHash = lastHash;
                colored = raster.coloredPixels(); ++drawn;
                if (vmInput && drawn == 1) std::cout << "SceneInputReady=1" << std::endl;
                if ((!frames && !inputCheck) || vmInput) SDL_Delay(16);
            }
            if (frames || inputCheck || !snapshot.empty())
                require(drawn > 0 && colored > 5000, "ROM scene rendered blank or insufficient geometry");
            if (inputCheck) require(drawn == 3 && lastHash != firstHash, "SDL camera/quit integration check failed");
            if (vmInput) require(cameraKeys > 0 && quitSeen && lastHash != firstHash, "VM window keyboard/camera/quit check failed");
            if (!snapshot.empty()) {
                int fd = ::open(snapshot.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
                require(fd >= 0, "snapshot must be a new private file; never overwrite ROM/cache/user files");
                auto *file = ::fdopen(fd, "wb");
                if (!file) { ::close(fd); throw std::runtime_error("cannot open snapshot stream"); }
                auto *stream = SDL_RWFromFP(file, SDL_TRUE);
                if (!stream) { std::fclose(file); throw std::runtime_error("cannot create snapshot writer"); }
                require(SDL_SaveBMP_RW(surface, stream, 1) == 0, "cannot save private scene snapshot");
            }
            std::cout << "SceneFrames=" << drawn << " ScenePixels=" << colored << " FirstPixelHash=" << firstHash
                      << " LastPixelHash=" << lastHash << " InputCheck=" << (inputCheck ? "PASS" : "not-requested") << '\n';
            if (vmInput) std::cout << "SceneVMInput=PASS CameraKeys=" << cameraKeys << " QuitSeen=" << quitSeen << '\n';
        }
        if (controller) SDL_GameControllerClose(controller);
        controller = nullptr;
        if (offscreen) SDL_FreeSurface(offscreen);
        offscreen = nullptr;
        SDL_DestroyWindow(window); window = nullptr; SDL_Quit();
        rusage usage{}; if (getrusage(RUSAGE_SELF, &usage) == 0) std::cout << "ScenePeakRSSKB=" << usage.ru_maxrss << '\n';
        std::cout << "SM64_NATIVE_ROM_SCENE_GAMEPLAY_UNIMPLEMENTED\n";
        return 0;
    } catch (const std::exception& e) {
        if (controller) SDL_GameControllerClose(controller);
        if (offscreen) SDL_FreeSurface(offscreen);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        std::cerr << "SM64 scene development: " << e.what() << '\n';
        return 1;
    }
}
