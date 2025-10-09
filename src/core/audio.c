#include "engine/audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <mmsystem.h>
#    include <mmreg.h>
#    include <io.h>
#endif

#define AUDIO_ALIAS_MUSIC "bgm_music"
#define AUDIO_MAX_STREAMS 32
#define AUDIO_MAX_PATH    512

typedef enum AudioStreamType {
    AUDIO_STREAM_VOICE = 0,
    AUDIO_STREAM_EFFECT = 1
} AudioStreamType;

#if defined(_WIN32)

typedef struct AudioStreamBuffer {
    WAVEHDR header;
    uint8_t *data;
    struct AudioStreamBuffer *next;
} AudioStreamBuffer;

typedef struct AudioStream {
    HWAVEOUT handle;
    AudioStreamBuffer *head;
    AudioStreamBuffer *tail;
    WAVEFORMATEX format;
    uint8_t id;
    AudioStreamType type;
    float volume;
    uint32_t buffer_count;
    LONG closing;
    int active;
    int auto_close;
} AudioStream;

#endif

typedef struct AudioState {
    float master_volume;
    float music_volume;
    char music_track[AUDIO_MAX_PATH];
    int music_configured;
    int music_playing;
    int music_loop;
    int initialized;
    uint8_t next_effect_id;
#if defined(_WIN32)
    AudioStream streams[AUDIO_MAX_STREAMS];
    CRITICAL_SECTION stream_lock;
    int stream_lock_initialized;
#endif
} AudioState;

static AudioState g_audio = {0};

