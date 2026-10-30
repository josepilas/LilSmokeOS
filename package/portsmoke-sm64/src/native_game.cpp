// SPDX-License-Identifier: MPL-2.0
#include "gameplay.hpp"
#include "loading_ui.hpp"
#include "mario_actor.hpp"
#include "course_objects.hpp"
#include "sound_effects.hpp"
#include <SDL.h>
extern "C" {
#include <TGL/gl.h>
#include <zbuffer.h>
}
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <sys/resource.h>
#include <unistd.h>

using namespace portsmoke::sm64;
namespace {
void check(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}
struct Camera {
    float x = 0, y = 0, z = 0, yaw = 0, pitch = 0.35f, distance = 850;
    void follow(const PlayerState& p, bool reset = false)
    {
        float speed = reset ? 1.f : 0.25f;
        x += (p.position.x - x) * speed;
        y += (p.position.y + 110 - y) * ((!p.grounded && p.velocityY < 0) ? 1.f : speed);
        z += (p.position.z - z) * speed;
        if (reset)
            yaw = p.yaw + 3.1415926536f;
    }
};
Position unit(Position p)
{
    float length = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    check(length > 0.0001f, "invalid camera vector");
    return { p.x / length, p.y / length, p.z / length };
}
Position cross(Position a, Position b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
class Rasterizer {
public:
    Rasterizer(const SceneMesh& terrain, const MarioActor& actor, const CollisionWorld& world, const ObjectGraphics& objects)
        : terrain_(terrain)
        , actor_(actor)
        , world_(world)
        , objects_(objects)
    {
        buffer_ = ZB_open(320, 240, ZB_MODE_RGBA, nullptr);
        check(buffer_, "software framebuffer allocation failed");
        glInit(buffer_);
        glViewport(0, 0, 320, 240);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glFrontFace(GL_CCW);
        terrainBase_ = upload(terrain_);
        for (const auto& part : actor.parts)
            partBases_.push_back(upload(part.mesh));
        for (unsigned i=0;i<4;++i) coinBases_[i]=upload(objects.coins[i]);
        starBase_=upload(objects.star);
        headBase_=upload(objects.goombaHead);bodyBase_=upload(objects.goombaBody);
        leftFootBase_=upload(objects.goombaLeftFoot);rightFootBase_=upload(objects.goombaRightFoot);
        check(nextTexture_ <= 256, "native actor texture budget exceeded");
    }
    ~Rasterizer()
    {
        glClose();
        ZB_close(buffer_);
    }
    void draw(const Camera& c, const PlayerState& player, const CourseState& course, bool paused, int selection)
    {
        glClearColor(0.45f, 0.69f, 0.91f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        float near = 20, height = near * std::tan(0.5235987756f);
        glFrustum(-height * 4 / 3, height * 4 / 3, -height, height, near, 50000);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        Position direction { std::cos(c.pitch) * std::sin(c.yaw), std::sin(c.pitch), std::cos(c.pitch) * std::cos(c.yaw) };
        float distance = std::max(35.f, world_.cameraDistance({ c.x, c.y, c.z }, direction, c.distance) - 25);
        Position eye { c.x + distance * direction.x, c.y + distance * direction.y, c.z + distance * direction.z };
        auto forward = unit({ c.x - eye.x, c.y - eye.y, c.z - eye.z });
        auto side = unit(cross(forward, { 0, 1, 0 }));
        auto up = cross(side, forward);
        float view[16] { side.x, up.x, -forward.x, 0, side.y, up.y, -forward.y, 0, side.z, up.z, -forward.z, 0, 0, 0, 0, 1 };
        glMultMatrixf(view);
        glTranslatef(-eye.x, -eye.y, -eye.z);
        mesh(terrain_, terrainBase_);
        for(const auto& object:course.objects) {
            if(!object.active || std::hypot(object.position.x-player.position.x,object.position.z-player.position.z)>5000)continue;
            glPushMatrix();glTranslatef(object.position.x,object.position.y,object.position.z);
            if(object.kind==ObjectKind::Coin||object.kind==ObjectKind::RedCoin) {
                glRotatef(c.yaw*57.2957795131f,0,1,0);
                unsigned frame=(course.ticks/4)%4;
                mesh(objects_.coins[frame],coinBases_[frame],object.kind==ObjectKind::RedCoin?1:2);
            } else if(object.kind==ObjectKind::Star) {
                glRotatef(course.ticks*4.f,0,1,0);mesh(objects_.star,starBase_);
            } else {
                glRotatef(object.yaw*57.2957795131f,0,1,0);glScalef(0.25f,0.25f,0.25f);
                glPushMatrix();glTranslatef(48,0,0);mesh(objects_.goombaHead,headBase_);glPopMatrix();
                glPushMatrix();glRotatef((c.yaw-object.yaw)*57.2957795131f,0,1,0);mesh(objects_.goombaBody,bodyBase_);glPopMatrix();
                float swing=std::sin(object.age*0.2f)*16;
                glPushMatrix();glTranslatef(-60,-16,45);glRotatef(swing,0,0,1);mesh(objects_.goombaLeftFoot,leftFootBase_);glPopMatrix();
                glPushMatrix();glTranslatef(-60,-16,-45);glRotatef(-swing,0,0,1);mesh(objects_.goombaRightFoot,rightFootBase_);glPopMatrix();
            }
            glPopMatrix();
        }
        auto motion = player.grounded ? (player.action == PlayerAction::Walking ? MarioMotion::Walking : MarioMotion::Idle) : MarioMotion::SingleJump;
        auto pose = sampleMarioPose(actor_, motion, player.animationFrame);
        glPushMatrix();
        glTranslatef(player.position.x, player.position.y, player.position.z);
        glRotatef(player.yaw * 57.2957795131f, 0, 1, 0);
        for (std::size_t i = 0; i < actor_.parts.size(); ++i) {
            glPushMatrix();
            glMultMatrixf(pose.jointMatrices[actor_.parts[i].joint].data());
            mesh(actor_.parts[i].mesh, partBases_[i]);
            glPopMatrix();
        }
        glPopMatrix();
        text("SM64 NATIVE PROTOTYPE", 4, 4, 0xffffff);
        char stats[64];unsigned stars=0;
        for(unsigned bits=course.stars;bits;bits>>=1)stars+=bits&1;
        std::snprintf(stats,sizeof(stats),"COINS %u  RED %u/8  STARS %u",course.coins,course.redCoins,stars);
        text(stats,4,16,0xffe34d);
        std::snprintf(stats,sizeof(stats),"HEALTH %u/8",course.health);text(stats,4,28,0xffffff);
        if (paused) {
            for (int y = 70; y < 170; ++y)
                for (int x = 60; x < 260; ++x)
                    buffer_->pbuf[y * 320 + x] = 0xff172127;
            text("PAUSED", 136, 82, 0xffffff);
            const char* labels[] { "RESUME", "RESTART LEVEL", "EXIT" };
            for (int i = 0; i < 3; ++i) {
                text(labels[i], 102, 106 + i * 18, i == selection ? 0x7ed6ed : 0xffffff);
                if (i == selection)
                    text(">", 86, 106 + i * 18, 0x7ed6ed);
            }
        }
    }
    void present(SDL_Surface* surface)
    {
        check(surface && surface->w == 320 && surface->h == 240, "native viewport must be 320x240");
        check(SDL_LockSurface(surface) == 0, SDL_GetError());
        int result = SDL_ConvertPixels(320, 240, SDL_PIXELFORMAT_ARGB8888, buffer_->pbuf, buffer_->linesize,
            surface->format->format, surface->pixels, surface->pitch);
        SDL_UnlockSurface(surface);
        check(result == 0, "software presentation conversion failed");
    }
    std::uint64_t hash() const
    {
        std::uint64_t hash = 1469598103934665603ull;
        for (unsigned i = 0; i < 320 * 240; ++i)
            for (unsigned shift : { 16u, 8u, 0u }) {
                hash ^= (buffer_->pbuf[i] >> shift) & 255;
                hash *= 1099511628211ull;
            }
        return hash;
    }

private:
    static void text(const char* s, int x, int y, unsigned color) { glDrawText(reinterpret_cast<const unsigned char*>(s), x, y, color); }
    unsigned upload(const SceneMesh& mesh)
    {
        unsigned base = nextTexture_;
        for (const auto& texture : mesh.textures) {
            check(nextTexture_ < 256, "texture budget exceeded");
            const auto& image = texture.image;
            std::vector<std::uint8_t> rgb(std::size_t(image.width) * image.height * 3);
            for (std::size_t i = 0; i < rgb.size() / 3; ++i) {
                if (image.rgba[i * 4 + 3])
                    std::copy_n(image.rgba.data() + i * 4, 3, rgb.data() + i * 3);
                else {
                    rgb[i * 3] = 255;
                    rgb[i * 3 + 1] = 0;
                    rgb[i * 3 + 2] = 255;
                }
            }
            glBindTexture(GL_TEXTURE_2D, nextTexture_++);
            glTexImage2D(GL_TEXTURE_2D, 0, 3, image.width, image.height, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
        }
        return base;
    }
    static void mesh(const SceneMesh& mesh, unsigned base, unsigned tint=0)
    {
        for (const auto& batch : mesh.batches) {
            check(batch.first <= mesh.vertices.size() && batch.count <= mesh.vertices.size() - batch.first, "invalid mesh batch");
            if (batch.texture >= 0) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, base + unsigned(batch.texture));
            } else
                glDisable(GL_TEXTURE_2D);
            if (batch.cullBack)
                glEnable(GL_CULL_FACE);
            else
                glDisable(GL_CULL_FACE);
            glBegin(GL_TRIANGLES);
            for (unsigned i = 0; i < batch.count; ++i) {
                const auto& v = mesh.vertices[batch.first + i];
                glColor3f(v.color[0] / 255.f, v.color[1] / 255.f*(tint==1?0.18f:tint==2?0.85f:1.f), v.color[2] / 255.f*(tint?0.1f:1.f));
                glTexCoord2f(v.s, v.t);
                glVertex3f(v.x, v.y, v.z);
            }
            glEnd();
        }
    }
    ZBuffer* buffer_ = nullptr;
    const SceneMesh& terrain_;
    const MarioActor& actor_;
    const CollisionWorld& world_;
    const ObjectGraphics& objects_;
    unsigned nextTexture_ = 1, terrainBase_ = 0;
    std::array<unsigned,4> coinBases_{};
    unsigned starBase_=0,headBase_=0,bodyBase_=0,leftFootBase_=0,rightFootBase_=0;
    std::vector<unsigned> partBases_;
};
struct Controls {
    bool left = false, right = false, up = false, down = false, jump = false, pressed = false, paused = false, quit = false, reset = false;
    float axisX = 0, axisY = 0, orbit = 0;
    int selection = 0;
    unsigned pauses = 0;
    void confirm()
    {
        if (selection == 0)
            paused = false;
        else if (selection == 1) {
            reset = true;
            paused = false;
        } else
            quit = true;
    }
    void menu(int direction) { selection = (selection + direction + 3) % 3; }
    void key(SDL_Keycode key, bool held, bool repeat = false)
    {
        if (key == SDLK_ESCAPE && held) {
            quit = true;
            return;
        }
        if (key == SDLK_RETURN && held && !repeat) {
            paused = !paused;
            if (paused) {
                selection = 0;
                ++pauses;
            }
            return;
        }
        if (paused && held && !repeat) {
            if (key == SDLK_UP || key == SDLK_w)
                menu(-1);
            if (key == SDLK_DOWN || key == SDLK_s)
                menu(1);
            if (key == SDLK_LCTRL || key == SDLK_SPACE)
                confirm();
            return;
        }
        if (key == SDLK_LEFT || key == SDLK_a)
            left = held;
        if (key == SDLK_RIGHT || key == SDLK_d)
            right = held;
        if (key == SDLK_UP || key == SDLK_w)
            up = held;
        if (key == SDLK_DOWN || key == SDLK_s)
            down = held;
        if (key == SDLK_LCTRL || key == SDLK_SPACE) {
            if (held && !jump && !repeat)
                pressed = true;
            jump = held;
        }
        if (key == SDLK_TAB && held)
            reset = true;
    }
    void event(const SDL_Event& event)
    {
        if (event.type == SDL_QUIT)
            quit = true;
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
            key(event.key.keysym.sym, event.type == SDL_KEYDOWN, event.key.repeat);
        if (event.type == SDL_CONTROLLERAXISMOTION) {
            float value = event.caxis.value / 32768.f;
            value = std::abs(value) < 0.2f ? 0.f : (value - std::copysign(0.2f, value)) / 0.8f;
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
                axisX = value;
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
                axisY = -value;
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTX)
                orbit = value;
        }
        if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) {
            bool held = event.type == SDL_CONTROLLERBUTTONDOWN;
            switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_A:
                key(SDLK_LCTRL, held);
                break;
            case SDL_CONTROLLER_BUTTON_START:
                key(SDLK_RETURN, held);
                break;
            case SDL_CONTROLLER_BUTTON_BACK:
                key(SDLK_ESCAPE, held);
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                key(SDLK_UP, held);
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                key(SDLK_DOWN, held);
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                key(SDLK_LEFT, held);
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                key(SDLK_RIGHT, held);
                break;
            default:
                break;
            }
        }
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            clear();
    }
    void clear()
    {
        left = right = up = down = jump = pressed = false;
        axisX = axisY = orbit = 0;
    }
    PlayerInput take(float yaw)
    {
        PlayerInput input { axisX + right - left, axisY + up - down, yaw, pressed, jump };
        pressed = false;
        return input;
    }
};
void snapshot(SDL_Surface* surface, const std::filesystem::path& path)
{
    int fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
    check(fd >= 0, "snapshot must be a new private file");
    auto* file = ::fdopen(fd, "wb");
    if (!file) {
        ::close(fd);
        throw std::runtime_error("snapshot stream failed");
    }
    auto* stream = SDL_RWFromFP(file, SDL_TRUE);
    if (!stream) {
        std::fclose(file);
        throw std::runtime_error("snapshot writer failed");
    }
    check(SDL_SaveBMP_RW(surface, stream, 1) == 0, "snapshot write failed");
}
}
int main(int argc, char** argv)
{
    SDL_Window* window = nullptr;
    SDL_GameController* controller = nullptr;
    SDL_Surface* offscreen = nullptr;
    try {
        std::filesystem::path original, pack, picture, icon;
        bool headless = false, qa = false, vm = false;
        [[maybe_unused]] bool objectPreview = false;
        [[maybe_unused]] bool qaPad = false;
        unsigned frames = 0;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            auto value = [&] {check(i+1<argc,"missing option value");return argv[++i]; };
            if (arg == "--original")
                original = value();
            else if (arg == "--pack")
                pack = value();
            else if (arg == "--snapshot")
                picture = value();
            else if (arg == "--icon")
                icon = value();
            else if (arg == "--headless")
                headless = true;
            else if (arg == "--frames") {
                frames = std::stoul(value());
                check(frames > 0 && frames <= 3600, "frame budget");
            }
#ifdef SM64_SCENE_TEST
            else if (arg == "--gameplay-check")
                qa = true;
            else if (arg == "--gameplay-pad-check")
                qa = qaPad = true;
            else if (arg == "--vm-input")
                vm = true;
            else if (arg == "--object-preview")
                objectPreview = true;
#endif
            else
                throw std::runtime_error("unknown native option: " + arg);
        }
        check(!pack.empty(), "usage: sm64-scene-dev --original original/ --pack .sm64/bob-us-v1.pdat");
        auto actorPack = pack.parent_path() / "mario-us-v1.pdat";
        SDL_SetHint("SDL_JOYSTICK_THREAD", "0");
        check(SDL_Init(headless ? SDL_INIT_EVENTS : SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0, SDL_GetError());
        if (headless)
            offscreen = SDL_CreateRGBSurfaceWithFormat(0, 320, 240, 32, SDL_PIXELFORMAT_ARGB8888);
        else {
            window = SDL_CreateWindow("SM64 Native Prototype", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 320, 240, SDL_WINDOW_SHOWN);
            check(window, SDL_GetError());
        }
        auto* surface = headless ? offscreen : SDL_GetWindowSurface(window);
        check(surface, SDL_GetError());
        for (int i = 0; !headless && i < SDL_NumJoysticks(); ++i)
            if (SDL_IsGameController(i)) {
                controller = SDL_GameControllerOpen(i);
                break;
            }
        std::cout << "NativeGamePresentation=" << (headless ? "SDL-offscreen" : "SDL-window")
                  << " Driver=" << (headless ? "none" : SDL_GetCurrentVideoDriver()) << '\n';
        if (original.empty())
            original = pack.parent_path().parent_path() / "original";
        if (icon.empty())
            icon = pack.parent_path().parent_path() / "icon.webp";
        check(!std::filesystem::is_symlink(pack.parent_path()), "Cache directory must not be a symlink.");
        std::filesystem::create_directories(pack.parent_path());
        if (!showExtraction(surface, window, original, pack.parent_path(), icon, headless)) {
            if (controller)
                SDL_GameControllerClose(controller);
            if (offscreen)
                SDL_FreeSurface(offscreen);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 0;
        }
        auto mesh = loadBobScene(pack);
        auto world = loadBobCollision(pack);
        auto actor = loadMarioActor(actorPack);
        auto objectGraphics=loadObjectGraphics(pack.parent_path());
        auto course=loadBobObjects(pack.parent_path(),world);
        auto progressFile=pack.parent_path().parent_path()/"saves/bob-progress.dat";
        loadCourseSave(course,progressFile);
        PlayerState player;
        resetPlayer(player, world);
        if (objectPreview) {
            auto enemy=std::find_if(course.objects.begin(),course.objects.end(),[](const auto& o){return o.kind==ObjectKind::Goomba;});
            check(enemy!=course.objects.end(),"no ROM enemy to preview");
            player.position={enemy->position.x-250,enemy->position.y,enemy->position.z+250};
            player.position.y=world.floor(player.position.x,player.position.y+500,player.position.z).height;
            player.yaw=2.3561944902f;
        }
        Position spawn = player.position;
        Camera camera;
        camera.follow(player, true);
        if (objectPreview) camera.yaw+=0.8f;
        Controls controls;
        std::cout << "SM64NativePrototype=0.5 CPU=TinyGL Viewport=320x240 TerrainTriangles=" << mesh.vertices.size() / 3
                  << " CollisionSurfaces=" << world.size() << " MarioParts=" << actor.parts.size() << '\n';
        std::cout << "NativeCourseObjects=" << course.objects.size() << " RedCoinGoal=8 Save=atomic-star-bits\n";
        if (qa && !frames)
            frames = 150;
        {
            SoundEffects sound(pack.parent_path(),headless);
            Rasterizer raster(mesh, actor, world,objectGraphics);
            bool audioPaused=false;
            unsigned drawn = 0, ticks = 0, pausedFrames = 0;
            std::uint64_t firstHash = 0, lastHash = 0;
            auto now = SDL_GetTicks();
            float accumulator = 0, maxTravel = 0, maxHeight = player.position.y;
            bool inputSeen = false, quitSeen = false;
            while (!controls.quit && (!frames || drawn < frames)) {
#ifdef SM64_SCENE_TEST
                if (qa) {
                    auto key = [&](SDL_Keycode k, bool down) {
                        SDL_Event e {};
                        if (qaPad && (k == SDLK_UP || k == SDLK_RIGHT)) {
                            e.type = SDL_CONTROLLERAXISMOTION;
                            e.caxis.axis = k == SDLK_UP ? SDL_CONTROLLER_AXIS_LEFTY : SDL_CONTROLLER_AXIS_LEFTX;
                            e.caxis.value = down ? (k == SDLK_UP ? -32768 : 32767) : 0;
                        } else if (qaPad) {
                            e.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
                            e.cbutton.button = k == SDLK_RETURN ? SDL_CONTROLLER_BUTTON_START : SDL_CONTROLLER_BUTTON_A;
                        } else {
                            e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
                            e.key.keysym.sym = k;
                        }
                        check(SDL_PushEvent(&e) == 1, "SDL QA input failed");
                    };
                    if (drawn == 0)
                        key(SDLK_UP, true);
                    if (drawn == 25)
                        key(SDLK_UP, false);
                    if (drawn == 35)
                        key(SDLK_LCTRL, true);
                    if (drawn == 41)
                        key(SDLK_LCTRL, false);
                    if (drawn == 65 || drawn == 75) {
                        key(SDLK_RETURN, true);
                        key(SDLK_RETURN, false);
                    }
                    if (drawn == 95)
                        key(SDLK_RIGHT, true);
                    if (drawn == 115)
                        key(SDLK_RIGHT, false);
                }
#endif
                SDL_Event event {};
                while (SDL_PollEvent(&event)) {
                    inputSeen = inputSeen || event.type == SDL_KEYDOWN || event.type == SDL_CONTROLLERBUTTONDOWN;
                    quitSeen = quitSeen || event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE);
                    if (event.type == SDL_CONTROLLERDEVICEADDED && !controller && SDL_IsGameController(event.cdevice.which))
                        controller = SDL_GameControllerOpen(event.cdevice.which);
                    if (event.type == SDL_CONTROLLERDEVICEREMOVED && controller && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) == event.cdevice.which) {
                        SDL_GameControllerClose(controller);
                        controller = nullptr;
                        controls.clear();
                    }
                    controls.event(event);
                }
                if (controls.quit)
                    break;
                if (controls.reset) {
                    resetPlayer(player, world);
                    unsigned stars=course.stars;
                    course=loadBobObjects(pack.parent_path(),world);course.stars=stars;
                    for(auto& object:course.objects)if(object.kind==ObjectKind::Star&&(stars&object.starBit))object.active=false;
                    camera.follow(player, true);
                    controls.reset = false;
                    controls.clear();
                }
                auto frameStart = SDL_GetTicks();
                accumulator += std::min(float(frameStart - now), 133.3333f);
                now = frameStart;
                if (headless || qa)
                    accumulator = 1000.f / 30;
                if (controls.paused) {
                    accumulator = 0;
                    ++pausedFrames;
                    controls.pressed = false;
                }
                if(audioPaused!=controls.paused){audioPaused=controls.paused;sound.pause(audioPaused);}
                unsigned catchup = 0;
                while (accumulator >= 1000.f / 30 && ++catchup <= 4) {
                    camera.yaw += controls.orbit * 0.055f;
                    unsigned jumps=player.jumps,landings=player.landings;
                    stepPlayer(player, controls.take(camera.yaw), world);
                    if(player.jumps!=jumps)sound.play(SoundEffect::Jump);
                    if(player.landings!=landings)sound.play(SoundEffect::Land);
                    auto interactions=stepObjects(course,player,world);
                    if(interactions.hurt)sound.play(SoundEffect::Hurt);
                    if(interactions.stomp)sound.play(SoundEffect::Land);
                    if(interactions.star)saveCourse(course,progressFile);
                    camera.follow(player);
                    accumulator -= 1000.f / 30;
                    ++ticks;
                }
                maxTravel = std::max(maxTravel, std::hypot(player.position.x - spawn.x, player.position.z - spawn.z));
                maxHeight = std::max(maxHeight, player.position.y);
                raster.draw(camera, player, course, controls.paused, controls.selection);
                raster.present(surface);
                if (window)
                    check(SDL_UpdateWindowSurface(window) == 0, SDL_GetError());
                if (!drawn)
                    firstHash = raster.hash();
                ++drawn;
                if (window && drawn == 1)
                    std::cout << "NativeGameInputReady=1" << std::endl;
                if (window && !qa) {
                    auto work = SDL_GetTicks() - frameStart;
                    if (work < 33)
                        SDL_Delay(33 - work);
                }
            }
            lastHash = raster.hash();
            if (qa && drawn >= 150)
                check(player.jumps >= 1 && player.landings >= 1 && maxTravel > 150 && pausedFrames >= 10 && controls.pauses == 1 && firstHash != lastHash, "native SDL gameplay check failed");
            if (vm)
                check(inputSeen && quitSeen && firstHash != lastHash && player.jumps >= 1, "VM gameplay input/exit failed");
            if (!picture.empty())
                snapshot(surface, picture);
            std::cout << "NativeGameFrames=" << drawn << " SimulationTicks=" << ticks << " PlayerJumps=" << player.jumps << " PlayerLandings=" << player.landings
                      << " WallHits=" << player.wallHits << " Pauses=" << controls.pauses << " PausedFrames=" << pausedFrames << " Travel=" << maxTravel
                      << " PeakY=" << maxHeight << " FirstPixelHash=" << firstHash << " LastPixelHash=" << lastHash << '\n';
            if (qa)
                std::cout << "NativeGameplayCheck=" << (drawn >= 150 ? "PASS" : "PARTIAL") << '\n';
            if (vm)
                std::cout << "NativeGameVMInput=PASS\n";
        }
        if (controller)
            SDL_GameControllerClose(controller);
        controller = nullptr;
        if (offscreen)
            SDL_FreeSurface(offscreen);
        offscreen = nullptr;
        SDL_DestroyWindow(window);
        window = nullptr;
        SDL_Quit();
        rusage usage {};
        if (!getrusage(RUSAGE_SELF, &usage))
            std::cout << "NativeGamePeakRSSKB=" << usage.ru_maxrss << '\n';
        std::cout << "SM64_NATIVE_MOVEMENT_PROTOTYPE_NOT_FULL_GAME\n";
        return 0;
    } catch (const std::exception& error) {
        if (controller)
            SDL_GameControllerClose(controller);
        if (offscreen)
            SDL_FreeSurface(offscreen);
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
        std::cerr << "SM64 native prototype: " << error.what() << '\n';
        return 1;
    }
}
