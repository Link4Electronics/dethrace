#define STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.c"

#include "harness/audio.h"

#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CDA_AUDIO_SAMPLES 4096
#define AUDIOBACKEND_MAX_VOLUME 128

struct tAudioBackend_stream_impl {
    SDL_AudioDeviceID device;
    SDL_AudioSpec spec;
    Uint8* owned_data;
    Uint32 owned_len;
    int volume;
    int pan;
    int frequency;
    int initialized;
    int looping;
    Uint32 end_time;
};

typedef struct tAudioBackend_stream_impl tAudioBackend_stream_impl;

static struct {
    SDL_AudioDeviceID device;
    SDL_AudioSpec spec;
    Uint8* pcm_data;
    Uint32 pcm_len;
    volatile Uint32 play_pos;
    int playing;
    int loop;
    int volume;
} cda;

static int AudioBackend_OpenAndQueue(tAudioBackend_stream_impl* stream, const SDL_AudioSpec* spec, const Uint8* data, Uint32 size) {
    stream->device = SDL_OpenAudioDevice(NULL, 0, spec, NULL, 0);
    if (stream->device == 0) {
        return 0;
    }
    if (SDL_QueueAudio(stream->device, data, size) < 0) {
        SDL_CloseAudioDevice(stream->device);
        stream->device = 0;
        return 0;
    }
    SDL_PauseAudioDevice(stream->device, 0);
    stream->initialized = 1;
    return 1;
}

static void AudioBackend_CloseQueuedDevice(tAudioBackend_stream_impl* stream) {
    if (stream->device != 0) {
        SDL_ClearQueuedAudio(stream->device);
        SDL_CloseAudioDevice(stream->device);
        stream->device = 0;
    }
    stream->initialized = 0;
}

static void SDLCALL AudioBackend_CDAFill(void* userdata, Uint8* stream, int len) {
    (void)userdata;
    SDL_memset(stream, cda.spec.silence, len);
    if (!cda.playing || cda.pcm_data == NULL) {
        return;
    }

    while (len > 0) {
        Uint32 remaining = cda.pcm_len - cda.play_pos;
        if (remaining == 0) {
            if (cda.loop) {
                cda.play_pos = 0;
                remaining = cda.pcm_len;
            } else {
                cda.playing = 0;
                return;
            }
        }
        Uint32 copy_len = SDL_min((Uint32)len, remaining);
        SDL_memcpy(stream, cda.pcm_data + cda.play_pos, copy_len);
        cda.play_pos += copy_len;
        stream += copy_len;
        len -= (int)copy_len;
    }
}

tAudioBackend_error_code AudioBackend_Init(void) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        return eAB_error;
    }
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_InitCDA(void) {
    if (access("MUSIC/Track02.ogg", F_OK) == -1) {
        return eAB_error;
    }
    return eAB_success;
}

void AudioBackend_UnInit(void) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void AudioBackend_UnInitCDA(void) {
    AudioBackend_StopCDA();
}