static float audio_clamp(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

#if defined(_WIN32)

static void audio_copy_string(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0U) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    size_t len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1U;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void audio_log_mci_error(MCIERROR err, const char *context)
{
    if (!err) {
        return;
    }

    char buffer[256];
    if (mciGetErrorStringA(err, buffer, sizeof(buffer))) {
        fprintf(stderr, "[audio] %s failed: %s\n", context, buffer);
    } else {
        fprintf(stderr, "[audio] %s failed: MCI error %lu\n", context, (unsigned long)err);
    }
}

static bool audio_mci_command(const char *command, const char *context)
{
    MCIERROR err = mciSendStringA(command, NULL, 0, NULL);
    if (err != 0U) {
        audio_log_mci_error(err, context);
        return false;
    }
    return true;
}

static void audio_stream_detach_buffer(AudioStream *stream, AudioStreamBuffer *buffer)
{
    if (!stream || !buffer) {
        return;
    }

    AudioStreamBuffer *prev = NULL;
    AudioStreamBuffer *node = stream->head;
    while (node) {
        if (node == buffer) {
            if (prev) {
                prev->next = node->next;
            } else {
                stream->head = node->next;
            }
            if (stream->tail == buffer) {
                stream->tail = prev;
            }
            if (stream->buffer_count > 0U) {
                --stream->buffer_count;
            }
            break;
        }
        prev = node;
        node = node->next;
    }
}

static void CALLBACK audio_waveout_callback(HWAVEOUT device, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2)
{
    (void)param2;
    if (msg != WOM_DONE || !instance || !param1) {
        return;
    }

    AudioStream *stream = (AudioStream *)instance;
    WAVEHDR *header = (WAVEHDR *)param1;
    AudioStreamBuffer *buffer = (AudioStreamBuffer *)header->dwUser;
    if (!stream || !buffer) {
        return;
    }

    waveOutUnprepareHeader(device, header, sizeof(WAVEHDR));

    if (g_audio.stream_lock_initialized) {
        EnterCriticalSection(&g_audio.stream_lock);
        audio_stream_detach_buffer(stream, buffer);
        int empty = stream->head == NULL;
        LeaveCriticalSection(&g_audio.stream_lock);

        free(buffer->data);
        free(buffer);

        if (stream->auto_close && empty && stream->handle && InterlockedCompareExchange(&stream->closing, 1, 0) == 0) {
            waveOutClose(stream->handle);
            stream->handle = NULL;

            EnterCriticalSection(&g_audio.stream_lock);
            stream->head = NULL;
            stream->tail = NULL;
            stream->buffer_count = 0;
            stream->active = 0;
            stream->auto_close = 0;
            stream->volume = 1.0f;
            stream->id = 0;
            memset(&stream->format, 0, sizeof(WAVEFORMATEX));
            LeaveCriticalSection(&g_audio.stream_lock);

            InterlockedExchange(&stream->closing, 0);
        }
    } else {
        free(buffer->data);
        free(buffer);
    }
    (void)device;
}

static AudioStream *audio_stream_find(AudioStreamType type, uint8_t id)
{
    for (size_t i = 0; i < AUDIO_MAX_STREAMS; ++i) {
        AudioStream *stream = &g_audio.streams[i];
        if (stream->active && stream->type == type && stream->id == id) {
            return stream;
        }
    }
    return NULL;
}

static int audio_stream_format_matches(const AudioStream *stream, const WAVEFORMATEX *fmt)
{
    if (!stream || !fmt) {
        return 0;
    }

    return stream->format.wFormatTag == fmt->wFormatTag &&
           stream->format.nChannels == fmt->nChannels &&
           stream->format.nSamplesPerSec == fmt->nSamplesPerSec &&
           stream->format.wBitsPerSample == fmt->wBitsPerSample;
}

static AudioStream *audio_stream_allocate(void)
{
    for (size_t i = 0; i < AUDIO_MAX_STREAMS; ++i) {
        AudioStream *stream = &g_audio.streams[i];
        if (!stream->active && stream->handle == NULL && stream->buffer_count == 0U) {
            memset(stream, 0, sizeof(*stream));
            stream->volume = 1.0f;
            return stream;
        }
    }
    return NULL;
}

static MMRESULT audio_stream_open(AudioStream *stream, const WAVEFORMATEX *fmt, int auto_close)
{
    if (!stream || !fmt) {
        return MMSYSERR_INVALPARAM;
    }

    stream->closing = 0;
    MMRESULT result = waveOutOpen(&stream->handle,
                                  WAVE_MAPPER,
                                  fmt,
                                  (DWORD_PTR)audio_waveout_callback,
                                  (DWORD_PTR)stream,
                                  CALLBACK_FUNCTION);

    if (result == MMSYSERR_NOERROR) {
        stream->format = *fmt;
        stream->head = NULL;
        stream->tail = NULL;
        stream->buffer_count = 0U;
        stream->volume = 1.0f;
        stream->auto_close = auto_close;
        stream->active = 1;
    }

    return result;
}

static void audio_stream_close(AudioStream *stream)
{
    if (!stream || !stream->handle) {
        return;
    }

    InterlockedExchange(&stream->closing, 1);
    waveOutReset(stream->handle);
    waveOutClose(stream->handle);
    stream->handle = NULL;

    if (g_audio.stream_lock_initialized) {
        EnterCriticalSection(&g_audio.stream_lock);
        AudioStreamBuffer *node = stream->head;
        while (node) {
            AudioStreamBuffer *next = node->next;
            free(node->data);
            free(node);
            node = next;
        }
        stream->head = NULL;
        stream->tail = NULL;
        stream->buffer_count = 0U;
        LeaveCriticalSection(&g_audio.stream_lock);
    }

    stream->active = 0;
    stream->auto_close = 0;
    stream->volume = 1.0f;
    stream->id = 0;
    memset(&stream->format, 0, sizeof(WAVEFORMATEX));
    InterlockedExchange(&stream->closing, 0);
}

static AudioStream *audio_stream_acquire(AudioStreamType type,
                                         uint8_t id,
                                         const WAVEFORMATEX *fmt,
                                         int auto_close)
{
    AudioStream *stream = audio_stream_find(type, id);
    if (stream) {
        if (!audio_stream_format_matches(stream, fmt)) {
            audio_stream_close(stream);
            stream = NULL;
        }
    }

    if (!stream) {
        stream = audio_stream_allocate();
        if (!stream) {
            fprintf(stderr, "[audio] no available stream slots\n");
            return NULL;
        }

        if (audio_stream_open(stream, fmt, auto_close) != MMSYSERR_NOERROR) {
            fprintf(stderr, "[audio] failed to open audio stream\n");
            memset(stream, 0, sizeof(*stream));
            return NULL;
        }

        stream->type = type;
        stream->id = id;
    }

    return stream;
}

static bool audio_stream_queue_pcm(AudioStream *stream,
                                   const int16_t *samples,
                                   size_t frame_count,
                                   float volume)
{
    if (!stream || !stream->handle || !samples || frame_count == 0U) {
        return false;
    }

    const size_t channels = stream->format.nChannels ? stream->format.nChannels : 1U;
    const size_t total_samples = frame_count * channels;
    const size_t byte_count = total_samples * sizeof(int16_t);

    AudioStreamBuffer *buffer = (AudioStreamBuffer *)calloc(1, sizeof(AudioStreamBuffer));
    if (!buffer) {
        return false;
    }

    buffer->data = (uint8_t *)malloc(byte_count);
    if (!buffer->data) {
        free(buffer);
        return false;
    }

    int16_t *dst = (int16_t *)buffer->data;
    float gain = audio_clamp(volume, 0.0f, 1.0f) * audio_clamp(g_audio.master_volume, 0.0f, 1.0f);

    for (size_t i = 0; i < total_samples; ++i) {
        float sample = (float)samples[i] * gain;
        if (sample > 32767.0f) {
            sample = 32767.0f;
        } else if (sample < -32768.0f) {
            sample = -32768.0f;
        }
        dst[i] = (int16_t)sample;
    }

    memset(&buffer->header, 0, sizeof(WAVEHDR));
    buffer->header.lpData = (LPSTR)buffer->data;
    buffer->header.dwBufferLength = (DWORD)byte_count;
    buffer->header.dwUser = (DWORD_PTR)buffer;

    if (g_audio.stream_lock_initialized) {
        EnterCriticalSection(&g_audio.stream_lock);
        if (!stream->head) {
            stream->head = buffer;
        } else {
            stream->tail->next = buffer;
        }
        stream->tail = buffer;
        ++stream->buffer_count;
        LeaveCriticalSection(&g_audio.stream_lock);
    } else {
        if (!stream->head) {
            stream->head = buffer;
        } else {
            stream->tail->next = buffer;
        }
        stream->tail = buffer;
        ++stream->buffer_count;
    }

    MMRESULT prepare_result = waveOutPrepareHeader(stream->handle, &buffer->header, sizeof(WAVEHDR));
    if (prepare_result != MMSYSERR_NOERROR) {
        if (g_audio.stream_lock_initialized) {
            EnterCriticalSection(&g_audio.stream_lock);
            audio_stream_detach_buffer(stream, buffer);
            LeaveCriticalSection(&g_audio.stream_lock);
        } else {
            audio_stream_detach_buffer(stream, buffer);
        }
        free(buffer->data);
        free(buffer);
        return false;
    }

    MMRESULT write_result = waveOutWrite(stream->handle, &buffer->header, sizeof(WAVEHDR));
    if (write_result != MMSYSERR_NOERROR) {
        waveOutUnprepareHeader(stream->handle, &buffer->header, sizeof(WAVEHDR));
        if (g_audio.stream_lock_initialized) {
            EnterCriticalSection(&g_audio.stream_lock);
            audio_stream_detach_buffer(stream, buffer);
            LeaveCriticalSection(&g_audio.stream_lock);
        } else {
            audio_stream_detach_buffer(stream, buffer);
        }
        free(buffer->data);
        free(buffer);
        return false;
    }

    return true;
}

typedef struct WavInfo {
    int16_t *samples;
    size_t frame_count;
    WAVEFORMATEX format;
} WavInfo;

static bool audio_load_wav(const char *path, WavInfo *out_info)
{
    if (!path || !out_info) {
        return false;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "[audio] failed to open wav: %s\n", path);
        return false;
    }

    char riff[4];
    if (fread(riff, 1, 4, fp) != 4 || memcmp(riff, "RIFF", 4) != 0) {
        fclose(fp);
        fprintf(stderr, "[audio] invalid wav (missing RIFF): %s\n", path);
        return false;
    }

    uint32_t riff_size = 0;
    fread(&riff_size, sizeof(uint32_t), 1, fp);

    char wave[4];
    if (fread(wave, 1, 4, fp) != 4 || memcmp(wave, "WAVE", 4) != 0) {
        fclose(fp);
        fprintf(stderr, "[audio] invalid wav (missing WAVE): %s\n", path);
        return false;
    }

    WAVEFORMATEX fmt = {0};
    int got_fmt = 0;
    int got_data = 0;
    int16_t *samples = NULL;
    uint32_t data_size = 0;

    while (!feof(fp)) {
        char chunk_id[4];
        uint32_t chunk_size = 0;
        if (fread(chunk_id, 1, 4, fp) != 4) {
            break;
        }
        if (fread(&chunk_size, sizeof(uint32_t), 1, fp) != 1) {
            break;
        }

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            if (chunk_size < sizeof(WAVEFORMATEX) - sizeof(uint16_t)) {
                fclose(fp);
                fprintf(stderr, "[audio] unsupported wav fmt chunk: %s\n", path);
                return false;
            }

            uint16_t audio_format = 0;
            uint16_t channels = 0;
            uint32_t sample_rate = 0;
            uint32_t byte_rate = 0;
            uint16_t block_align = 0;
            uint16_t bits_per_sample = 0;

            fread(&audio_format, sizeof(uint16_t), 1, fp);
            fread(&channels, sizeof(uint16_t), 1, fp);
            fread(&sample_rate, sizeof(uint32_t), 1, fp);
            fread(&byte_rate, sizeof(uint32_t), 1, fp);
            fread(&block_align, sizeof(uint16_t), 1, fp);
            fread(&bits_per_sample, sizeof(uint16_t), 1, fp);

            if (chunk_size > 16) {
                fseek(fp, chunk_size - 16, SEEK_CUR);
            }

            if (audio_format != WAVE_FORMAT_PCM || (bits_per_sample != 16)) {
                fclose(fp);
                fprintf(stderr, "[audio] unsupported wav format (only 16-bit PCM): %s\n", path);
                return false;
            }

            fmt.wFormatTag = WAVE_FORMAT_PCM;
            fmt.nChannels = channels;
            fmt.nSamplesPerSec = sample_rate;
            fmt.wBitsPerSample = bits_per_sample;
            fmt.nBlockAlign = block_align;
            fmt.nAvgBytesPerSec = byte_rate;
            fmt.cbSize = 0;
            got_fmt = 1;
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            if (!got_fmt) {
                fclose(fp);
                fprintf(stderr, "[audio] wav missing fmt chunk before data: %s\n", path);
                return false;
            }

            samples = (int16_t *)malloc(chunk_size);
            if (!samples) {
                fclose(fp);
                fprintf(stderr, "[audio] out of memory reading wav: %s\n", path);
                return false;
            }

            if (fread(samples, 1, chunk_size, fp) != chunk_size) {
                free(samples);
                fclose(fp);
                fprintf(stderr, "[audio] truncated wav data: %s\n", path);
                return false;
            }

            data_size = chunk_size;
            got_data = 1;
        } else {
            fseek(fp, chunk_size, SEEK_CUR);
        }

        if (got_fmt && got_data) {
            break;
        }
    }

    fclose(fp);

    if (!got_fmt || !got_data || data_size == 0U) {
        if (samples) {
            free(samples);
        }
        fprintf(stderr, "[audio] incomplete wav file: %s\n", path);
        return false;
    }

    size_t frame_count = data_size / fmt.nBlockAlign;

    out_info->samples = samples;
    out_info->frame_count = frame_count;
    out_info->format = fmt;
    return true;
}

