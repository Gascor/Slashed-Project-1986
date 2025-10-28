#include "engine/audio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#    ifndef COBJMACROS
#        define COBJMACROS
#    endif
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <unknwn.h>
#    include <mmdeviceapi.h>
#    include <audioclient.h>
#    include <functiondiscoverykeys_devpkey.h>
#    include <avrt.h>
#    include <mmsystem.h>
#    include <mmreg.h>
#    include <ksmedia.h>
#    include <io.h>
#    include <initguid.h>
#endif

#define VOICE_TARGET_RATE 16000U
#define VOICE_CHANNELS    1U
#define VOICE_QUEUE_LIMIT 64U
#define VOICE_RING_FRAMES (VOICE_TARGET_RATE * 4U)
#define AUDIO_ALIAS_MUSIC "bgm_music"
#define AUDIO_MAX_PATH    512

typedef struct AudioState {
    float master_volume;
    float music_volume;
    float effects_volume;
    float voice_volume;
    float microphone_volume;
    int initialized;
    int music_configured;
    int music_playing;
    int music_loop;
    char music_track[AUDIO_MAX_PATH];
    uint32_t output_device_token;
    uint32_t input_device_token;
#if defined(_WIN32)
    IMMDeviceEnumerator *device_enumerator;
#endif
} AudioState;

static AudioState g_audio = {
    .master_volume = 1.0f,
    .music_volume = 1.0f,
    .effects_volume = 1.0f,
    .voice_volume = 1.0f,
    .microphone_volume = 1.0f,
    .initialized = 0,
    .music_configured = 0,
    .music_playing = 0,
    .music_loop = 0,
    .music_track = {0},
    .output_device_token = UINT32_MAX,
    .input_device_token = UINT32_MAX,
#if defined(_WIN32)
    .device_enumerator = NULL
#endif
};

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

static void audio_log(const char *message)
{
    fprintf(stderr, "[audio] %s\n", message);
}

#if defined(_WIN32)

DEFINE_GUID(CLSID_MMDeviceEnumerator,
            0xbcde0395,
            0xe52f,
            0x467c,
            0x8e,
            0x3d,
            0xc4,
            0x57,
            0x92,
            0x91,
            0x69,
            0x2e);

DEFINE_GUID(IID_IMMDeviceEnumerator,
            0xa95664d2,
            0x9614,
            0x4f35,
            0xa7,
            0x46,
            0xde,
            0x8d,
            0xb6,
            0x36,
            0x17,
            0xe6);

DEFINE_GUID(IID_IAudioClient,
            0x1cb9ad4c,
            0xdbfa,
            0x4c32,
            0xb1,
            0x78,
            0xc2,
            0xf5,
            0x68,
            0xa7,
            0x03,
            0xb2);

DEFINE_GUID(IID_IAudioRenderClient,
            0xf294acfc,
            0x3146,
            0x4483,
            0xa7,
            0xbf,
            0xad,
            0xdc,
            0xa7,
            0xc2,
            0x60,
            0xe2);

DEFINE_GUID(IID_IAudioCaptureClient,
            0xc8adbd64,
            0xe71e,
            0x48a0,
            0xa4,
            0xde,
            0x18,
            0x5c,
            0x39,
            0x5c,
            0xd3,
            0x17);

DEFINE_GUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT,
            0x00000003,
            0x0000,
            0x0010,
            0x80,
            0x00,
            0x00,
            0xaa,
            0x00,
            0x38,
            0x9b,
            0x71);

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(x)                                                       \
    do {                                                                      \
        if ((x) != NULL) {                                                    \
            IUnknown_Release((IUnknown *)(x));                                \
            (x) = NULL;                                                       \
        }                                                                     \
    } while (0)
#endif

static void audio_log_hresult(const char *context, HRESULT hr)
{
    fprintf(stderr, "[audio] %s failed (HRESULT=0x%08lX)\n", context, (long)hr);
}

static bool audio_mci_command(const char *command, const char *context)
{
    MCIERROR err = mciSendStringA(command, NULL, 0, NULL);
    if (err != 0U) {
        char buffer[256] = {0};
        if (mciGetErrorStringA(err, buffer, sizeof(buffer))) {
            fprintf(stderr, "[audio] %s failed: %s\n", context, buffer);
        } else {
            fprintf(stderr, "[audio] %s failed: MCI error %lu\n", context, (unsigned long)err);
        }
        return false;
    }
    return true;
}

typedef struct VoiceRenderBuffer {
    float *samples;
    size_t frame_count;
    size_t cursor;
    struct VoiceRenderBuffer *next;
} VoiceRenderBuffer;

typedef struct VoicePlaybackContext {
    IMMDevice *device;
    IAudioClient *client;
    IAudioRenderClient *render;
    HANDLE event;
    HANDLE thread;
    CRITICAL_SECTION lock;
    VoiceRenderBuffer *head;
    VoiceRenderBuffer *tail;
    WAVEFORMATEX *format;
    UINT32 buffer_frames;
    float *mix_buffer;
    LONG running;
    int lock_initialized;
} VoicePlaybackContext;

typedef struct VoiceCaptureContext {
    IMMDevice *device;
    IAudioClient *client;
    IAudioCaptureClient *capture;
    HANDLE event;
    HANDLE thread;
    CRITICAL_SECTION lock;
    int16_t *ring;
    size_t ring_capacity;
    size_t ring_write;
    size_t ring_read;
    float level_linear;
    float level_db;
    WAVEFORMATEX *format;
    LONG running;
    int lock_initialized;
} VoiceCaptureContext;

