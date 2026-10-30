#pragma once
#include "Decoder.h"
#include "PlaybackClock.h"
#include "m8_pocketsnes_audio.h"
#include <SDL2/SDL.h>
#include <array>
#include <algorithm>
#include <cerrno>
#include <string>
namespace lilsmoke::media {
class AudioSink {
public:
    ~AudioSink(){close();}
    bool open() {
        close();epochSubmitted_=0;clock_.reset();native_=m8_pcm_requested()!=0;
        if(native_)return m8_pcm_init(44100,2,16)!=0;
        if(SDL_InitSubSystem(SDL_INIT_AUDIO)<0)return false;
        SDL_AudioSpec spec{};spec.freq=44100;spec.format=AUDIO_S16SYS;spec.channels=2;spec.samples=1024;
        device_=SDL_OpenAudioDevice(nullptr,0,&spec,nullptr,0);
        if(device_)SDL_PauseAudioDevice(device_,0);return device_!=0;
    }
    void close() {if(native_){m8_pcm_close();native_=false;}if(device_){SDL_CloseAudioDevice(device_);device_=0;}}
    void pause(bool value) {
        if(native_){if(value)m8_pcm_close();else {epochSubmitted_=0;clock_.reset();m8_pcm_init(44100,2,16);}}
        if(!value&&device_){SDL_ClearQueuedAudio(device_);epochSubmitted_=0;}
        if(device_)SDL_PauseAudioDevice(device_,value);
    }
    void flush() {epochSubmitted_=0;clock_.reset();if(native_){m8_pcm_close();m8_pcm_init(44100,2,16);}if(device_)SDL_ClearQueuedAudio(device_);}
    bool feed(Decoder &decoder,int volume,bool muted) {
        for(unsigned n=0;n<32&&decoder.audioFrames();++n) {
            size_t room=735;
            if(native_) {
                m8_pcm_stats stats{};m8_pcm_get_stats(&stats);
                if(!stats.ready||stats.inflight>=M8_PCM_INFLIGHT_LIMIT)return false;
                const uint64_t consumed=played(),ahead=epochSubmitted_>consumed?epochSubmitted_-consumed:0;
                const uint64_t capacity=735U*M8_PCM_INFLIGHT_LIMIT;
                if(ahead>=capacity)return false;
                room=size_t(std::min<uint64_t>(room,capacity-ahead));
            }
            else if(!device_||SDL_GetQueuedAudioSize(device_)>=735U*4*4)return false;
            size_t frames=std::min(room,decoder.audioFrames());std::array<int16_t,735*2> samples{};
            int gain=muted?0:std::clamp(volume,0,100);
            for(size_t i=0;i<frames*2;++i)samples[i]=int16_t(int(decoder.audio[decoder.audioHead+i])*gain/100);
            int accepted=native_?m8_pcm_send(samples.data(),frames):SDL_QueueAudio(device_,samples.data(),Uint32(frames*4))==0;
            if(accepted<=0)return false;
            decoder.consumeAudio(frames);submitted_+=frames;epochSubmitted_+=frames;
        }
        return true;
    }
    bool empty()const {
        if(native_){m8_pcm_stats stats{};m8_pcm_get_stats(&stats);return !stats.inflight&&played()>=epochSubmitted_;}
        return !device_||SDL_GetQueuedAudioSize(device_)==0;
    }
    uint64_t submitted()const{return submitted_;}
    uint64_t played()const {
        if(native_){m8_pcm_stats stats{};m8_pcm_get_stats(&stats);return clock_.played(stats.delivered_frames,SDL_GetTicks64());}
        uint64_t queued=device_?SDL_GetQueuedAudioSize(device_)/4:0;
        return epochSubmitted_>queued?epochSubmitted_-queued:0;
    }
    bool failed()const {
        if(native_){m8_pcm_stats stats{};m8_pcm_get_stats(&stats);return stats.error!=0&&!m8_pcm_active();}
        return false;
    }
    std::string failure()const {
        m8_pcm_stats stats{};if(native_)m8_pcm_get_stats(&stats);
        return stats.error==ETIMEDOUT?"Audio hardware timed out":"Audio output failed";
    }
private:
    SDL_AudioDeviceID device_=0;
    bool native_=false;
    uint64_t submitted_=0;
    uint64_t epochSubmitted_=0;
    PlaybackClock clock_;
};
}