#endif /* defined(_WIN32) */

static void audio_music_apply_volume(void)
{
#if defined(_WIN32)
    if (!g_audio.music_playing) {
        return;
    }

    float gain = audio_clamp(g_audio.music_volume, 0.0f, 1.0f) * audio_clamp(g_audio.master_volume, 0.0f, 1.0f);
    unsigned int level = (unsigned int)(gain * 1000.0f);
    if (level > 1000U) {
        level = 1000U;
    }

    char command[128];
    snprintf(command, sizeof(command), "setaudio " AUDIO_ALIAS_MUSIC " volume to %u", level);
    audio_mci_command(command, "set volume");
#endif
}

bool audio_init(void)
{
    memset(&g_audio, 0, sizeof(g_audio));
    g_audio.initialized = 1;
    g_audio.master_volume = 1.0f;
    g_audio.music_volume = 1.0f;
    g_audio.next_effect_id = 1;

#if defined(_WIN32)
    InitializeCriticalSection(&g_audio.stream_lock);
    g_audio.stream_lock_initialized = 1;
#endif

    return true;
}

void audio_shutdown(void)
{
    if (!g_audio.initialized) {
        return;
    }

    audio_music_stop();
    audio_voice_stop_all();

#if defined(_WIN32)
    for (size_t i = 0; i < AUDIO_MAX_STREAMS; ++i) {
        AudioStream *stream = &g_audio.streams[i];
        if (stream->handle) {
            audio_stream_close(stream);
        }
    }

    if (g_audio.stream_lock_initialized) {
        DeleteCriticalSection(&g_audio.stream_lock);
        g_audio.stream_lock_initialized = 0;
    }
#endif

    memset(&g_audio, 0, sizeof(g_audio));
}

