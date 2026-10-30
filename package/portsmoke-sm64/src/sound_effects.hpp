#pragma once
#include "asset_pack.hpp"
#include <SDL.h>
#include <array>

namespace portsmoke::sm64 {
struct RomSound {
    std::vector<std::int16_t> pcm;
    unsigned rate = 32000;
};
// Bounded US control/sample banks, not a synthesized replacement sound.
RomSound loadRomSound(const std::filesystem::path& cache, unsigned bank, unsigned instrument);
std::vector<std::int16_t> decodeVadpcm(const std::vector<std::uint8_t>& encoded,
                                      const std::vector<std::int16_t>& book, unsigned predictors);
enum class SoundEffect { Jump, Land, Hurt };
class SoundEffects {
public:
    explicit SoundEffects(const std::filesystem::path& cache, bool silent=false);
    ~SoundEffects();
    void play(SoundEffect effect);
    void pause(bool paused);
    bool available() const { return device_!=0; }
private:
    struct Voice { const RomSound* sound=nullptr; unsigned position=0, fraction=0; };
    static void mix(void*,Uint8*,int);
    SDL_AudioDeviceID device_=0;
    std::array<RomSound,3> sounds_;
    std::array<Voice,4> voices_{};
};
}
