#define _GNU_SOURCE
#include "od-sdl.h"
#include "m8_pocketsnes_audio.h"
#include <SDL.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define QUEUE_MAX 65536U
#define CONVERSION_MAX 262144U
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int in_callback;
static struct {
	SDL_AudioSpec spec;
	SDL_AudioCVT cvt;
	int (*convert)(SDL_AudioCVT *);
	pthread_t thread;
	int initialized,open,stop,paused,failed;
	unsigned char *buffer,queue[QUEUE_MAX];
	unsigned head,used,capacity,frame_bytes;
	unsigned pending_original,pending_frames,pending_at;
} device;
static int audio_error(const char *reason){od_error(reason);return -1;}
int od_audio_initialized(void)
{
	pthread_mutex_lock(&mutex);
	int initialized=device.initialized;
	pthread_mutex_unlock(&mutex);
	return initialized;
}
void od_audio_mark_initialized(void)
{
	pthread_mutex_lock(&mutex);device.initialized=1;pthread_mutex_unlock(&mutex);
}
static void silence(unsigned char *p,unsigned bytes,SDL_AudioFormat format)
{
	memset(p,format==AUDIO_U8?0x80:0,bytes);
	if(format==AUDIO_U16LSB||format==AUDIO_U16MSB) {
		unsigned index=format==AUDIO_U16LSB?1:0;
		for(unsigned i=index;i<bytes;i+=2)p[i]=0x80;
	}
}
static void advance(struct timespec *time,unsigned frames)
{
	uint64_t ns=(uint64_t)frames*1000000000ULL/44100U;
	time->tv_nsec+=(long)(ns%1000000000ULL);
	time->tv_sec+=(time_t)(ns/1000000000ULL);
	if(time->tv_nsec>=1000000000L){time->tv_nsec-=1000000000L;++time->tv_sec;}
}
static void *audio_thread(void *unused)
{
	struct timespec deadline,now,delay={0,2000000};
	int16_t zeros[M8_PCM_CHUNK_FRAMES*2]={0};
	(void)unused;
	clock_gettime(CLOCK_MONOTONIC,&deadline);
	for(;;) {
		unsigned frames=0;
		const int16_t *pcm=zeros;
		pthread_mutex_lock(&mutex);
		if(device.stop){pthread_mutex_unlock(&mutex);break;}
		if(device.paused||device.failed) {
			pthread_mutex_unlock(&mutex);
			(void)m8_pcm_active();nanosleep(&delay,NULL);
			clock_gettime(CLOCK_MONOTONIC,&deadline);continue;
		}
		if(device.pending_at==device.pending_frames) {
			unsigned bytes=device.spec.size,available=bytes;
			device.pending_at=device.pending_frames=device.pending_original=0;
			if(device.spec.callback) {
				silence(device.buffer,bytes,device.spec.format);
				in_callback=1;
				device.spec.callback(device.spec.userdata,device.buffer,(int)bytes);
				in_callback=0;
			} else {
				if(available>device.used)available=device.used;
				available-=available%device.frame_bytes;
				for(unsigned i=0;i<available;++i)
					device.buffer[i]=device.queue[(device.head+i)%device.capacity];
				device.head=(device.head+available)%device.capacity;device.used-=available;
				bytes=available;
			}
			if(bytes) {
				device.cvt.buf=device.buffer;device.cvt.len=(int)bytes;
				if(device.convert(&device.cvt)<0||device.cvt.len_cvt<0||
				   (unsigned)device.cvt.len_cvt>CONVERSION_MAX||device.cvt.len_cvt%4) {
					device.failed=1;
					pthread_mutex_unlock(&mutex);
					od_error("SDL AudioCVT conversion failed; native audio stopped");continue;
				}
				device.pending_frames=(unsigned)device.cvt.len_cvt/4;
				device.pending_original=bytes;
			}
		}
		if(device.pending_at<device.pending_frames) {
			frames=device.pending_frames-device.pending_at;
			if(frames>M8_PCM_CHUNK_FRAMES)frames=M8_PCM_CHUNK_FRAMES;
			pcm=(const int16_t *)device.buffer+device.pending_at*2;
		} else frames=M8_PCM_CHUNK_FRAMES;
		/* PCM send copies a bounded packet; no vendor syscall on this thread. */
		int active=m8_pcm_active();
		int accepted=active?m8_pcm_send(pcm,frames):-1;
		if(!active||accepted<0)device.failed=1;
		if(device.pending_at<device.pending_frames)device.pending_at+=frames;
		pthread_mutex_unlock(&mutex);
		if(!active)od_error("Native PCM worker failed; no dummy or ALSA fallback");
		advance(&deadline,frames);
		clock_gettime(CLOCK_MONOTONIC,&now);
		/* Do not burst stale audio after a long callback or descheduled thread. */
		if(now.tv_sec>deadline.tv_sec||(now.tv_sec==deadline.tv_sec&&now.tv_nsec>deadline.tv_nsec))
			deadline=now;
		while(clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&deadline,NULL)==EINTR){}
	}
	(void)m8_pcm_close();return NULL;
}
static int open_audio(const SDL_AudioSpec *wanted,SDL_AudioSpec *obtained)
{
	SDL_AudioCVT cvt;
	SDL_AudioSpec spec;
	int (*build)(SDL_AudioCVT *,SDL_AudioFormat,Uint8,int,SDL_AudioFormat,Uint8,int)=
		od_real("SDL_BuildAudioCVT",2);
	int (*convert)(SDL_AudioCVT *)=od_real("SDL_ConvertAudio",2);
	if(!wanted||wanted->freq<8000||wanted->freq>96000||wanted->channels<1||wanted->channels>2||
	   wanted->samples>4096)return audio_error("Unsupported bounded SDL audio specification");
	spec=*wanted;
	if(!spec.samples)spec.samples=512;
	switch(spec.format) {
		case AUDIO_U8:case AUDIO_S8:case AUDIO_U16LSB:case AUDIO_U16MSB:
		case AUDIO_S16LSB:case AUDIO_S16MSB:case AUDIO_S32LSB:case AUDIO_S32MSB:
		case AUDIO_F32LSB:case AUDIO_F32MSB:break;
		default:return audio_error("Unsupported SDL audio format");
	}
	unsigned frame_bytes=(unsigned)SDL_AUDIO_BITSIZE(spec.format)/8*spec.channels;
	spec.size=spec.samples*frame_bytes;spec.silence=spec.format==AUDIO_U8?0x80:0;
	if(!build||!convert||build(&cvt,spec.format,spec.channels,spec.freq,AUDIO_S16LSB,2,44100)<0||
	   cvt.len_mult<1||(unsigned)cvt.len_mult>CONVERSION_MAX/spec.size)
		return audio_error("Native audio requires a bounded working SDL2 AudioCVT");
	pthread_mutex_lock(&mutex);
	if(device.open){pthread_mutex_unlock(&mutex);return audio_error("Only one native SDL audio device is supported");}
	device.buffer=malloc(spec.size*(unsigned)cvt.len_mult);
	if(!device.buffer){pthread_mutex_unlock(&mutex);return audio_error("Audio conversion allocation failed");}
	setenv("LILSMOKE_M8_AUDIO","1",1);
	if(m8_pcm_init(44100,2,16)!=1) {
		free(device.buffer);device.buffer=NULL;pthread_mutex_unlock(&mutex);
		return audio_error("Native isolated PCM initialization failed");
	}
	device.spec=spec;device.cvt=cvt;device.convert=convert;device.frame_bytes=frame_bytes;
	device.capacity=(unsigned)spec.freq*frame_bytes/4;
	if(device.capacity>QUEUE_MAX)device.capacity=QUEUE_MAX;
	device.capacity-=device.capacity%frame_bytes;
	device.head=device.used=device.pending_at=device.pending_frames=device.pending_original=0;
	device.open=device.initialized=device.paused=1;device.failed=device.stop=0;
	int result=pthread_create(&device.thread,NULL,audio_thread,NULL);
	if(result) {
		device.open=0;free(device.buffer);device.buffer=NULL;pthread_mutex_unlock(&mutex);
		m8_pcm_close();return audio_error("Native audio pacing thread creation failed");
	}
	if(obtained)*obtained=spec;
	pthread_mutex_unlock(&mutex);
	fprintf(stderr,"OD_SDL_AUDIO api=%d callback=%d source=%d/%u/%u sink=44100/2/S16LE cvt=SDL2 queue=%u\n",
		od_api(),spec.callback!=NULL,spec.freq,spec.channels,SDL_AUDIO_BITSIZE(spec.format),device.capacity);
	return 0;
}
void od_audio_shutdown(void)
{
	if(in_callback){od_error("CloseAudio is not supported inside its callback");return;}
	pthread_mutex_lock(&mutex);
	if(!device.open){device.initialized=0;pthread_mutex_unlock(&mutex);return;}
	if(pthread_equal(pthread_self(),device.thread)) {
		device.stop=1;pthread_mutex_unlock(&mutex);od_error("CloseAudio cannot join from its own callback");return;
	}
	device.stop=1;pthread_mutex_unlock(&mutex);pthread_join(device.thread,NULL);
	pthread_mutex_lock(&mutex);
	free(device.buffer);device.buffer=NULL;device.open=device.initialized=0;
	device.used=device.pending_original=device.pending_frames=device.pending_at=0;
	pthread_mutex_unlock(&mutex);
}
int SDL_AudioInit(const char *driver)
{
	int (*real)(const char *)=od_real("SDL_AudioInit",0);
	if(od_enabled()){od_audio_mark_initialized();return 0;}
	return real?real(driver):-1;
}
void SDL_AudioQuit(void)
{
	void (*real)(void)=od_real("SDL_AudioQuit",0);
	if(od_enabled())od_audio_shutdown();else if(real)real();
}
int SDL_OpenAudio(SDL_AudioSpec *wanted,SDL_AudioSpec *obtained)
{
	int (*real)(SDL_AudioSpec *,SDL_AudioSpec *)=od_real("SDL_OpenAudio",0);
	return od_enabled()?open_audio(wanted,obtained):(real?real(wanted,obtained):-1);
}
SDL_AudioDeviceID SDL_OpenAudioDevice(const char *name,int capture,const SDL_AudioSpec *wanted,
                                    SDL_AudioSpec *obtained,int changes)
{
	SDL_AudioDeviceID (*real)(const char *,int,const SDL_AudioSpec *,SDL_AudioSpec *,int)=
		od_real("SDL_OpenAudioDevice",2);
	if(!od_enabled())return real?real(name,capture,wanted,obtained,changes):0;
	if(capture){audio_error("Stock M8 adapter does not support audio capture");return 0;}
	return open_audio(wanted,obtained)?0:1;
}
void SDL_CloseAudio(void)
{
	void (*real)(void)=od_real("SDL_CloseAudio",0);
	if(od_enabled())od_audio_shutdown();else if(real)real();
}
void SDL_CloseAudioDevice(SDL_AudioDeviceID id)
{
	void (*real)(SDL_AudioDeviceID)=od_real("SDL_CloseAudioDevice",2);
	if(od_enabled()){if(id==1)od_audio_shutdown();}else if(real)real(id);
}
void SDL_PauseAudio(int pause)
{
	void (*real)(int)=od_real("SDL_PauseAudio",0);
	if(od_enabled()){pthread_mutex_lock(&mutex);device.paused=pause!=0;pthread_mutex_unlock(&mutex);}
	else if(real)real(pause);
}
void SDL_PauseAudioDevice(SDL_AudioDeviceID id,int pause)
{
	void (*real)(SDL_AudioDeviceID,int)=od_real("SDL_PauseAudioDevice",2);
	if(od_enabled()){if(id==1)SDL_PauseAudio(pause);}else if(real)real(id,pause);
}
SDL_AudioStatus SDL_GetAudioStatus(void)
{
	SDL_AudioStatus (*real)(void)=od_real("SDL_GetAudioStatus",0);
	if(!od_enabled())return real?real():SDL_AUDIO_STOPPED;
	pthread_mutex_lock(&mutex);
	SDL_AudioStatus result=!device.open||device.failed?SDL_AUDIO_STOPPED:
		device.paused?SDL_AUDIO_PAUSED:SDL_AUDIO_PLAYING;
	pthread_mutex_unlock(&mutex);return result;
}
SDL_AudioStatus SDL_GetAudioDeviceStatus(SDL_AudioDeviceID id)
{
	SDL_AudioStatus (*real)(SDL_AudioDeviceID)=od_real("SDL_GetAudioDeviceStatus",2);
	return od_enabled()?(id==1?SDL_GetAudioStatus():SDL_AUDIO_STOPPED):(real?real(id):SDL_AUDIO_STOPPED);
}
void SDL_LockAudio(void)
{
	void (*real)(void)=od_real("SDL_LockAudio",0);
	if(od_enabled())pthread_mutex_lock(&mutex);else if(real)real();
}
void SDL_UnlockAudio(void)
{
	void (*real)(void)=od_real("SDL_UnlockAudio",0);
	if(od_enabled())pthread_mutex_unlock(&mutex);else if(real)real();
}
void SDL_LockAudioDevice(SDL_AudioDeviceID id)
{
	void (*real)(SDL_AudioDeviceID)=od_real("SDL_LockAudioDevice",2);
	if(od_enabled()){if(id==1)SDL_LockAudio();}else if(real)real(id);
}
void SDL_UnlockAudioDevice(SDL_AudioDeviceID id)
{
	void (*real)(SDL_AudioDeviceID)=od_real("SDL_UnlockAudioDevice",2);
	if(od_enabled()){if(id==1)SDL_UnlockAudio();}else if(real)real(id);
}
int SDL_QueueAudio(SDL_AudioDeviceID id,const void *data,Uint32 bytes)
{
	int (*real)(SDL_AudioDeviceID,const void *,Uint32)=od_real("SDL_QueueAudio",2);
	if(!od_enabled())return real?real(id,data,bytes):-1;
	pthread_mutex_lock(&mutex);
	if(id!=1||!device.open||device.failed||device.spec.callback||(!data&&bytes)||bytes%device.frame_bytes||
	   bytes>device.capacity-device.used) {
		pthread_mutex_unlock(&mutex);return audio_error("Invalid audio device, callback queue, frame alignment or bounded queue full");
	}
	for(unsigned i=0;i<bytes;++i)device.queue[(device.head+device.used+i)%device.capacity]=((const unsigned char *)data)[i];
	device.used+=bytes;pthread_mutex_unlock(&mutex);return 0;
}
Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID id)
{
	Uint32 (*real)(SDL_AudioDeviceID)=od_real("SDL_GetQueuedAudioSize",2);
	if(!od_enabled())return real?real(id):0;
	pthread_mutex_lock(&mutex);
	Uint32 result=id==1&&device.open?device.used:0;
	if(id==1&&device.pending_frames>device.pending_at)
		result+=(Uint32)((uint64_t)device.pending_original*(device.pending_frames-device.pending_at)/device.pending_frames);
	pthread_mutex_unlock(&mutex);return result;
}
void SDL_ClearQueuedAudio(SDL_AudioDeviceID id)
{
	void (*real)(SDL_AudioDeviceID)=od_real("SDL_ClearQueuedAudio",2);
	if(!od_enabled()){if(real)real(id);return;}
	pthread_mutex_lock(&mutex);
	if(id==1){device.head=device.used=device.pending_frames=device.pending_at=device.pending_original=0;}
	pthread_mutex_unlock(&mutex);
}
int SDL_GetNumAudioDevices(int capture)
{
	int (*real)(int)=od_real("SDL_GetNumAudioDevices",2);
	return od_enabled()?(capture?0:1):(real?real(capture):0);
}
const char *SDL_GetAudioDeviceName(int index,int capture)
{
	const char *(*real)(int,int)=od_real("SDL_GetAudioDeviceName",2);
	return od_enabled()?(!capture&&!index?"M8 native isolated PCM":NULL):(real?real(index,capture):NULL);
}
const char *SDL_GetCurrentAudioDriver(void)
{
	const char *(*real)(void)=od_real("SDL_GetCurrentAudioDriver",0);
	return od_enabled()?(od_audio_initialized()?"m8-native":NULL):(real?real():NULL);
}
char *SDL_AudioDriverName(char *buffer,int length)
{
	char *(*real)(char *,int)=od_real("SDL_AudioDriverName",1);
	if(!od_enabled())return real?real(buffer,length):NULL;
	if(!buffer||length<1||!od_audio_initialized())return NULL;
	snprintf(buffer,(size_t)length,"m8-native");
	return buffer;
}