void audio_set_master_volume(float volume)
{
    g_audio.master_volume = audio_clamp(volume, 0.0f, 1.0f);
    audio_music_apply_volume();
}

float audio_master_volume(void)
{
    return g_audio.master_volume;
}

bool audio_music_set_track(const char *path)
{
    if (!g_audio.initialized || !path || !path[0]) {
        return false;
    }

#if defined(_WIN32)
    char full_path[AUDIO_MAX_PATH] = {0};
    if (!_fullpath(full_path, path, sizeof(full_path))) {
        audio_copy_string(full_path, sizeof(full_path), path);
    }

    if (_access(full_path, 0) != 0) {
        fprintf(stderr, "[audio] cannot access music file: %s\n", full_path);
        g_audio.music_configured = 0;
        g_audio.music_track[0] = '\0';
        return false;
    }

    audio_copy_string(g_audio.music_track, sizeof(g_audio.music_track), full_path);
    g_audio.music_configured = 1;
    return true;
#else
    (void)path;
    return false;
#endif
}

bool audio_music_play(float volume, bool loop)
{
    if (!g_audio.initialized || !g_audio.music_configured) {
        return false;
    }

    g_audio.music_volume = audio_clamp(volume, 0.0f, 1.0f);
    g_audio.music_loop = loop ? 1 : 0;

#if defined(_WIN32)
    if (g_audio.music_playing) {
        audio_music_stop();
    }

    char command[1024];
    snprintf(command, sizeof(command), "open \"%s\" alias " AUDIO_ALIAS_MUSIC, g_audio.music_track);
    if (!audio_mci_command(command, "open music")) {
        g_audio.music_playing = 0;
        return false;
    }

    audio_music_apply_volume();

    snprintf(command,
             sizeof(command),
             loop ? "play " AUDIO_ALIAS_MUSIC " repeat" : "play " AUDIO_ALIAS_MUSIC);
    if (!audio_mci_command(command, "play music")) {
        audio_mci_command("close " AUDIO_ALIAS_MUSIC, "close music");
        g_audio.music_playing = 0;
        return false;
    }

    g_audio.music_playing = 1;
    return true;
#else
    return false;
#endif
}

