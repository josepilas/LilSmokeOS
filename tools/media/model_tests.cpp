#include "MediaPaths.h"
#include "Captions.h"
#include <cassert>
#include <set>
#include <iostream>
using namespace lilsmoke::media;
int main(int argc,char **argv) {
    assert(argc==2);const std::string root=argv[1];
    assert(kind("UPPER.WEBP")==Kind::Photo&&kind("movie.MP4")==Kind::Video);
    assert(kind("sound.OPUS")==Kind::Audio&&kind("save.bin")==Kind::Unknown);
    auto local=collection(root+"/Local.m3u");assert(local.size()==3);
    auto pls=collection(root+"/Local.PLS");assert(pls.size()==2);
    assert(local[0]==root+"/One.wav"&&local[1]==root+"/Two.mp3");
    Playlist p;p.files={"a","b","c","d"};p.shuffle=true;p.random.seed(7);
    std::set<size_t> played={p.index};while(p.advance(false,true))assert(played.insert(p.index).second);
    assert(played.size()==4);p.repeat=1;assert(p.advance(false,true));
    p.repeat=2;size_t same=p.index;assert(p.advance(false,true)&&p.index==same);
    p.repeat=0;p.shuffle=false;assert(p.advance(false));
    p.shuffle=true;assert(p.advance(false,true));assert(p.advance(true));
    Playlist linear;linear.files={"a","b"};assert(linear.advance(false,true));assert(!linear.advance(false,true));
    auto captions=loadCaptions(root+"/Clip.srt");assert(captions.size()==2);
    assert(captions[0].begin==0&&captions[0].end==2&&captions[1].begin==3);
    assert(captionTime("01:02:03,500")==3723.5&&captionTime("00:02.250")==2.25);
    std::cout<<"PASS media classification, local playlists, shuffle without replacement, repeat, previous history and captions\n";
}
