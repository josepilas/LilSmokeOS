#include "Decoder.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <chrono>
extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/display.h>
}
namespace lilsmoke::media {
namespace {
using Timer=std::chrono::steady_clock;
uint64_t micros(Timer::time_point start){return std::chrono::duration_cast<std::chrono::microseconds>(Timer::now()-start).count();}
}
void Decoder::fail(int rc,const char *stage) {
    char text[128];av_strerror(rc,text,sizeof(text));error=std::string(stage)+": "+text;
}
bool Decoder::setup(int index,AVCodecContext **out) {
    AVStream *s=format_->streams[index];const AVCodec *codec=avcodec_find_decoder(s->codecpar->codec_id);
    if(!codec){error="Unsupported codec: "+std::string(avcodec_get_name(s->codecpar->codec_id));return false;}
    *out=avcodec_alloc_context3(codec);if(!*out){error="Not enough decoder memory";return false;}
    int rc=avcodec_parameters_to_context(*out,s->codecpar);if(rc<0){fail(rc,"Codec parameters");return false;}
    (*out)->thread_count=1;(*out)->max_pixels=photo?12000000:2073600;
    (*out)->pkt_timebase=s->time_base;
    if(photo&&codec->max_lowres>0) {
        while((*out)->lowres<codec->max_lowres&&((s->codecpar->width>>(*out)->lowres)>2048||(s->codecpar->height>>(*out)->lowres)>1536))++(*out)->lowres;
    }
    rc=avcodec_open2(*out,codec,nullptr);if(rc<0){fail(rc,"Open codec");return false;}return true;
}
bool Decoder::open(const std::string &file,Kind type,int maxW,int maxH,bool externalVideo) {
    close();error.clear();photo=type==Kind::Photo;maxWidth_=maxW;maxHeight_=maxH;
    if(!localFile(file)){error="File unavailable";return false;}
    format_=avformat_alloc_context();if(!format_){error="Not enough memory";return false;}
    format_->probesize=2*1024*1024;format_->max_analyze_duration=AV_TIME_BASE;
    AVDictionary *options=nullptr;av_dict_set(&options,"protocol_whitelist","file",0);
    int rc=avformat_open_input(&format_,file.c_str(),nullptr,&options);av_dict_free(&options);
    if(rc<0){fail(rc,"Open media");return false;}
    rc=avformat_find_stream_info(format_,nullptr);if(rc<0){fail(rc,"Read media");return false;}
    if(format_->nb_streams>64){error="Too many media streams";return false;}
    for(unsigned i=0;i<format_->nb_streams;++i) {
        auto *s=format_->streams[i];
        if(s->codecpar->codec_type==AVMEDIA_TYPE_AUDIO)audioIndices_.push_back(int(i));
    }
    if(!photo&&!audioIndices_.empty()) {ai_=audioIndices_[0];audioTracks=unsigned(audioIndices_.size());if(!setup(ai_,&audio_))return false;hasAudio=true;}
    if(type!=Kind::Audio)vi_=av_find_best_stream(format_,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0);
    if(type==Kind::Audio)for(unsigned i=0;i<format_->nb_streams;++i)
        if(format_->streams[i]->disposition&AV_DISPOSITION_ATTACHED_PIC) {vi_=int(i);hasArt=true;break;}
    if(vi_>=0) {
        auto *p=format_->streams[vi_]->codecpar;sourceWidth=p->width;sourceHeight=p->height;
        int64_t pixels=int64_t(sourceWidth)*sourceHeight;
        if(hasArt&&(sourceWidth<=0||sourceHeight<=0||pixels>2073600)){vi_=-1;hasArt=false;sourceWidth=sourceHeight=0;}
        else {
            if(sourceWidth<=0||sourceHeight<=0||pixels>(photo?12000000:2073600)){error="Image dimensions exceed the memory limit";return false;}
            if(externalVideo&&!photo&&!hasArt)hasVideo=true;
            else if(!setup(vi_,&video_)) {
                if(!hasArt)return false;
                avcodec_free_context(&video_);vi_=-1;hasArt=false;error.clear();
            } else hasVideo=!hasArt;
        }
    }
    if(!hasAudio&&!hasVideo){error="No supported audio or picture stream";return false;}
    if(format_->duration!=AV_NOPTS_VALUE)duration=std::max(0.0,format_->duration/double(AV_TIME_BASE));
    auto metadata=[this](const char *key){auto *item=av_dict_get(format_->metadata,key,nullptr,0);return item?std::string(item->value).substr(0,512):std::string();};
    title=metadata("title");artist=metadata("artist");album=metadata("album");
    if(title.empty())title=std::filesystem::path(file).filename().string();
    codec=hasVideo?avcodec_get_name(format_->streams[vi_]->codecpar->codec_id):avcodec_get_name(audio_->codec_id);
    frame_=av_frame_alloc();packet_=av_packet_alloc();if(!frame_||!packet_){error="Not enough frame memory";return false;}
    return true;
}
void Decoder::close() {
    clearPending();
    avcodec_free_context(&audio_);avcodec_free_context(&video_);avformat_close_input(&format_);
    av_frame_free(&frame_);av_packet_free(&packet_);swr_free(&resample_);sws_freeContext(scale_);scale_=nullptr;
    av_channel_layout_uninit(&lastLayout_);lastRate_=0;lastFormat_=-1;
    ai_=vi_=-1;audioTrack=audioTracks=0;audioIndices_.clear();video.clear();audio.clear();audioHead=0;
    eof=flushed_=inputEof_=hasAudio=hasVideo=photo=hasArt=false;duration=position=fallbackPts_=seekTarget_=0;
    sourceWidth=sourceHeight=0;title.clear();artist.clear();album.clear();codec.clear();videoClock_=-1;discardedVideoFrames=0;
    profile={};
}
void Decoder::setVideoClock(double seconds) {
    videoClock_=std::isfinite(seconds)&&seconds>=0?seconds:-1;
}
void Decoder::consumeAudio(size_t frames) {
    audioHead+=std::min(frames,audioFrames())*2;
    if(audioHead==audio.size()){audio.clear();audioHead=0;}
    else if(audioHead>32768){audio.erase(audio.begin(),audio.begin()+audioHead);audioHead=0;}
}
bool Decoder::receive(AVCodecContext *ctx,bool sound) {
    for(;;) {
        int rc=avcodec_receive_frame(ctx,frame_);if(rc==AVERROR(EAGAIN)||rc==AVERROR_EOF)return true;
        if(rc<0){fail(rc,"Decode frame");return false;}
        if(sound) {
            if(frame_->nb_samples<=0||frame_->nb_samples>32768||frame_->sample_rate<8000||frame_->sample_rate>192000||frame_->ch_layout.nb_channels<1||frame_->ch_layout.nb_channels>8){error="Audio frame exceeds playback limits";av_frame_unref(frame_);return false;}
            if(!resample_||lastRate_!=frame_->sample_rate||lastFormat_!=frame_->format||av_channel_layout_compare(&lastLayout_,&frame_->ch_layout)) {
                swr_free(&resample_);av_channel_layout_uninit(&lastLayout_);
                AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
                rc=swr_alloc_set_opts2(&resample_,&stereo,AV_SAMPLE_FMT_S16,44100,&frame_->ch_layout,AVSampleFormat(frame_->format),frame_->sample_rate,0,nullptr);
                if(rc>=0)rc=swr_init(resample_);if(rc<0){fail(rc,"Audio conversion");av_frame_unref(frame_);return false;}
                lastRate_=frame_->sample_rate;lastFormat_=frame_->format;av_channel_layout_copy(&lastLayout_,&frame_->ch_layout);
            }
            const int count=av_rescale_rnd(swr_get_delay(resample_,frame_->sample_rate)+frame_->nb_samples,44100,frame_->sample_rate,AV_ROUND_UP);
            if(count<0||count>65536||audioFrames()+size_t(count)>65536){error="Audio queue memory limit";av_frame_unref(frame_);return false;}
            std::vector<int16_t> samples(size_t(count)*2);uint8_t *dst=reinterpret_cast<uint8_t*>(samples.data());
            rc=swr_convert(resample_,&dst,count,const_cast<const uint8_t**>(frame_->extended_data),frame_->nb_samples);
            if(rc<0){fail(rc,"Resample audio");av_frame_unref(frame_);return false;}
            size_t discard=0;
            if(seekTarget_>0&&frame_->best_effort_timestamp!=AV_NOPTS_VALUE) {
                double pts=frame_->best_effort_timestamp*av_q2d(format_->streams[ai_]->time_base);
                if(format_->start_time!=AV_NOPTS_VALUE)pts-=format_->start_time/double(AV_TIME_BASE);
                discard=size_t(std::clamp((seekTarget_-pts)*44100,0.0,double(rc)));
            }
            audio.insert(audio.end(),samples.begin()+discard*2,samples.begin()+rc*2);
        } else {
            ++profile.received;
            const int w=frame_->width,h=frame_->height;
            if(w<=0||h<=0||int64_t(w)*h>(photo?12000000:2073600)){error="Decoded image exceeds memory limit";av_frame_unref(frame_);return false;}
            const AVStream *stream=format_->streams[vi_];
            double pts=frame_->best_effort_timestamp==AV_NOPTS_VALUE?fallbackPts_:frame_->best_effort_timestamp*av_q2d(stream->time_base);
            if(frame_->best_effort_timestamp!=AV_NOPTS_VALUE&&format_->start_time!=AV_NOPTS_VALUE)pts-=format_->start_time/double(AV_TIME_BASE);
            pts=std::max(0.0,pts);double rate=av_q2d(stream->avg_frame_rate);
            fallbackPts_=pts+(rate>0?1/rate:1/25.0);
            // Decode reference frames normally, but avoid allocation/scaling
            // for pictures already too late for the audio playback clock.
            if(!photo&&!hasArt&&(pts+.001<seekTarget_||(videoClock_>=0&&pts<videoClock_-.080))){
                ++discardedVideoFrames;av_frame_unref(frame_);continue;
            }
            double aspect=double(w)/h;
            if(!photo&&frame_->sample_aspect_ratio.num>0)aspect*=av_q2d(frame_->sample_aspect_ratio);
            int ow=std::min(maxWidth_,w),oh=std::max(1,int(ow/aspect));
            if(oh>maxHeight_){oh=maxHeight_;ow=std::max(1,int(oh*aspect));}
            const auto conversionStart=Timer::now();
            const bool argb=!photo&&!hasArt;
            VideoFrame output;output.width=ow;output.height=oh;
            if(argb)output.argb.resize(size_t(ow)*oh);else output.pixels.resize(size_t(ow)*oh);
            const AVFrameSideData *orientation=av_frame_get_side_data(frame_,AV_FRAME_DATA_DISPLAYMATRIX);
            if(orientation&&orientation->size>=9*sizeof(int32_t)) {
                double angle=-av_display_rotation_get(reinterpret_cast<const int32_t*>(orientation->data));
                if(std::isfinite(angle))output.rotation=(int(std::lround(angle/90))*90%360+360)%360;
            }
            scale_=sws_getCachedContext(scale_,w,h,AVPixelFormat(frame_->format),ow,oh,argb?AV_PIX_FMT_BGRA:AV_PIX_FMT_RGB565LE,SWS_FAST_BILINEAR,nullptr,nullptr,nullptr);
            if(!scale_){error="Picture conversion unavailable";av_frame_unref(frame_);return false;}
            uint8_t *dst[4]={argb?reinterpret_cast<uint8_t*>(output.argb.data()):reinterpret_cast<uint8_t*>(output.pixels.data()),nullptr,nullptr,nullptr};int strides[4]={ow*(argb?4:2),0,0,0};
            sws_scale(scale_,frame_->data,frame_->linesize,0,h,dst,strides);
            profile.convertUs+=micros(conversionStart);++profile.converted;profile.pixels+=size_t(ow)*oh;
            output.pts=pts;
            if(output.pts+.001>=seekTarget_)video.push_back(std::move(output));
            if(video.size()>8){error="Video queue memory limit";av_frame_unref(frame_);return false;}
        }
        av_frame_unref(frame_);
    }
}
bool Decoder::decode(AVCodecContext *ctx,const AVPacket *p,bool sound) {
    const auto start=Timer::now();
    const auto conversionBefore=profile.convertUs;
    int rc=avcodec_send_packet(ctx,p);
    if(rc==AVERROR(EAGAIN)){if(!receive(ctx,sound))return false;rc=avcodec_send_packet(ctx,p);}
    if(rc<0&&rc!=AVERROR_EOF){fail(rc,"Submit packet");return false;}
    const bool ok=receive(ctx,sound);
    profile.decodeUs+=micros(start)-(profile.convertUs-conversionBefore);return ok;
}
void Decoder::clearPending() {
    for(auto *p:pendingVideo_)av_packet_free(&p);
    pendingVideo_.clear();pendingVideoBytes_=0;
}
bool Decoder::queueVideo(const AVPacket *packet) {
    constexpr size_t limit=4*1024*1024;
    if(packet->size<0||pendingVideo_.size()>=128||size_t(packet->size)>limit-pendingVideoBytes_) {
        error="Media interleaving exceeds the bounded video queue";return false;
    }
    AVPacket *copy=av_packet_clone(packet);
    if(!copy){error="Not enough packet memory";return false;}
    pendingVideo_.push_back(copy);pendingVideoBytes_+=size_t(copy->size);return true;
}
bool Decoder::drainVideo() {
    while(!pendingVideo_.empty()&&video.size()<3) {
        AVPacket *p=pendingVideo_.front();pendingVideo_.pop_front();
        pendingVideoBytes_-=size_t(p->size);
        bool ok=decode(video_,p,false);av_packet_free(&p);if(!ok)return false;
    }
    return true;
}
bool Decoder::pump() {
    if(!format_||eof||!error.empty())return false;
    if(!drainVideo())return false;
    auto supplied=[this] {return (!video_||video.size()>=3||hasArt)&&(!audio_||audioFrames()>=8820);};
    if(supplied())return true;
    for(unsigned n=0;n<6;++n) {
        if(inputEof_) {
            if(!pendingVideo_.empty()||video.size()>=3)return true;
            if(!flushed_){flushed_=true;if(audio_&&!decode(audio_,nullptr,true))return false;if(video_&&!decode(video_,nullptr,false))return false;}
            eof=true;return true;
        }
        const auto readStart=Timer::now();int rc=av_read_frame(format_,packet_);
        profile.demuxUs+=micros(readStart);
        if(rc==AVERROR_EOF){inputEof_=true;continue;}
        if(rc<0){fail(rc,"Read packet");return false;}
        if(packet_->size>32*1024*1024){av_packet_unref(packet_);error="Media packet exceeds memory limit";return false;}
        bool ok=true;if(packet_->stream_index==ai_)ok=decode(audio_,packet_,true);
        else if(packet_->stream_index==vi_&&video_) {
            if(video.size()>=3||!pendingVideo_.empty())ok=queueVideo(packet_);
            else ok=decode(video_,packet_,false);
        }
        av_packet_unref(packet_);if(!ok)return false;
        // A full video queue cannot prevent reading the first audio packet.
        if(supplied()||audioFrames()>=44100)return true;
    }
    return true;
}
bool Decoder::seek(double seconds) {
    if(!format_||photo)return false;
    seconds=std::max(0.0,duration>0?std::min(seconds,duration):seconds);
    int64_t start=format_->start_time==AV_NOPTS_VALUE?0:format_->start_time;
    int rc=avformat_seek_file(format_,-1,INT64_MIN,int64_t(seconds*AV_TIME_BASE)+start,INT64_MAX,AVSEEK_FLAG_BACKWARD);
    if(rc<0){fail(rc,"Seek");return false;}
    if(audio_)avcodec_flush_buffers(audio_);if(video_)avcodec_flush_buffers(video_);
    swr_free(&resample_);clearPending();video.clear();audio.clear();audioHead=0;eof=flushed_=inputEof_=false;
    videoClock_=-1;fallbackPts_=position=seekTarget_=seconds;return true;
}
bool Decoder::changeAudio() {
    if(audioTracks<2)return false;
    audioTrack=(audioTrack+1)%audioTracks;avcodec_free_context(&audio_);ai_=audioIndices_[audioTrack];
    if(!setup(ai_,&audio_))return false;return seek(position);
}
}