void audio_music_stop(void)
{
#if defined(_WIN32)
    if (g_audio.music_playing) {
        audio_mci_command("stop " AUDIO_ALIAS_MUSIC, "stop music");
        audio_mci_command("close " AUDIO_ALIAS_MUSIC, "close music");
    }
#endif

    g_audio.music_playing = 0;
}

bool audio_music_is_playing(void)
{
    return g_audio.music_playing != 0;
}

void audio_music_set_volume(float volume)
{
    g_audio.music_volume = audio_clamp(volume, 0.0f, 1.0f);
    audio_music_apply_volume();
}

float audio_music_volume(void)
{
    return g_audio.music_volume;
}

bool audio_effect_play_file(const char *path, float volume)
{
    if (!g_audio.initialized || !path || !path[0]) {
        return false;
    }

    float gain = audio_clamp(volume, 0.0f, 1.0f);
    if (gain <= 0.0f) {
        return true;
    }

#if defined(_WIN32)
    WavInfo info = {0};
    if (!audio_load_wav(path, &info)) {
        return false;
    }

    uint8_t stream_id = g_audio.next_effect_id++;
    if (g_audio.next_effect_id == 0U) {
        g_audio.next_effect_id = 1U;
    }

    AudioStream *stream = audio_stream_acquire(AUDIO_STREAM_EFFECT, stream_id, &info.format, 1);
    if (!stream) {
        free(info.samples);
        return false;
    }

    bool queued = audio_stream_queue_pcm(stream, info.samples, info.frame_count, gain);
    free(info.samples);
    return queued;
#else
    (void)path;
    (void)gain;
    return false;
#endif
}

