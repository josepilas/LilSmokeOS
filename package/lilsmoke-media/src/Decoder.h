#pragma once
#include "MediaPaths.h"
#include <cstdint>
#include <deque>
#include <string>
#include <vector>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
namespace lilsmoke::media {
struct VideoFrame { std::vector<uint16_t> pixels;std::vector<uint32_t> argb;int width=0,height=0,rotation=0;double pts=0; };
struct DecodeProfile {
    uint64_t demuxUs=0,decodeUs=0,convertUs=0,received=0,converted=0,pixels=0;
};
class Decoder {
public:
    ~Decoder(){close();}
    bool open(const std::string &file,Kind kind,int maxWidth,int maxHeight,bool externalVideo=false);
    void close();
    bool pump();
    bool seek(double seconds);
    void setVideoClock(double seconds);
    std::string error,title,artist,album,codec;
    double duration=0,position=0;
    bool eof=false,hasAudio=false,hasVideo=false,photo=false,hasArt=false;
    int sourceWidth=0,sourceHeight=0;
    unsigned audioTrack=0,audioTracks=0;
    uint64_t discardedVideoFrames=0;
    DecodeProfile profile;
    bool changeAudio();
    std::deque<VideoFrame> video;
    std::vector<int16_t> audio;
    size_t audioHead=0;
    size_t audioFrames()const{return (audio.size()-audioHead)/2;}
    void consumeAudio(size_t n);
private:
    AVFormatContext *format_=nullptr;
    AVCodecContext *audio_=nullptr,*video_=nullptr;
    AVFrame *frame_=nullptr;
    AVPacket *packet_=nullptr;
    SwrContext *resample_=nullptr;
    SwsContext *scale_=nullptr;
    int ai_=-1,vi_=-1,maxWidth_=640,maxHeight_=360;
    bool flushed_=false;
    bool inputEof_=false;
    std::deque<AVPacket*> pendingVideo_;
    size_t pendingVideoBytes_=0;
    bool queueVideo(const AVPacket *packet);
    bool drainVideo();
    void clearPending();
    double fallbackPts_=0;
    double seekTarget_=0;
    double videoClock_=-1;
    int lastRate_=0,lastFormat_=-1;
    AVChannelLayout lastLayout_{};
    std::vector<int> audioIndices_;
    bool setup(int index,AVCodecContext **context);
    bool decode(AVCodecContext *context,const AVPacket *packet,bool sound);
    bool receive(AVCodecContext *context,bool sound);
    void fail(int code,const char *stage);
};
}