static VoicePlaybackContext g_voice_playback = {0};
static VoiceCaptureContext g_voice_capture = {0};

static BOOL audio_format_is_float(const WAVEFORMATEX *fmt)
{
    if (!fmt) {
        return FALSE;
    }
    if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return TRUE;
    }
    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE *ext = (const WAVEFORMATEXTENSIBLE *)fmt;
        return IsEqualGUID(&ext->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }
    return FALSE;
}

static UINT32 audio_format_bytes_per_sample(const WAVEFORMATEX *fmt)
{
    return fmt ? (fmt->wBitsPerSample / 8U) : 0U;
}

static size_t audio_format_byte_size(const WAVEFORMATEX *fmt)
{
    if (!fmt) {
        return 0U;
    }
    size_t size = sizeof(WAVEFORMATEX);
    if (fmt->cbSize > 0U || fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        size += fmt->cbSize;
    }
    return size;
}

static WAVEFORMATEX *audio_clone_format(const WAVEFORMATEX *fmt)
{
    size_t size = audio_format_byte_size(fmt);
    if (size == 0U) {
        return NULL;
    }
    WAVEFORMATEX *copy = (WAVEFORMATEX *)CoTaskMemAlloc(size);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, fmt, size);
    return copy;
}

static IMMDevice *audio_get_endpoint(uint32_t token, EDataFlow flow)
{
    if (!g_audio.device_enumerator) {
        return NULL;
    }

    IMMDevice *device = NULL;
    if (token == UINT32_MAX) {
        HRESULT hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(g_audio.device_enumerator, flow, eConsole, &device);
        if (FAILED(hr)) {
            audio_log_hresult("GetDefaultAudioEndpoint", hr);
            return NULL;
        }
        return device;
    }

    IMMDeviceCollection *collection = NULL;
    HRESULT hr = IMMDeviceEnumerator_EnumAudioEndpoints(g_audio.device_enumerator, flow, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) {
        audio_log_hresult("EnumAudioEndpoints", hr);
        return NULL;
    }

    UINT32 count = 0;
    IMMDeviceCollection_GetCount(collection, &count);
    if (token >= count) {
        SAFE_RELEASE(collection);
        return NULL;
    }

    hr = IMMDeviceCollection_Item(collection, token, &device);
    if (FAILED(hr)) {
        audio_log_hresult("IMMDeviceCollection::Item", hr);
        device = NULL;
    }

    SAFE_RELEASE(collection);
    return device;
}

static size_t audio_enumerate_wasapi_devices(EDataFlow flow, AudioDeviceInfo *out_devices, size_t max_devices)
{
    if (!g_audio.device_enumerator) {
        return 0;
    }

    size_t index = 0;
    if (out_devices && max_devices > 0U) {
        AudioDeviceInfo *info = &out_devices[index++];
        info->id = UINT32_MAX;
        info->is_default = true;
        info->is_input = (flow == eCapture);
        snprintf(info->name, sizeof(info->name), "System Default %s", info->is_input ? "Input" : "Output");
    }

    IMMDeviceCollection *collection = NULL;
    HRESULT hr = IMMDeviceEnumerator_EnumAudioEndpoints(g_audio.device_enumerator, flow, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) {
        audio_log_hresult("EnumAudioEndpoints", hr);
        return index;
    }

    UINT32 endpoint_count = 0;
    IMMDeviceCollection_GetCount(collection, &endpoint_count);
    for (UINT32 i = 0; i < endpoint_count; ++i) {
        IMMDevice *device = NULL;
        if (FAILED(IMMDeviceCollection_Item(collection, i, &device))) {
            continue;
        }

        IPropertyStore *props = NULL;
        if (SUCCEEDED(IMMDevice_OpenPropertyStore(device, STGM_READ, &props))) {
            PROPVARIANT name;
            PropVariantInit(&name);
            if (SUCCEEDED(IPropertyStore_GetValue(props, &PKEY_Device_FriendlyName, &name))) {
                if (out_devices && index < max_devices) {
                    AudioDeviceInfo *info = &out_devices[index];
                    info->id = i;
                    info->is_default = false;
                    info->is_input = (flow == eCapture);
                    if (name.vt == VT_LPWSTR && name.pwszVal) {
                        wcstombs(info->name, name.pwszVal, sizeof(info->name) - 1U);
                        info->name[sizeof(info->name) - 1U] = '\0';
                    } else {
                        snprintf(info->name, sizeof(info->name), "Device %u", i + 1U);
                    }
                }
                ++index;
            }
            PropVariantClear(&name);
            SAFE_RELEASE(props);
        }

        SAFE_RELEASE(device);
    }

    SAFE_RELEASE(collection);
    return index;
}

static void voice_render_queue_clear(void)
{
    if (!g_voice_playback.lock_initialized) {
        g_voice_playback.head = NULL;
        g_voice_playback.tail = NULL;
        return;
    }

    EnterCriticalSection(&g_voice_playback.lock);
    VoiceRenderBuffer *node = g_voice_playback.head;
    while (node) {
        VoiceRenderBuffer *next = node->next;
        free(node->samples);
        free(node);
        node = next;
    }
    g_voice_playback.head = g_voice_playback.tail = NULL;
    LeaveCriticalSection(&g_voice_playback.lock);
}

