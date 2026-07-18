#define STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.c"

#include "harness/audio.h"

#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define AUDIOBACKEND_MAX_VOLUME 128

struct tAudioBackend_stream_impl {
    SDL_AudioStream* stream;
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
    SDL_AudioStream* stream;
    Uint8* pcm_data;
    Uint32 pcm_len;
    int playing;
    int loop;
    int volume;
    Uint32 end_time;
} cda = { .volume = AUDIOBACKEND_MAX_VOLUME };

static void AudioBackend_CloseStreamDevice(tAudioBackend_stream_impl* stream) {
    if (stream->stream != NULL) {
        SDL_DestroyAudioStream(stream->stream);
        stream->stream = NULL;
    }
    if (stream->owned_data != NULL) {
        free(stream->owned_data);
        stream->owned_data = NULL;
        stream->owned_len = 0;
    }
    stream->initialized = 0;
    stream->looping = 0;
}

static int AudioBackend_OpenStreamAndQueue(tAudioBackend_stream_impl* stream, const SDL_AudioSpec* spec, const Uint8* data, Uint32 size) {
    stream->spec = *spec;
    stream->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &stream->spec, NULL, NULL);
    if (stream->stream == NULL) {
        printf("AUDIO: SDL_OpenAudioDeviceStream failed: %s\n", SDL_GetError());
        return 0;
    }
    if (!SDL_PutAudioStreamData(stream->stream, data, (int)size)) {
        printf("AUDIO: SDL_PutAudioStreamData failed: %s\n", SDL_GetError());
        SDL_DestroyAudioStream(stream->stream);
        stream->stream = NULL;
        return 0;
    }
    SDL_ResumeAudioStreamDevice(stream->stream);
    stream->initialized = 1;
    return 1;
}

tAudioBackend_error_code AudioBackend_Init(void) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        printf("SDL audio init error: %s\n", SDL_GetError());
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
    if (cda.stream != NULL) {
        SDL_ClearAudioStream(cda.stream);
        SDL_DestroyAudioStream(cda.stream);
        cda.stream = NULL;
    }
    if (cda.pcm_data != NULL) {
        free(cda.pcm_data);
        cda.pcm_data = NULL;
    }
    cda.pcm_len = 0;
    cda.playing = 0;
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_PlayCDA(int track) {
    char path[256];
    short* decoded;
    int channels, sample_rate;
    int samples;
    SDL_AudioSpec pcm_spec;

    snprintf(path, sizeof(path), "MUSIC/Track%02d.ogg", track);
    AudioBackend_StopCDA();

    samples = stb_vorbis_decode_filename(path, &channels, &sample_rate, &decoded);
    if (decoded == NULL) {
        return eAB_error;
    }

    cda.pcm_len = samples * channels * (int)sizeof(short);
    cda.pcm_data = (Uint8*)decoded;

    SDL_zero(pcm_spec);
    pcm_spec.format = SDL_AUDIO_S16;
    pcm_spec.channels = channels;
    pcm_spec.freq = sample_rate;

    cda.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &pcm_spec, NULL, NULL);
    if (cda.stream == NULL) {
        AudioBackend_StopCDA();
        return eAB_error;
    }
    if (!SDL_PutAudioStreamData(cda.stream, cda.pcm_data, (int)cda.pcm_len)) {
        AudioBackend_StopCDA();
        return eAB_error;
    }
    { float g = cda.volume / 256.0f; if (g < 0.05f) g = 0.05f; SDL_SetAudioStreamGain(cda.stream, g); }
    SDL_ResumeAudioStreamDevice(cda.stream);
    cda.loop = 1;
    cda.playing = 1;
    cda.end_time = SDL_GetTicks() + (cda.pcm_len * 1000 / (channels * sample_rate * (int)sizeof(short))) + 200;
    return eAB_success;
}

int AudioBackend_CDAIsPlaying(void) {
    if (cda.stream == NULL) {
        return 0;
    }
    if (!cda.playing) {
        return 0;
    }
    if (SDL_GetAudioStreamQueued(cda.stream) == 0) {
        if (cda.loop) {
            SDL_PutAudioStreamData(cda.stream, cda.pcm_data, (int)cda.pcm_len);
        }
    }
    return 1;
}

