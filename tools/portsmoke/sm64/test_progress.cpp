#include "course_objects.hpp"
#include "sound_effects.hpp"
#include "extraction.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace portsmoke::sm64;
unsigned checks=0;
void expect(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
std::vector<std::uint8_t> flat() {
    std::vector<std::uint8_t> bytes;
    auto half=[&](int n){bytes.push_back((unsigned(n)>>8)&255);bytes.push_back(unsigned(n)&255);};
    half(0x40);half(4);
    for(auto p:std::array<Position,4>{{{-8191,0,-8191},{-8191,0,8191},{8191,0,-8191},{8191,0,8191}}}) {
        half(int(p.x));half(int(p.y));half(int(p.z));
    }
    half(0);half(2);for(int i:{0,1,2,2,1,3})half(i);half(0x41);
    return bytes;
}
int main(int argc,char** argv) {
    try {
        if(argc!=3)return 64;
        std::filesystem::path original=argv[1],cache=argv[2];
        ExtractionStatus status;extractGameData(original,cache,status);expect(status.error.empty(),status.error.c_str());
        auto world=loadBobCollision(cache/"bob-us-v1.pdat");
        auto course=loadBobObjects(cache,world);auto graphics=loadObjectGraphics(cache);
        expect(course.objects.size()==49,"wrong ROM-derived object count");
        unsigned red=0,enemies=0;
        for(const auto& object:course.objects){red+=object.kind==ObjectKind::RedCoin;enemies+=object.kind==ObjectKind::Goomba;}
        expect(red==8&&enemies>=2,"missing original red coins / Goombas");
        expect(!graphics.star.vertices.empty()&&!graphics.goombaHead.vertices.empty()&&!graphics.goombaBody.vertices.empty(),"missing ROM meshes");
        expect(graphics.coins[0].textures[0].image.width==32,"coin ROM sprite missing");
        PlayerState player;course.invulnerable=1000;
        for(auto& object:course.objects)if(object.kind==ObjectKind::RedCoin) {
            player.position={object.position.x,object.position.y-80,object.position.z};
            expect(stepObjects(course,player,world).coin,"red coin not collectible");
            expect(!object.active,"collected coin still visible");
        }
        expect(course.redCoins==8&&course.coins>=16,"red-coin goal accounting");
        auto star=std::find_if(course.objects.begin(),course.objects.end(),[](const auto& o){return o.starBit==(1U<<3);});
        expect(star!=course.objects.end()&&star->active,"red-coin star not spawned");
        player.position={star->position.x,star->position.y-80,star->position.z};
        expect(stepObjects(course,player,world).star,"spawned star not collectible");
        expect(course.stars&(1U<<3),"star progress not set");
        auto save=cache.parent_path()/"saves/progress.dat";
        saveCourse(course,save);auto restored=loadBobObjects(cache,world);loadCourseSave(restored,save);
        expect(restored.stars==course.stars,"atomic star save/reload failed");
        for(auto& o:restored.objects)if(o.starBit==(1U<<3))expect(!o.active,"saved star reappeared");
        CollisionWorld ground(flat());CourseState combat;
        combat.objects.push_back({ObjectKind::Goomba,{0,0,0},{0,0,0}});player.position={0,0,0};
        expect(stepObjects(combat,player,ground).hurt&&combat.health==6,"Goomba did not damage Mario");
        expect(!stepObjects(combat,player,ground).hurt&&combat.health==6,"invulnerability missing");
        player.position={0,100,0};player.velocityY=-4;
        expect(stepObjects(combat,player,ground).stomp,"descending stomp failed");
        expect(combat.objects[0].kind==ObjectKind::Coin&&player.velocityY==42,"stomp did not bounce/drop coin");
        std::vector<std::uint8_t> encoded(9,0x17);encoded[0]=0;
        auto pcm=decodeVadpcm(encoded,std::vector<std::int16_t>(16,0),1);
        expect(pcm.size()==16&&pcm[0]==1&&pcm[1]==7,"VADPCM residual decode");
        encoded[1]=0x8f;pcm=decodeVadpcm(encoded,std::vector<std::int16_t>(16,0),1);
        expect(pcm[0]==-8&&pcm[1]==-1,"VADPCM negative residual decode");
        bool rejected=false;try{decodeVadpcm({255,0,0,0,0,0,0,0,0},std::vector<std::int16_t>(16),1);}catch(...){rejected=true;}
        expect(rejected,"malformed VADPCM predictor accepted");
        for(auto pair:std::array<std::array<unsigned,2>,3>{{{8,2},{0,4},{8,10}}}) {
            auto sound=loadRomSound(cache,pair[0],pair[1]);
            expect(sound.pcm.size()>1000&&sound.pcm.size()<=65536,"invalid original sound length");
            expect(std::any_of(sound.pcm.begin(),sound.pcm.end(),[](auto x){return x!=0;}),"silent ROM effect");
            std::vector<std::uint8_t> raw;for(auto sample:sound.pcm){raw.push_back(unsigned(sample)&255);raw.push_back((unsigned(sample)>>8)&255);}
            std::cout<<"Sound bank="<<pair[0]<<" instrument="<<pair[1]<<" frames="<<sound.pcm.size()<<" rate="<<sound.rate<<" pcm_sha1="<<sha1(raw)<<'\n';
        }
        expect(SDL_Init(SDL_INIT_AUDIO)==0,SDL_GetError());
        {SoundEffects audio(cache);expect(audio.available(),"SDL audio not opened");audio.play(SoundEffect::Jump);SDL_Delay(50);audio.pause(true);audio.pause(false);}
        SDL_Quit();
        std::cout<<"CourseProgressChecks="<<checks<<" PASS ROM meshes/coins/red-star/combat/save/VADPCM/SDL-audio\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