static void voice_render_queue_push(VoiceRenderBuffer *buffer)
{
    if (!buffer) {
        return;
    }
    if (!g_voice_playback.lock_initialized) {
        free(buffer->samples);
        free(buffer);
        return;
    }
    EnterCriticalSection(&g_voice_playback.lock);
    VoiceRenderBuffer *node = g_voice_playback.head;
    size_t count = 0;
    while (node) {
        ++count;
        node = node->next;
    }
    while (count >= VOICE_QUEUE_LIMIT && g_voice_playback.head) {
        VoiceRenderBuffer *oldest = g_voice_playback.head;
        g_voice_playback.head = oldest->next;
        if (!g_voice_playback.head) {
            g_voice_playback.tail = NULL;
        }
        free(oldest->samples);
        free(oldest);
        --count;
    }
    if (!g_voice_playback.head) {
        g_voice_playback.head = g_voice_playback.tail = buffer;
    } else {
        g_voice_playback.tail->next = buffer;
        g_voice_playback.tail = buffer;
    }
    LeaveCriticalSection(&g_voice_playback.lock);
}

static VoiceRenderBuffer *voice_render_queue_peek(void)
{
    if (!g_voice_playback.lock_initialized) {
        return NULL;
    }
    return g_voice_playback.head;
}

static void voice_render_queue_pop(void)
{
    if (!g_voice_playback.lock_initialized) {
        return;
    }
    VoiceRenderBuffer *node = g_voice_playback.head;
    if (!node) {
        return;
    }
    g_voice_playback.head = node->next;
    if (!g_voice_playback.head) {
        g_voice_playback.tail = NULL;
    }
    free(node->samples);
    free(node);
}

static VoiceRenderBuffer *voice_create_buffer(const AudioVoiceFrame *frame, const WAVEFORMATEX *fmt)
{
    if (!frame || !frame->samples || frame->sample_count == 0U || !fmt) {
        return NULL;
    }

    const uint32_t dst_rate = fmt->nSamplesPerSec;
    const uint32_t src_rate = frame->sample_rate ? frame->sample_rate : dst_rate;
    const uint8_t dst_channels = (uint8_t)fmt->nChannels;
    const uint8_t src_channels = frame->channels ? frame->channels : 1U;

    double ratio = (double)dst_rate / (double)src_rate;
    size_t dst_frames = (size_t)((double)frame->sample_count * ratio) + 1U;
    if (dst_frames == 0U) {
        dst_frames = 1U;
    }

    VoiceRenderBuffer *buffer = (VoiceRenderBuffer *)calloc(1, sizeof(VoiceRenderBuffer));
    if (!buffer) {
        return NULL;
    }
    buffer->samples = (float *)calloc(dst_frames * dst_channels, sizeof(float));
    if (!buffer->samples) {
        free(buffer);
        return NULL;
    }

    const float scale = audio_clamp(g_audio.master_volume, 0.0f, 1.0f) *
                        audio_clamp(g_audio.voice_volume, 0.0f, 1.0f) /
                        32768.0f;

    double src_pos = 0.0;
    for (size_t i = 0; i < dst_frames; ++i) {
        size_t src_index = (size_t)src_pos;
        if (src_index >= frame->sample_count) {
            src_index = frame->sample_count - 1U;
        }
        size_t src_next = (src_index + 1U < frame->sample_count) ? (src_index + 1U) : src_index;
        double frac = src_pos - (double)src_index;

        for (uint8_t ch = 0; ch < dst_channels; ++ch) {
            uint8_t src_ch = (ch < src_channels) ? ch : 0;
            int16_t s0 = frame->samples[src_index * src_channels + src_ch];
            int16_t s1 = frame->samples[src_next * src_channels + src_ch];
            float interp = (float)s0 + (float)(s1 - s0) * (float)frac;
            buffer->samples[i * dst_channels + ch] = interp * scale;
        }

        src_pos += 1.0 / ratio;
    }

    buffer->frame_count = dst_frames;
    buffer->cursor = 0;
    buffer->next = NULL;
    return buffer;
}

static void voice_mix_output(BYTE *dest, UINT32 frames)
{
    if (!dest || !g_voice_playback.format || frames == 0U) {
        return;
    }
    if (!g_voice_playback.lock_initialized) {
        memset(dest, 0, (size_t)frames * g_voice_playback.format->nChannels * audio_format_bytes_per_sample(g_voice_playback.format));
        return;
    }

    const UINT32 channels = g_voice_playback.format->nChannels;
    const BOOL float_format = audio_format_is_float(g_voice_playback.format);
    const UINT32 bytes_per_sample = audio_format_bytes_per_sample(g_voice_playback.format);
    const size_t total_samples = (size_t)frames * (size_t)channels;

    memset(g_voice_playback.mix_buffer, 0, total_samples * sizeof(float));

    EnterCriticalSection(&g_voice_playback.lock);
    VoiceRenderBuffer *node = g_voice_playback.head;
    size_t frame_index = 0;
    while (node && frame_index < frames) {
        size_t available = node->frame_count - node->cursor;
        size_t to_copy = available;
        if (to_copy > (frames - frame_index)) {
            to_copy = frames - frame_index;
        }

        for (size_t i = 0; i < to_copy; ++i) {
            for (UINT32 ch = 0; ch < channels; ++ch) {
                size_t dst = (frame_index + i) * channels + ch;
                size_t src = (node->cursor + i) * channels + ch;
                g_voice_playback.mix_buffer[dst] += node->samples[src];
            }
        }

        node->cursor += to_copy;
        frame_index += to_copy;
        if (node->cursor >= node->frame_count) {
            VoiceRenderBuffer *next = node->next;
            free(node->samples);
            free(node);
            node = next;
            g_voice_playback.head = node;
            if (!node) {
                g_voice_playback.tail = NULL;
            }
        } else {
            break;
        }
    }
    LeaveCriticalSection(&g_voice_playback.lock);

    for (size_t i = 0; i < total_samples; ++i) {
        if (g_voice_playback.mix_buffer[i] > 1.0f) {
            g_voice_playback.mix_buffer[i] = 1.0f;
        } else if (g_voice_playback.mix_buffer[i] < -1.0f) {
            g_voice_playback.mix_buffer[i] = -1.0f;
        }
    }

    if (float_format) {
        memcpy(dest, g_voice_playback.mix_buffer, total_samples * sizeof(float));
    } else if (bytes_per_sample == 2U) {
        int16_t *dst = (int16_t *)dest;
        for (size_t i = 0; i < total_samples; ++i) {
            float sample = g_voice_playback.mix_buffer[i] * 32767.0f;
            if (sample > 32767.0f) {
                sample = 32767.0f;
            } else if (sample < -32768.0f) {
                sample = -32768.0f;
            }
            dst[i] = (int16_t)sample;
        }
    } else {
        memset(dest, 0, total_samples * bytes_per_sample);
    }
}

