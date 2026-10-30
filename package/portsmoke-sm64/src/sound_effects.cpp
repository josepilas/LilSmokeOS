// SPDX-License-Identifier: MPL-2.0
#include "sound_effects.hpp"
#include "extraction.hpp"
#include "runtime_assets.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace portsmoke::sm64 {
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
unsigned word(const std::vector<std::uint8_t>& b,std::size_t at) {
    check(at<=b.size()&&b.size()-at>=4,"truncated SM64 sound bank");
    return unsigned(b[at])<<24|unsigned(b[at+1])<<16|unsigned(b[at+2])<<8|b[at+3];
}
unsigned half(const std::vector<std::uint8_t>& b,std::size_t at) {
    check(at<=b.size()&&b.size()-at>=2,"truncated SM64 sound codebook");
    return unsigned(b[at])*256+b[at+1];
}
std::int16_t signedHalf(unsigned n){return std::int16_t(n<32768?int(n):int(n)-65536);}
std::int64_t floor2048(std::int64_t n){auto q=n/2048;return q-(n%2048<0);}
}

std::vector<std::int16_t> decodeVadpcm(const std::vector<std::uint8_t>& encoded,
                                      const std::vector<std::int16_t>& book,unsigned predictors) {
    check(!encoded.empty()&&encoded.size()%9==0&&encoded.size()<=36864,"invalid bounded VADPCM frame data");
    check(predictors>0&&predictors<=8&&book.size()==predictors*16,"unsupported VADPCM predictor book");
    std::vector<std::int16_t> out;out.reserve(encoded.size()/9*16);
    std::array<std::int32_t,16> state{};
    // Order-2 recurrence from the upstream aifc_decode runtime decode path.
    // Keep unclipped predictor history; clip only the emitted PCM sample.
    for(std::size_t at=0;at<encoded.size();at+=9) {
        unsigned exponent=encoded[at]>>4,predictor=encoded[at]&15;
        check(exponent<=15&&predictor<predictors,"invalid VADPCM frame predictor");
        std::int32_t residual[16];
        for(unsigned i=0;i<16;++i) {
            unsigned nibble=(encoded[at+1+i/2]>>(i%2?0:4))&15;
            residual[i]=(nibble<8?int(nibble):int(nibble)-16)*int(1u<<exponent);
        }
        auto* coefficients=book.data()+predictor*16;
        for(unsigned group=0;group<2;++group) {
            auto old0=state[group?6:14],old1=state[group?7:15];
            for(unsigned i=0;i<8;++i) {
                std::int64_t sum=std::int64_t(coefficients[i])*old0+std::int64_t(coefficients[8+i])*old1;
                for(unsigned j=0;j<i;++j)sum+=std::int64_t(coefficients[8+i-j-1])*residual[group*8+j];
                auto decoded=floor2048(sum)+residual[group*8+i];
                check(decoded>=-1048576&&decoded<=1048576,"VADPCM predictor overflow");
                state[group*8+i]=std::int32_t(decoded);
                out.push_back(std::int16_t(std::clamp(decoded,std::int64_t(-32768),std::int64_t(32767))));
            }
        }
    }
    return out;
}

