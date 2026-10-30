#pragma once
#include <cstdint>
#include <memory>
#include <string>
struct SDL_Renderer;
namespace lilsmoke::media {
class NativeVideo {
public:
    NativeVideo();
    ~NativeVideo();
    NativeVideo(const NativeVideo&)=delete;
    NativeVideo& operator=(const NativeVideo&)=delete;
    static bool available();
    bool openDisplay(int width,int height);
    bool start(const std::string &file,int sourceWidth,int sourceHeight);
    void update();
    void play();
    void pause(bool value);
    bool seek(double seconds);
    void aspect(int sourceWidth,int sourceHeight,bool fill);
    bool present(SDL_Renderer *renderer);
    bool ready()const;
    double position()const;
    double duration()const;
    const std::string &error()const;
    bool stop();
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