static DWORD WINAPI voice_render_thread(LPVOID unused)
{
    (void)unused;
    DWORD task_index = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsA("Pro Audio", &task_index);

    g_voice_playback.running = TRUE;
    while (g_voice_playback.running) {
        DWORD wait = WaitForSingleObject(g_voice_playback.event, INFINITE);
        if (wait != WAIT_OBJECT_0) {
            continue;
        }
        if (!g_voice_playback.running) {
            break;
        }

        UINT32 padding = 0;
        if (FAILED(IAudioClient_GetCurrentPadding(g_voice_playback.client, &padding))) {
            continue;
        }
        if (padding >= g_voice_playback.buffer_frames) {
            continue;
        }

        UINT32 frames_to_write = g_voice_playback.buffer_frames - padding;
        BYTE *data = NULL;
        if (FAILED(IAudioRenderClient_GetBuffer(g_voice_playback.render, frames_to_write, &data))) {
            continue;
        }

        voice_mix_output(data, frames_to_write);
        IAudioRenderClient_ReleaseBuffer(g_voice_playback.render, frames_to_write, 0);
    }

    if (mmcss) {
        AvRevertMmThreadCharacteristics(mmcss);
    }
    return 0;
}

static void voice_playback_shutdown(void)
{
    g_voice_playback.running = FALSE;
    if (g_voice_playback.event) {
        SetEvent(g_voice_playback.event);
    }
    if (g_voice_playback.thread) {
        WaitForSingleObject(g_voice_playback.thread, INFINITE);
        CloseHandle(g_voice_playback.thread);
        g_voice_playback.thread = NULL;
    }

    voice_render_queue_clear();
    if (g_voice_playback.lock_initialized) {
        DeleteCriticalSection(&g_voice_playback.lock);
        g_voice_playback.lock_initialized = 0;
    }

    SAFE_RELEASE(g_voice_playback.render);
    SAFE_RELEASE(g_voice_playback.client);
    SAFE_RELEASE(g_voice_playback.device);

    if (g_voice_playback.event) {
        CloseHandle(g_voice_playback.event);
        g_voice_playback.event = NULL;
    }
    if (g_voice_playback.format) {
        CoTaskMemFree(g_voice_playback.format);
        g_voice_playback.format = NULL;
    }
    free(g_voice_playback.mix_buffer);
    g_voice_playback.mix_buffer = NULL;
}

static bool voice_playback_start(uint32_t token)
{
    voice_playback_shutdown();

    g_voice_playback.device = audio_get_endpoint(token, eRender);
    if (!g_voice_playback.device) {
        return false;
    }

    HRESULT hr = IMMDevice_Activate(g_voice_playback.device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&g_voice_playback.client);
    if (FAILED(hr)) {
        audio_log_hresult("IMMDevice::Activate(render)", hr);
        voice_playback_shutdown();
        return false;
    }

    WAVEFORMATEX request = {0};
    request.wFormatTag = WAVE_FORMAT_PCM;
    request.nChannels = VOICE_CHANNELS;
    request.nSamplesPerSec = VOICE_TARGET_RATE;
    request.wBitsPerSample = 16;
    request.nBlockAlign = (request.nChannels * request.wBitsPerSample) / 8U;
    request.nAvgBytesPerSec = request.nSamplesPerSec * request.nBlockAlign;
    request.cbSize = 0;

    WAVEFORMATEX *closest = NULL;
    hr = IAudioClient_IsFormatSupported(g_voice_playback.client, AUDCLNT_SHAREMODE_SHARED, &request, &closest);
    if (hr == S_OK) {
        g_voice_playback.format = audio_clone_format(&request);
        if (!g_voice_playback.format) {
            voice_playback_shutdown();
            return false;
        }
    } else if (hr == S_FALSE && closest != NULL) {
        g_voice_playback.format = audio_clone_format(closest);
        if (!g_voice_playback.format) {
            voice_playback_shutdown();
            CoTaskMemFree(closest);
            return false;
        }
    } else {
        hr = IAudioClient_GetMixFormat(g_voice_playback.client, &g_voice_playback.format);
        if (FAILED(hr)) {
            audio_log_hresult("IAudioClient::GetMixFormat(render)", hr);
            voice_playback_shutdown();
            return false;
        }
    }
    if (closest) {
        CoTaskMemFree(closest);
        closest = NULL;
    }

    g_voice_playback.event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!g_voice_playback.event) {
        audio_log("Failed to create render event");
        voice_playback_shutdown();
        return false;
    }

    REFERENCE_TIME duration = 20 * 10000; /* 20 ms */
    hr = IAudioClient_Initialize(g_voice_playback.client,
                                 AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                 duration,
                                 0,
                                 g_voice_playback.format,
                                 NULL);
    if (FAILED(hr)) {
        audio_log_hresult("IAudioClient::Initialize(render)", hr);
        voice_playback_shutdown();
        return false;
    }

    hr = IAudioClient_SetEventHandle(g_voice_playback.client, g_voice_playback.event);
    if (FAILED(hr)) {
        audio_log_hresult("IAudioClient::SetEventHandle", hr);
        voice_playback_shutdown();
        return false;
    }

    hr = IAudioClient_GetBufferSize(g_voice_playback.client, &g_voice_playback.buffer_frames);
    if (FAILED(hr)) {
        audio_log_hresult("IAudioClient::GetBufferSize", hr);
        voice_playback_shutdown();
        return false;
    }

    hr = IAudioClient_GetService(g_voice_playback.client, &IID_IAudioRenderClient, (void **)&g_voice_playback.render);
    if (FAILED(hr)) {
        audio_log_hresult("IAudioClient::GetService(render)", hr);
        voice_playback_shutdown();
        return false;
    }

    g_voice_playback.mix_buffer = (float *)calloc(g_voice_playback.buffer_frames * g_voice_playback.format->nChannels, sizeof(float));
    if (!g_voice_playback.mix_buffer) {
        audio_log("Failed to allocate mix buffer");
        voice_playback_shutdown();
        return false;
    }

    InitializeCriticalSection(&g_voice_playback.lock);
    g_voice_playback.lock_initialized = 1;
    g_voice_playback.head = g_voice_playback.tail = NULL;
    g_voice_playback.running = TRUE;

    g_voice_playback.thread = CreateThread(NULL, 0, voice_render_thread, NULL, 0, NULL);
    if (!g_voice_playback.thread) {
        audio_log("Failed to create render thread");
        voice_playback_shutdown();
        return false;
    }

    hr = IAudioClient_Start(g_voice_playback.client);
    if (FAILED(hr)) {
        audio_log_hresult("IAudioClient::Start(render)", hr);
        voice_playback_shutdown();
        return false;
    }

    return true;
}