tAudioBackend_error_code AudioBackend_StopCDA(void) {
    if (cda.device != 0) {
        cda.playing = 0;
        SDL_CloseAudioDevice(cda.device);
        cda.device = 0;
    }
    if (cda.pcm_data != NULL) {
        free(cda.pcm_data);
        cda.pcm_data = NULL;
    }
    cda.pcm_len = 0;
    cda.play_pos = 0;
    cda.playing = 0;
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_PlayCDA(int track) {
    char path[256];
    short* decoded;
    int channels, sample_rate;
    int samples;

    snprintf(path, sizeof(path), "MUSIC/Track%02d.ogg", track);
    AudioBackend_StopCDA();

    samples = stb_vorbis_decode_filename(path, &channels, &sample_rate, &decoded);
    if (decoded == NULL) {
        return eAB_error;
    }

    cda.pcm_len = samples * channels * (int)sizeof(short);
    cda.pcm_data = (Uint8*)decoded;
    cda.spec.freq = sample_rate;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    cda.spec.format = AUDIO_S16MSB;
#else
    cda.spec.format = AUDIO_S16LSB;
#endif
    cda.spec.channels = (Uint8)channels;
    cda.spec.silence = 0;
    cda.spec.samples = CDA_AUDIO_SAMPLES;
    cda.spec.callback = AudioBackend_CDAFill;
    cda.loop = 1;
    cda.play_pos = 0;
    cda.playing = 1;

    cda.device = SDL_OpenAudioDevice(NULL, 0, &cda.spec, NULL, 0);
    if (cda.device == 0) {
        AudioBackend_StopCDA();
        return eAB_error;
    }
    SDL_PauseAudioDevice(cda.device, 0);
    return eAB_success;
}

int AudioBackend_CDAIsPlaying(void) {
    return cda.playing && cda.device != 0 && SDL_GetAudioDeviceStatus(cda.device) == SDL_AUDIO_PLAYING;
}

tAudioBackend_error_code AudioBackend_SetCDAVolume(int volume) {
    cda.volume = volume;
    return eAB_success;
}

void* AudioBackend_AllocateSampleTypeStruct(void) {
    tAudioBackend_stream_impl* s = (tAudioBackend_stream_impl*)malloc(sizeof(tAudioBackend_stream_impl));
    if (s) {
        memset(s, 0, sizeof(*s));
        s->volume = AUDIOBACKEND_MAX_VOLUME;
    }
    return s;
}

tAudioBackend_error_code AudioBackend_PlaySample(void* type_struct_sample, int channels, void* data, int size, int rate, int bit_depth, int loop) {
    SDL_AudioSpec spec;
    tAudioBackend_stream_impl* stream = (tAudioBackend_stream_impl*)type_struct_sample;

    assert(stream != NULL);
    if (data == NULL || size <= 0) return eAB_error;

    AudioBackend_CloseQueuedDevice(stream);

    if (loop) {
        stream->owned_data = (Uint8*)malloc(size);
        if (stream->owned_data) {
            memcpy(stream->owned_data, data, size);
            stream->owned_len = (Uint32)size;
        }
    }

    SDL_zero(spec);
    spec.freq = stream->frequency > 0 ? stream->frequency : rate;
    spec.format = (bit_depth == 16) ? AUDIO_S16SYS : AUDIO_U8;
    spec.channels = (Uint8)channels;
    spec.samples = 4096;

    if (!AudioBackend_OpenAndQueue(stream, &spec, (const Uint8*)data, (Uint32)size)) {
        if (stream->owned_data) {
            free(stream->owned_data);
            stream->owned_data = NULL;
            stream->owned_len = 0;
        }
        return eAB_error;
    }
    stream->looping = loop ? 1 : 0;
    if (!loop) {
        if (channels > 0 && rate > 0 && bit_depth > 0) {
            int bytes_per_sample = (bit_depth + 7) / 8;
            Uint64 duration_ms = (Uint64)size * 1000 / ((Uint64)channels * (Uint64)rate * (Uint64)bytes_per_sample);
            stream->end_time = SDL_GetTicks() + (Uint32)duration_ms + 500;
        } else {
            stream->end_time = SDL_GetTicks() + 500;
        }
    }
    return eAB_success;
}

int AudioBackend_SoundIsPlaying(void* type_struct_sample) {
    tAudioBackend_stream_impl* stream = (tAudioBackend_stream_impl*)type_struct_sample;
    assert(stream != NULL);

    if (stream->device == 0) return 0;

    if (stream->looping) {
        Uint32 queued = SDL_GetQueuedAudioSize(stream->device);
        if (stream->owned_data != NULL && stream->owned_len > 0) {
            if (queued < stream->owned_len / 2) {
                SDL_QueueAudio(stream->device, stream->owned_data, stream->owned_len);
            }
        }
        return 1;
    }

    if (SDL_GetTicks() < stream->end_time) {
        return 1;
    }

    if (SDL_GetQueuedAudioSize(stream->device) > 0) {
        return 1;
    }

    return 0;
}

tAudioBackend_error_code AudioBackend_StopSample(void* type_struct_sample) {
    tAudioBackend_stream_impl* stream = (tAudioBackend_stream_impl*)type_struct_sample;
    assert(stream != NULL);
    stream->looping = 0;
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_SetVolume(void* type_struct_sample, int volume) {
    (void)type_struct_sample; (void)volume;
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_SetPan(void* type_struct_sample, int pan) {
    ((tAudioBackend_stream_impl*)type_struct_sample)->pan = pan;
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_SetFrequency(void* type_struct_sample, int original_rate, int new_rate) {
    (void)original_rate;
    ((tAudioBackend_stream_impl*)type_struct_sample)->frequency = new_rate;
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_SetVolumeSeparate(void* type_struct_sample, int left_volume, int right_volume) {
    (void)type_struct_sample; (void)left_volume; (void)right_volume;
    return eAB_error;
}

tAudioBackend_stream* AudioBackend_StreamOpen(int bit_depth, int channels, unsigned int sample_rate) {
    tAudioBackend_stream_impl* stream;
    SDL_AudioSpec spec;

    if (bit_depth != 8 && bit_depth != 16) return NULL;

    SDL_zero(spec);
    spec.freq = (int)sample_rate;
    spec.format = bit_depth == 8 ? AUDIO_U8 : AUDIO_S16SYS;
    spec.channels = (Uint8)channels;
    spec.samples = 2048;

    stream = (tAudioBackend_stream_impl*)malloc(sizeof(tAudioBackend_stream_impl));
    if (!stream) return NULL;
    memset(stream, 0, sizeof(*stream));
    stream->volume = AUDIOBACKEND_MAX_VOLUME;
    stream->spec = spec;
    return (tAudioBackend_stream*)stream;
}

tAudioBackend_error_code AudioBackend_StreamWrite(tAudioBackend_stream* stream_handle, const unsigned char* data, unsigned long size) {
    tAudioBackend_stream_impl* stream = (tAudioBackend_stream_impl*)stream_handle;
    if (!stream || !data || !size || size > 0xffffffffu) return eAB_error;

    if (stream->device == 0) {
        if (!AudioBackend_OpenAndQueue(stream, &stream->spec, data, (Uint32)size)) {
            return eAB_error;
        }
    } else {
        if (SDL_QueueAudio(stream->device, data, (Uint32)size) < 0) {
            return eAB_error;
        }
    }
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_StreamClose(tAudioBackend_stream* stream_handle) {
    tAudioBackend_stream_impl* stream = (tAudioBackend_stream_impl*)stream_handle;
    if (stream) {
        AudioBackend_CloseQueuedDevice(stream);
        free(stream);
    }
    return eAB_success;
}

#undef STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.c"