RomSound loadRomSound(const std::filesystem::path& cache,unsigned bank,unsigned instrument) {
    RuntimeAssets assets(cache/"sm64-us-v1.pdat",allAssetDefinition());
    auto ctl=assets.pack().load("sound/sound_data.ctl.be");
    check(half(ctl,0)==1&&bank<half(ctl,2),"invalid US sound-bank index");
    unsigned offset=word(ctl,4+bank*8),size=word(ctl,8+bank*8);
    check(offset<=ctl.size()&&size<=ctl.size()-offset&&size>=20,"invalid control-bank bounds");
    std::vector<std::uint8_t> block(ctl.begin()+offset+16,ctl.begin()+offset+size);
    check(instrument<word(ctl,offset)&&instrument<128,"invalid US sound instrument");
    unsigned inst=word(block,4+instrument*4),sample=word(block,inst+16),tuneBits=word(block,inst+20);
    float tuning;std::memcpy(&tuning,&tuneBits,4);
    check(std::isfinite(tuning)&&tuning>=0.125f&&tuning<=4.f,"invalid sound tuning");
    check(word(block,sample)==0,"unsupported US sample encoding");
    unsigned sampleOffset=word(block,sample+4),loop=word(block,sample+8),bookOffset=word(block,sample+12),bytes=word(block,sample+16);
    check(word(block,bookOffset)==2,"unsupported VADPCM order");
    unsigned predictors=word(block,bookOffset+4);
    check(predictors>0&&predictors<=8,"invalid VADPCM predictor count");
    std::vector<std::int16_t> book;
    for(unsigned i=0;i<predictors*16;++i)book.push_back(signedHalf(half(block,bookOffset+8+i*2)));
    unsigned frames=word(block,loop+4);
    std::uint8_t header[4];assets.read("sound/sound_data.tbl.be",0,header,4);
    check(header[0]==0&&header[1]==2&&bank<unsigned(header[2])*256+header[3],"invalid US sample-bank index");
    std::vector<std::uint8_t> table(8);assets.read("sound/sound_data.tbl.be",4+bank*8,table.data(),8);
    unsigned base=word(table,0),limit=word(table,4);
    check(bytes>0&&bytes<=36864&&sampleOffset<=limit&&bytes<=limit-sampleOffset,"sample exceeds bounded bank");
    std::vector<std::uint8_t> encoded(bytes);
    assets.read("sound/sound_data.tbl.be",base+sampleOffset,encoded.data(),bytes);
    // US banks may have one alignment byte and an inclusive end one sample
    // past the decoded payload (also documented by disassemble_sound.py).
    check(bytes%9==0||bytes%9==1,"invalid VADPCM sample padding");
    encoded.resize(bytes/9*9);
    RomSound sound{decodeVadpcm(encoded,book,predictors),unsigned(32000*tuning)};
    check(frames>0&&frames<=sound.pcm.size()+1&&sound.rate>=4000&&sound.rate<=128000,"invalid sample loop/sample rate");
    sound.pcm.resize(std::min<std::size_t>(frames,sound.pcm.size()));
    return sound;
}

SoundEffects::SoundEffects(const std::filesystem::path& cache,bool silent) {
    sounds_[0]=loadRomSound(cache,8,2); // Original Mario "yah".
    sounds_[1]=loadRomSound(cache,0,4); // Original soft landing.
    sounds_[2]=loadRomSound(cache,8,10); // Original Mario attacked voice.
    std::cout<<"NativeRomSoundEffects=3 Decoder=VADPCM/order2 BackgroundMusic=not-yet\n";
    if(silent)return;
    if(SDL_InitSubSystem(SDL_INIT_AUDIO))throw std::runtime_error(SDL_GetError());
    SDL_AudioSpec wanted{};wanted.freq=44100;wanted.format=AUDIO_S16SYS;wanted.channels=2;
    wanted.samples=512;wanted.callback=mix;wanted.userdata=this;
    device_=SDL_OpenAudioDevice(nullptr,0,&wanted,nullptr,0);
    check(device_!=0,SDL_GetError());SDL_PauseAudioDevice(device_,0);
    std::cout<<"NativeSoundOutput=44100/stereo/S16 Voices=4\n";
}
SoundEffects::~SoundEffects(){if(device_)SDL_CloseAudioDevice(device_);}
void SoundEffects::play(SoundEffect effect) {
    if(!device_)return;
    SDL_LockAudioDevice(device_);
    auto voice=std::find_if(voices_.begin(),voices_.end(),[](const Voice& v){return !v.sound;});
    if(voice==voices_.end())voice=voices_.begin();
    *voice={&sounds_[unsigned(effect)],0,0};
    SDL_UnlockAudioDevice(device_);
}
void SoundEffects::pause(bool paused){if(device_)SDL_PauseAudioDevice(device_,paused?1:0);}
void SoundEffects::mix(void* opaque,Uint8* stream,int bytes) {
    auto& self=*static_cast<SoundEffects*>(opaque);auto* samples=reinterpret_cast<std::int16_t*>(stream);
    for(int frame=0;frame<bytes/4;++frame) {
        int mixed=0;
        for(auto& voice:self.voices_) {
            if(!voice.sound)continue;
            mixed+=voice.sound->pcm[voice.position]/2;
            voice.fraction+=voice.sound->rate;voice.position+=voice.fraction/44100;voice.fraction%=44100;
            if(voice.position>=voice.sound->pcm.size())voice.sound=nullptr;
        }
        samples[frame*2]=samples[frame*2+1]=std::int16_t(std::clamp(mixed,-32768,32767));
    }
}
}