static DWORD WINAPI voice_capture_thread(LPVOID unused)
{
    (void)unused;
    DWORD task_index = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsA("Audio", &task_index);

    g_voice_capture.running = TRUE;
    const BOOL float_format = audio_format_is_float(g_voice_capture.format);
    const UINT32 channels = g_voice_capture.format->nChannels;
    const UINT32 src_rate = g_voice_capture.format->nSamplesPerSec;

    while (g_voice_capture.running) {
        DWORD wait = WaitForSingleObject(g_voice_capture.event, INFINITE);
        if (wait != WAIT_OBJECT_0) {
            continue;
        }
        if (!g_voice_capture.running) {
            break;
        }

        UINT32 packet_frames = 0;
        HRESULT hr = IAudioCaptureClient_GetNextPacketSize(g_voice_capture.capture, &packet_frames);
        if (FAILED(hr)) {
            audio_log_hresult("IAudioCaptureClient::GetNextPacketSize", hr);
            continue;
        }

        while (packet_frames > 0) {
            BYTE *data = NULL;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = IAudioCaptureClient_GetBuffer(g_voice_capture.capture, &data, &frames, &flags, NULL, NULL);
            if (FAILED(hr)) {
                audio_log_hresult("IAudioCaptureClient::GetBuffer", hr);
                break;
            }

            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
                double ratio = (double)VOICE_TARGET_RATE / (double)src_rate;
                size_t dst_frames = (size_t)((double)frames * ratio) + 1U;
                int16_t *converted = (int16_t *)malloc(dst_frames * sizeof(int16_t));
                if (converted) {
                    double src_pos = 0.0;
                    float level_accum = 0.0f;
                    size_t level_samples = 0U;

                    for (size_t i = 0; i < dst_frames; ++i) {
                        size_t src_index = (size_t)src_pos;
                        if (src_index >= frames) {
                            src_index = frames - 1U;
                        }
                        size_t src_next = (src_index + 1U < frames) ? (src_index + 1U) : src_index;
                        double frac = src_pos - (double)src_index;

                        const BYTE *frame0 = data + src_index * g_voice_capture.format->nBlockAlign;
                        const BYTE *frame1 = data + src_next * g_voice_capture.format->nBlockAlign;

                        float sample0 = 0.0f;
                        float sample1 = 0.0f;
                        if (float_format) {
                            const float *f0 = (const float *)frame0;
                            const float *f1 = (const float *)frame1;
                            sample0 = f0[0];
                            sample1 = f1[0];
                            if (channels > 1U) {
                                for (UINT32 ch = 1; ch < channels; ++ch) {
                                    sample0 += f0[ch];
                                    sample1 += f1[ch];
                                }
                                sample0 /= (float)channels;
                                sample1 /= (float)channels;
                            }
                        } else {
                            const int16_t *i0 = (const int16_t *)frame0;
                            const int16_t *i1 = (const int16_t *)frame1;
                            sample0 = (float)i0[0] / 32768.0f;
                            sample1 = (float)i1[0] / 32768.0f;
                            if (channels > 1U) {
                                for (UINT32 ch = 1; ch < channels; ++ch) {
                                    sample0 += (float)i0[ch] / 32768.0f;
                                    sample1 += (float)i1[ch] / 32768.0f;
                                }
                                sample0 /= (float)channels;
                                sample1 /= (float)channels;
                            }
                        }

                        float sample = sample0 + (sample1 - sample0) * (float)frac;
                        if (sample > 1.0f) {
                            sample = 1.0f;
                        } else if (sample < -1.0f) {
                            sample = -1.0f;
                        }
                        converted[i] = (int16_t)(sample * 32767.0f);
                        level_accum += sample * sample;
                        ++level_samples;
                        src_pos += 1.0 / ratio;
                    }

                    EnterCriticalSection(&g_voice_capture.lock);
                    for (size_t i = 0; i < dst_frames; ++i) {
                        g_voice_capture.ring[g_voice_capture.ring_write] = converted[i];
                        g_voice_capture.ring_write = (g_voice_capture.ring_write + 1U) % g_voice_capture.ring_capacity;
                        if (g_voice_capture.ring_write == g_voice_capture.ring_read) {
                            g_voice_capture.ring_read = (g_voice_capture.ring_read + 1U) % g_voice_capture.ring_capacity;
                        }
                    }
                    if (level_samples > 0U) {
                        float rms = sqrtf(level_accum / (float)level_samples);
                        g_voice_capture.level_linear = (g_voice_capture.level_linear * 0.85f) + (rms * 0.15f);
                        g_voice_capture.level_db = (g_voice_capture.level_linear > 0.000001f)
                                                       ? 20.0f * log10f(g_voice_capture.level_linear)
                                                       : -120.0f;
                    }
                    LeaveCriticalSection(&g_voice_capture.lock);
                    free(converted);
                }
            }

            IAudioCaptureClient_ReleaseBuffer(g_voice_capture.capture, frames);
            hr = IAudioCaptureClient_GetNextPacketSize(g_voice_capture.capture, &packet_frames);
            if (FAILED(hr)) {
                audio_log_hresult("IAudioCaptureClient::GetNextPacketSize", hr);
                break;
            }
        }
    }

    if (mmcss) {
        AvRevertMmThreadCharacteristics(mmcss);
    }
    return 0;
}