tAudioBackend_error_code AudioBackend_SetCDAVolume(int volume) {
    cda.volume = volume;
    if (cda.stream != NULL) {
        float g = volume / 256.0f;
        if (g < 0.05f) g = 0.05f;
        SDL_SetAudioStreamGain(cda.stream, g);
    }
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

    printf("AUDIO: PlaySample ch=%d sz=%d rt=%d bd=%d loop=%d\n", channels, size, rate, bit_depth, loop);

    AudioBackend_CloseStreamDevice(stream);

    SDL_zero(spec);
    spec.freq = rate;
    spec.format = (bit_depth == 16) ? SDL_AUDIO_S16 : SDL_AUDIO_U8;
    spec.channels = channels;

    if (loop) {
        stream->owned_data = (Uint8*)malloc(size);
        if (stream->owned_data) {
            memcpy(stream->owned_data, data, size);
            stream->owned_len = (Uint32)size;
        }
    }

    if (!AudioBackend_OpenStreamAndQueue(stream, &spec, (const Uint8*)data, (Uint32)size)) {
        printf("PlaySample: OpenStreamAndQueue failed (ch=%d sz=%d rt=%d loop=%d)\n", channels, size, rate, loop);
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
            Uint64 bytes_per_sample = (Uint64)SDL_AUDIO_BYTESIZE((bit_depth == 16) ? SDL_AUDIO_S16 : SDL_AUDIO_U8);
            Uint64 duration_ms = (Uint64)size * 1000 / ((Uint64)channels * (Uint64)rate * bytes_per_sample);
            stream->end_time = SDL_GetTicks() + (Uint32)duration_ms + 500;
        } else {
            stream->end_time = SDL_GetTicks() + 500;
        }
    }
    float g = stream->volume / 256.0f;
    if (g < 0.05f) g = 0.05f;
    SDL_SetAudioStreamGain(stream->stream, g);
    if (stream->frequency > 0 && stream->frequency != rate) {
        SDL_SetAudioStreamFrequencyRatio(stream->stream, (float)stream->frequency / (float)rate);
    }
    return eAB_success;
}

int AudioBackend_SoundIsPlaying(void* type_struct_sample) {
    tAudioBackend_stream_impl* stream = (tAudioBackend_stream_impl*)type_struct_sample;
    assert(stream != NULL);

    if (stream->stream == NULL || !stream->initialized) {
        return 0;
    }

    if (stream->looping) {
        int queued = SDL_GetAudioStreamQueued(stream->stream);
        if (stream->owned_data != NULL && stream->owned_len > 0) {
            if (queued < (int)stream->owned_len / 2) {
                SDL_PutAudioStreamData(stream->stream, stream->owned_data, (int)stream->owned_len);
            }
        }
        return 1;
    }

    if (SDL_GetTicks() < stream->end_time) {
        return 1;
    }

    int q = SDL_GetAudioStreamQueued(stream->stream);
    int a = SDL_GetAudioStreamAvailable(stream->stream);
    if (q > 0 || a > 0) {
        return 1;
    }

    return 0;
}

tAudioBackend_error_code AudioBackend_StopSample(void* type_struct_sample) {
    tAudioBackend_stream_impl* stream = (tAudioBackend_stream_impl*)type_struct_sample;
    assert(stream != NULL);
    // Don't destroy the stream — let remaining buffered audio play out naturally.
    // CloseStreamDevice (called by PlaySample when this channel is reused) will
    // clean up. Without this, sounds stopped by S3StopOutletSound (e.g. during
    // menu→race transitions) cut instantaneously instead of playing to completion.
    stream->looping = 0;
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_SetVolume(void* type_struct_sample, int volume) {
    tAudioBackend_stream_impl* stream = (tAudioBackend_stream_impl*)type_struct_sample;
    assert(stream != NULL);
    stream->volume = volume;
    if (stream->stream != NULL) {
        float g = volume / 256.0f;
        if (g < 0.05f) g = 0.05f;
        SDL_SetAudioStreamGain(stream->stream, g);
    }
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_SetPan(void* type_struct_sample, int pan) {
    ((tAudioBackend_stream_impl*)type_struct_sample)->pan = pan;
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_SetFrequency(void* type_struct_sample, int original_rate, int new_rate) {
    (void)original_rate;
    tAudioBackend_stream_impl* stream = (tAudioBackend_stream_impl*)type_struct_sample;
    assert(stream != NULL);
    stream->frequency = new_rate;
    if (stream->stream != NULL) {
        SDL_SetAudioStreamFrequencyRatio(stream->stream, new_rate / (float)original_rate);
    }
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
    spec.format = bit_depth == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16;
    spec.channels = channels;

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

    if (stream->stream == NULL) {
        if (!AudioBackend_OpenStreamAndQueue(stream, &stream->spec, data, (Uint32)size)) {
            return eAB_error;
        }
    } else {
        if (!SDL_PutAudioStreamData(stream->stream, data, (int)size)) {
            return eAB_error;
        }
    }
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_StreamClose(tAudioBackend_stream* stream_handle) {
    tAudioBackend_stream_impl* stream = (tAudioBackend_stream_impl*)stream_handle;
    if (stream) {
        AudioBackend_CloseStreamDevice(stream);
        free(stream);
    }
    return eAB_success;
}

#undef STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.c"