bool audio_voice_submit(uint8_t speaker_id, const AudioVoiceFrame *frame)
{
    if (!g_audio.initialized || !frame || !frame->samples || frame->sample_count == 0U) {
        return false;
    }

    float gain = audio_clamp(frame->volume, 0.0f, 1.0f);
    if (gain <= 0.0f) {
        return true;
    }

#if defined(_WIN32)
    WAVEFORMATEX fmt = {0};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = frame->channels > 0U ? frame->channels : 1U;
    fmt.nSamplesPerSec = frame->sample_rate > 0U ? frame->sample_rate : 16000U;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = fmt.nChannels * (fmt.wBitsPerSample / 8);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    fmt.cbSize = 0;

    AudioStream *stream = audio_stream_acquire(AUDIO_STREAM_VOICE, speaker_id, &fmt, 0);
    if (!stream) {
        return false;
    }

    return audio_stream_queue_pcm(stream, frame->samples, frame->sample_count, gain);
#else
    (void)speaker_id;
    (void)frame;
    return false;
#endif
}

void audio_voice_stop(uint8_t speaker_id)
{
#if defined(_WIN32)
    AudioStream *stream = audio_stream_find(AUDIO_STREAM_VOICE, speaker_id);
    if (stream) {
        audio_stream_close(stream);
    }
#else
    (void)speaker_id;
#endif
}

void audio_voice_stop_all(void)
{
#if defined(_WIN32)
    for (size_t i = 0; i < AUDIO_MAX_STREAMS; ++i) {
        AudioStream *stream = &g_audio.streams[i];
        if (stream->active && stream->type == AUDIO_STREAM_VOICE) {
            audio_stream_close(stream);
        }
    }
#endif
}