static void voice_capture_shutdown(void)
{
    g_voice_capture.running = FALSE;
    if (g_voice_capture.event) {
        SetEvent(g_voice_capture.event);
    }
    if (g_voice_capture.thread) {
        WaitForSingleObject(g_voice_capture.thread, INFINITE);
        CloseHandle(g_voice_capture.thread);
        g_voice_capture.thread = NULL;
    }

    if (g_voice_capture.lock_initialized) {
    if (g_voice_capture.lock_initialized) {
        DeleteCriticalSection(&g_voice_capture.lock);
        g_voice_capture.lock_initialized = 0;
    }
        g_voice_capture.lock_initialized = 0;
    }

    SAFE_RELEASE(g_voice_capture.capture);
    SAFE_RELEASE(g_voice_capture.client);
    SAFE_RELEASE(g_voice_capture.device);

    if (g_voice_capture.event) {
        CloseHandle(g_voice_capture.event);
        g_voice_capture.event = NULL;
    }
    if (g_voice_capture.format) {
        CoTaskMemFree(g_voice_capture.format);
        g_voice_capture.format = NULL;
    }

    free(g_voice_capture.ring);
    g_voice_capture.ring = NULL;
    g_voice_capture.ring_capacity = 0;
    g_voice_capture.ring_read = 0;
    g_voice_capture.ring_write = 0;
    g_voice_capture.level_linear = 0.0f;
    g_voice_capture.level_db = -120.0f;
}

static bool voice_capture_start(uint32_t token)
{
    voice_capture_shutdown();

    g_voice_capture.device = audio_get_endpoint(token, eCapture);
    if (!g_voice_capture.device) {
        return false;
    }

    HRESULT hr = IMMDevice_Activate(g_voice_capture.device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&g_voice_capture.client);
    if (FAILED(hr)) {
        audio_log_hresult("IMMDevice::Activate(capture)", hr);
        voice_capture_shutdown();
        return false;
    }

    WAVEFORMATEX request = {0};
    request.wFormatTag = WAVE_FORMAT_PCM;
    request.nChannels = VOICE_CHANNELS;
    request.nSamplesPerSec = VOICE_TARGET_RATE;
    request.wBitsPerSample = 16;
    request.nBlockAlign = (request.nChannels * request.wBitsPerSample) / 8U;
    request.nAvgBytesPerSec = request.nSamplesPerSec * request.nBlockAlign;
    request.cbSize = 0;

    WAVEFORMATEX *closest = NULL;
    hr = IAudioClient_IsFormatSupported(g_voice_capture.client, AUDCLNT_SHAREMODE_SHARED, &request, &closest);
    if (hr == S_OK) {
        g_voice_capture.format = audio_clone_format(&request);
        if (!g_voice_capture.format) {
            voice_capture_shutdown();
            return false;
        }
    } else if (hr == S_FALSE && closest != NULL) {
        g_voice_capture.format = audio_clone_format(closest);
        if (!g_voice_capture.format) {
            voice_capture_shutdown();
            CoTaskMemFree(closest);
            return false;
        }
    } else {
        hr = IAudioClient_GetMixFormat(g_voice_capture.client, &g_voice_capture.format);
        if (FAILED(hr)) {
            audio_log_hresult("IAudioClient::GetMixFormat(capture)", hr);
            voice_capture_shutdown();
            return false;
        }
    }
    if (closest) {
        CoTaskMemFree(closest);
        closest = NULL;
    }

    g_voice_capture.event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!g_voice_capture.event) {
        audio_log("Failed to create capture event");
        voice_capture_shutdown();
        return false;
    }

    REFERENCE_TIME duration = 20 * 10000;
    hr = IAudioClient_Initialize(g_voice_capture.client,
                                 AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                 duration,
                                 0,
                                 g_voice_capture.format,
                                 NULL);
    if (FAILED(hr)) {
        audio_log_hresult("IAudioClient::Initialize(capture)", hr);
        voice_capture_shutdown();
        return false;
    }

    hr = IAudioClient_SetEventHandle(g_voice_capture.client, g_voice_capture.event);
    if (FAILED(hr)) {
        audio_log_hresult("IAudioClient::SetEventHandle(capture)", hr);
        voice_capture_shutdown();
        return false;
    }

    hr = IAudioClient_GetService(g_voice_capture.client, &IID_IAudioCaptureClient, (void **)&g_voice_capture.capture);
    if (FAILED(hr)) {
        audio_log_hresult("IAudioClient::GetService(capture)", hr);
        voice_capture_shutdown();
        return false;
    }

    g_voice_capture.ring_capacity = VOICE_RING_FRAMES;
    g_voice_capture.ring = (int16_t *)calloc(g_voice_capture.ring_capacity, sizeof(int16_t));
    if (!g_voice_capture.ring) {
        voice_capture_shutdown();
        return false;
    }

    InitializeCriticalSection(&g_voice_capture.lock);
    g_voice_capture.lock_initialized = 1;
    g_voice_capture.ring_read = g_voice_capture.ring_write = 0;
    g_voice_capture.level_linear = 0.0f;
    g_voice_capture.level_db = -120.0f;

    g_voice_capture.running = TRUE;
    g_voice_capture.thread = CreateThread(NULL, 0, voice_capture_thread, NULL, 0, NULL);
    if (!g_voice_capture.thread) {
        voice_capture_shutdown();
        return false;
    }

    hr = IAudioClient_Start(g_voice_capture.client);
    if (FAILED(hr)) {
        audio_log_hresult("IAudioClient::Start(capture)", hr);
        voice_capture_shutdown();
        return false;
    }

    return true;
}

