/***************************************************************************
    Libretro Audio Code.
    
    It takes the output from the PCM and YM chips, mixes them and then
    outputs appropriately.
    
    In order to achieve seamless audio, when audio is enabled the framerate
    is adjusted to essentially sync the video to the audio output.
    
    This is based upon code from the Atari800 emulator project.
    Copyright (c) 1998-2008 Atari800 development team
***************************************************************************/

#include <string.h>
#include <stdlib.h>
#include "audio.hpp"
#include "frontend/config.hpp" // fps
#include "engine/audio/osoundint.hpp"
#include <libretro.h>

extern retro_log_printf_t                 log_cb;

#ifdef __PS3__
#define free std::free
#endif

extern retro_audio_sample_batch_t  audio_batch_cb;

#ifdef COMPILE_SOUND_CODE

/* ----------------------------------------------------------------------------
   SDL Sound Implementation & Callback Function
   ----------------------------------------------------------------------------*/

// Note that these variables are accessed by two separate threads.
uint8_t* dsp_buffer;
static int dsp_buffer_bytes;
static int dsp_write_pos;
static int dsp_read_pos;
static int bytes_per_sample; // Number of bytes per sample entry (usually 4 bytes if stereo and 16-bit sound)

Audio::Audio()
{

}

Audio::~Audio()
{

}

void Audio::init()
{
    if (config.sound.enabled)
        start_audio();
}

void Audio::start_audio()
{
    if (!sound_enabled)
    {
        bytes_per_sample = CHANNELS * (BITS / 8);

        // Start Audio
        sound_enabled = true;

        // how many fragments in the dsp buffer
        const int DSP_BUFFER_FRAGS = 5;
        int specified_delay_samps = (FREQ * SND_DELAY) / 1000;
        int dsp_buffer_samps = SAMPLES * DSP_BUFFER_FRAGS + specified_delay_samps;
        dsp_buffer_bytes = CHANNELS * dsp_buffer_samps * (BITS / 8);
        dsp_buffer = new uint8_t[dsp_buffer_bytes];

        // Create Buffer For Mixing
        uint16_t buffer_size = (FREQ / config.fps) * CHANNELS;
        mix_buffer = new uint16_t[buffer_size];

        clear_buffers();
        clear_wav();
    }
}

void Audio::clear_buffers()
{
    dsp_read_pos  = 0;
    int specified_delay_samps = (FREQ * SND_DELAY) / 1000;
    dsp_write_pos = (specified_delay_samps+SAMPLES) * bytes_per_sample;

    for (int i = 0; i < dsp_buffer_bytes; i++)
        dsp_buffer[i] = 0;

    uint16_t buffer_size = (FREQ / config.fps) * CHANNELS;
    for (int i = 0; i < buffer_size; i++)
        mix_buffer[i] = 0;
}

void Audio::stop_audio()
{
    if (sound_enabled)
    {
        sound_enabled = false;

        delete[] dsp_buffer;
        delete[] mix_buffer;
    }
}

void Audio::pause_audio()
{
}

void Audio::resume_audio()
{
    if (sound_enabled)
        clear_buffers();
}