#endif /* defined(_WIN32) */

/* Public API */

bool audio_init(void)
{
    if (g_audio.initialized) {
        return true;
    }

#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        audio_log_hresult("CoInitializeEx", hr);
        return false;
    }

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator,
                          NULL,
                          CLSCTX_ALL,
                          &IID_IMMDeviceEnumerator,
                          (void **)&g_audio.device_enumerator);
    if (FAILED(hr)) {
        audio_log_hresult("CoCreateInstance(MMDeviceEnumerator)", hr);
        CoUninitialize();
        return false;
    }

    if (!voice_playback_start(g_audio.output_device_token)) {
        audio_log("Voice playback initialization failed");
    }
    if (!voice_capture_start(g_audio.input_device_token)) {
        audio_log("Microphone capture initialization failed");
    }
#endif

    g_audio.master_volume = 1.0f;
    g_audio.music_volume = 1.0f;
    g_audio.effects_volume = 1.0f;
    g_audio.voice_volume = 1.0f;
    g_audio.microphone_volume = 1.0f;
    g_audio.music_configured = 0;
    g_audio.music_playing = 0;
    g_audio.music_loop = 0;
    g_audio.music_track[0] = '\0';
    g_audio.initialized = 1;
    return true;
}

void audio_shutdown(void)
{
    audio_music_stop();
#if defined(_WIN32)
    voice_playback_shutdown();
    voice_capture_shutdown();
    SAFE_RELEASE(g_audio.device_enumerator);
    CoUninitialize();
    memset(&g_voice_playback, 0, sizeof(g_voice_playback));
    memset(&g_voice_capture, 0, sizeof(g_voice_capture));
#endif

    memset(&g_audio, 0, sizeof(g_audio));
}

void audio_set_master_volume(float volume)
{
    g_audio.master_volume = audio_clamp(volume, 0.0f, 1.0f);
}

float audio_master_volume(void)
{
    return g_audio.master_volume;
}

bool audio_music_set_track(const char *path)
{
#if defined(_WIN32)
    if (!path || !path[0]) {
        return false;
    }

    char full_path[AUDIO_MAX_PATH] = {0};
    if (!_fullpath(full_path, path, sizeof(full_path))) {
        strncpy(full_path, path, sizeof(full_path) - 1U);
        full_path[sizeof(full_path) - 1U] = '\0';
    }

    if (_access(full_path, 0) != 0) {
        fprintf(stderr, "[audio] cannot access music file: %s\n", full_path);
        g_audio.music_configured = 0;
        g_audio.music_track[0] = '\0';
        return false;
    }

    strncpy(g_audio.music_track, full_path, sizeof(g_audio.music_track) - 1U);
    g_audio.music_track[sizeof(g_audio.music_track) - 1U] = '\0';
    g_audio.music_configured = 1;
    return true;
#else
    (void)path;
    return false;
#endif
}

bool audio_music_play(float volume, bool loop)
{
#if defined(_WIN32)
    if (!g_audio.music_configured) {
        return false;
    }

    audio_music_stop();

    char command[1024];
    snprintf(command, sizeof(command), "open \"%s\" alias " AUDIO_ALIAS_MUSIC, g_audio.music_track);
    if (!audio_mci_command(command, "open music")) {
        return false;
    }

    g_audio.music_loop = loop ? 1 : 0;
    g_audio.music_volume = audio_clamp(volume, 0.0f, 1.0f);
    g_audio.music_playing = 1;
    audio_music_set_volume(g_audio.music_volume);

    snprintf(command,
             sizeof(command),
             loop ? "play " AUDIO_ALIAS_MUSIC " repeat" : "play " AUDIO_ALIAS_MUSIC);
    if (!audio_mci_command(command, "play music")) {
        audio_mci_command("close " AUDIO_ALIAS_MUSIC, "close music");
        g_audio.music_playing = 0;
        return false;
    }
    return true;
#else
    (void)volume;
    (void)loop;
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
#if defined(_WIN32)
    if (g_audio.music_playing) {
        float scaled = g_audio.music_volume * audio_clamp(g_audio.master_volume, 0.0f, 1.0f);
        unsigned int level = (unsigned int)(scaled * 1000.0f);
        if (level > 1000U) {
            level = 1000U;
        }
        char command[128];
        snprintf(command, sizeof(command), "setaudio " AUDIO_ALIAS_MUSIC " volume to %u", level);
        audio_mci_command(command, "set volume");
    }
#else
    (void)volume;
#endif
}

float audio_music_volume(void)
{
    return g_audio.music_volume;
}

void audio_set_effects_volume(float volume)
{
    g_audio.effects_volume = audio_clamp(volume, 0.0f, 1.0f);
}

float audio_effects_volume(void)
{
    return g_audio.effects_volume;
}

bool audio_effect_play_file(const char *path, float volume)
{
    (void)path;
    (void)volume;
    return false;
}

void audio_set_voice_volume(float volume)
{
    g_audio.voice_volume = audio_clamp(volume, 0.0f, 1.0f);
}

float audio_voice_volume(void)
{
    return g_audio.voice_volume;
}

void audio_set_microphone_volume(float volume)
{
    g_audio.microphone_volume = audio_clamp(volume, 0.0f, 1.0f);
}

float audio_microphone_volume(void)
{
    return g_audio.microphone_volume;
}

bool audio_voice_submit(uint8_t speaker_id, const AudioVoiceFrame *frame)
{
    (void)speaker_id;
#if defined(_WIN32)
    if (!frame || !frame->samples || frame->sample_count == 0U) {
        return false;
    }
    if (!g_voice_playback.client || !g_voice_playback.format) {
        return false;
    }

    VoiceRenderBuffer *buffer = voice_create_buffer(frame, g_voice_playback.format);
    if (!buffer) {
        return false;
    }
    if (!g_voice_playback.lock_initialized) {
        free(buffer->samples);
        free(buffer);
        return false;
    }
    voice_render_queue_push(buffer);
    return true;
#else
    (void)frame;
    return false;
#endif
}

void audio_voice_stop(uint8_t speaker_id)
{
    (void)speaker_id;
    audio_voice_stop_all();
}

void audio_voice_stop_all(void)
{
#if defined(_WIN32)
    voice_render_queue_clear();
#endif
}

bool audio_microphone_start(void)
{
#if defined(_WIN32)
    return voice_capture_start(g_audio.input_device_token);
#else
    return false;
#endif
}

void audio_microphone_stop(void)
{
#if defined(_WIN32)
    voice_capture_shutdown();
#endif
}

bool audio_microphone_active(void)
{
#if defined(_WIN32)
    return g_voice_capture.running != 0;
#else
    return false;
#endif
}

size_t audio_microphone_read(int16_t *out_samples, size_t max_samples)
{
    if (!out_samples || max_samples == 0U) {
        return 0U;
    }
#if defined(_WIN32)
    if (!g_voice_capture.lock_initialized) {
        return 0U;
    }
    size_t copied = 0U;
    EnterCriticalSection(&g_voice_capture.lock);
    while (copied < max_samples && g_voice_capture.ring_read != g_voice_capture.ring_write) {
        out_samples[copied++] = g_voice_capture.ring[g_voice_capture.ring_read];
        g_voice_capture.ring_read = (g_voice_capture.ring_read + 1U) % g_voice_capture.ring_capacity;
    }
    LeaveCriticalSection(&g_voice_capture.lock);
    return copied;
#else
    return 0U;
#endif
}

uint32_t audio_microphone_sample_rate(void)
{
    return VOICE_TARGET_RATE;
}

uint8_t audio_microphone_channels(void)
{
    return VOICE_CHANNELS;
}

float audio_microphone_level(void)
{
#if defined(_WIN32)
    return g_voice_capture.level_linear;
#else
    return 0.0f;
#endif
}

float audio_microphone_level_db(void)
{
#if defined(_WIN32)
    return g_voice_capture.level_db;
#else
    return -120.0f;
#endif
}

bool audio_select_output_device(uint32_t device_id)
{
    g_audio.output_device_token = device_id;
#if defined(_WIN32)
    return voice_playback_start(device_id);
#else
    return false;
#endif
}

uint32_t audio_current_output_device(void)
{
    return g_audio.output_device_token;
}

size_t audio_enumerate_output_devices(AudioDeviceInfo *out_devices, size_t max_devices)
{
#if defined(_WIN32)
    return audio_enumerate_wasapi_devices(eRender, out_devices, max_devices);
#else
    (void)out_devices;
    (void)max_devices;
    return 0;
#endif
}

bool audio_select_input_device(uint32_t device_id)
{
    g_audio.input_device_token = device_id;
#if defined(_WIN32)
    return voice_capture_start(device_id);
#else
    return false;
#endif
}

uint32_t audio_current_input_device(void)
{
    return g_audio.input_device_token;
}

size_t audio_enumerate_input_devices(AudioDeviceInfo *out_devices, size_t max_devices)
{
#if defined(_WIN32)
    return audio_enumerate_wasapi_devices(eCapture, out_devices, max_devices);
#else
    (void)out_devices;
    (void)max_devices;
    return 0;
#endif
}