// Called every frame to update the audio
void Audio::tick()
{
   static unsigned SND_RATE      = 44100;
    int bytes_written = 0;
    int newpos;

    if (!sound_enabled)
       return;

    // Update audio streams from PCM & YM Devices
    osoundint.pcm->stream_update();
    osoundint.ym->stream_update();

    // Get the audio buffers we've just output
    int16_t *pcm_buffer = osoundint.pcm->get_buffer();
    int16_t *ym_buffer  = osoundint.ym->get_buffer();
    int16_t *wav_buffer = wavfile.data;

    int samples_written = osoundint.pcm->buffer_size;

    // And mix them into the mix_buffer
    for (int i = 0; i < samples_written; i++)
    {
        int32_t mix_data = wav_buffer[wavfile.pos] + pcm_buffer[i] + ym_buffer[i];

        // Clip mix data
        if (mix_data >= (1 << 15))
            mix_data = (1 << 15);
        else if (mix_data < -(1 << 15))
            mix_data = -(1 << 15);

        mix_buffer[i] = mix_data;

        // Loop wav files
        if (++wavfile.pos >= wavfile.length)
            wavfile.pos = 0;
    }

    // Cast mix_buffer to a byte array, to align it with internal SDL format 
    uint8_t* mbuf8 = (uint8_t*) mix_buffer;

    // produce samples from the sound emulation
    bytes_written = (BITS == 8 ? samples_written : samples_written*2);
    
    // now we copy the data into the buffer and adjust the positions
    newpos = dsp_write_pos + bytes_written;
    if (newpos/dsp_buffer_bytes == dsp_write_pos/dsp_buffer_bytes) 
    {
        // no wrap
        memcpy(dsp_buffer+(dsp_write_pos%dsp_buffer_bytes), mbuf8, bytes_written);
    }
    else 
    {
        // wraps
        int first_part_size = dsp_buffer_bytes - (dsp_write_pos%dsp_buffer_bytes);
        memcpy(dsp_buffer+(dsp_write_pos%dsp_buffer_bytes), mbuf8, first_part_size);
        memcpy(dsp_buffer, mbuf8+first_part_size, bytes_written-first_part_size);
    }
    dsp_write_pos = newpos;

    // Sound callback has not yet been called
    dsp_read_pos += bytes_written;

    while (dsp_read_pos > dsp_buffer_bytes) 
    {
        dsp_write_pos -= dsp_buffer_bytes;
        dsp_read_pos -= dsp_buffer_bytes;
    }

   int audio_frames = SND_RATE / config.fps;
   audio_batch_cb((int16_t*)mix_buffer, audio_frames);
}

// Empty Wav Buffer
static int16_t EMPTY_BUFFER[] = {0, 0, 0, 0};

void Audio::load_wav(const char* filename)
{
    if (!sound_enabled)
       return;

#if 0
   /* TODO/FIXME */
    {
        clear_wav();

        // Load Wav File
        SDL_AudioSpec wave;
    
        uint8_t *data;
        uint32_t length;

        pause_audio();

        if( SDL_LoadWAV(filename, &wave, &data, &length) == NULL)
        {
            wavfile.loaded = 0;
            resume_audio();
            log_cb(RETRO_LOG_ERROR, "Could not load WAV: %s\n", filename);
            return;
        }
        
        // Halve Volume Of Wav File
        uint8_t* data_vol = new uint8_t[length];
        SDL_MixAudio(data_vol, data, length, SDL_MIX_MAXVOLUME / 2);

        // WAV File Needs Conversion To Target Format
        if (wave.format != AUDIO_S16 || wave.channels != 2 || wave.freq != FREQ)
        {
            SDL_AudioCVT cvt;
            SDL_BuildAudioCVT(&cvt, wave.format, wave.channels, wave.freq,
                                    AUDIO_S16,   CHANNELS,      FREQ);

            cvt.buf = (uint8_t*) malloc(length*cvt.len_mult);
            memcpy(cvt.buf, data_vol, length);
            cvt.len = length;
            SDL_ConvertAudio(&cvt);
            SDL_FreeWAV(data);
            delete[] data_vol;

            wavfile.data = (int16_t*) cvt.buf;
            wavfile.length = cvt.len_cvt / 2;
            wavfile.pos = 0;
            wavfile.loaded = 1;
        }
        // No Conversion Needed
        else
        {
            SDL_FreeWAV(data);
            wavfile.data = (int16_t*) data_vol;
            wavfile.length = length / 2;
            wavfile.pos = 0;
            wavfile.loaded = 2;
        }

        resume_audio();
    }
#endif
}

void Audio::clear_wav()
{
    if (wavfile.loaded)
    {
        if (wavfile.loaded == 1)
            free(wavfile.data);
        else
            delete[] wavfile.data;        
    }

    wavfile.length = 1;
    wavfile.data   = EMPTY_BUFFER;
    wavfile.pos    = 0;
    wavfile.loaded = false;
}
#endif
