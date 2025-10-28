#include "engine/renderer.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#if defined(_WIN32)
#    define COBJMACROS
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <wingdi.h>
#    include <objbase.h>
#    include <wincodec.h>
#endif
#include <GL/gl.h>

#ifndef GL_CLAMP_TO_EDGE
#    define GL_CLAMP_TO_EDGE 0x812F
#endif

struct Renderer {
    float clear_color[4];
    uint32_t viewport_width;
    uint32_t viewport_height;
    bool ui_active;
    bool font_ready;
    GLuint font_base;
    float font_line_height;
#if defined(_WIN32)
    bool logo_attempted;
    GLuint logo_texture;
    int logo_width;
    int logo_height;
#endif
};

static void renderer_apply_clear_color(const Renderer *renderer)
{
    if (!renderer) {
        return;
    }
    glClearColor(renderer->clear_color[0], renderer->clear_color[1], renderer->clear_color[2], renderer->clear_color[3]);
}

static void renderer_initialize_font(Renderer *renderer)
{
#if defined(_WIN32)
    if (!renderer || renderer->font_ready) {
        return;
    }

    HDC hdc = wglGetCurrentDC();
    if (!hdc) {
        return;
    }

    const int font_height = -18;
    HFONT font = CreateFontA(font_height,
                             0,
                             0,
                             0,
                             FW_NORMAL,
                             FALSE,
                             FALSE,
                             FALSE,
                             ANSI_CHARSET,
                             OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY,
                             FF_DONTCARE | DEFAULT_PITCH,
                             "Consolas");
    if (!font) {
        return;
    }

    HFONT old_font = (HFONT)SelectObject(hdc, font);

    GLuint base = glGenLists(96);
    if (base != 0) {
        if (wglUseFontBitmapsA(hdc, 32, 96, base)) {
            TEXTMETRICA metrics;
            if (GetTextMetricsA(hdc, &metrics)) {
                renderer->font_line_height = (float)metrics.tmHeight;
            } else {
                renderer->font_line_height = 18.0f;
            }
            renderer->font_base = base;
            renderer->font_ready = true;
        } else {
            glDeleteLists(base, 96);
        }
    }

    SelectObject(hdc, old_font);
    DeleteObject(font);
#else
    (void)renderer;
#endif
}

#if defined(_WIN32)
static bool renderer_load_logo(Renderer *renderer);
static void renderer_draw_ui_textured_rect(Renderer *renderer,
                                           GLuint texture,
                                           float x,
                                           float y,
                                           float width,
                                           float height);
#endif

Renderer *renderer_create(void)
{
    Renderer *renderer = (Renderer *)calloc(1, sizeof(Renderer));
    if (!renderer) {
        return NULL;
    }

    renderer->clear_color[0] = 0.05f;
    renderer->clear_color[1] = 0.08f;
    renderer->clear_color[2] = 0.12f;
    renderer->clear_color[3] = 1.0f;
    renderer->viewport_width = 1280U;
    renderer->viewport_height = 720U;
    renderer->ui_active = false;
    renderer->font_ready = false;
    renderer->font_base = 0;
    renderer->font_line_height = 18.0f;
#if defined(_WIN32)
    renderer->logo_attempted = false;
    renderer->logo_texture = 0;
    renderer->logo_width = 0;
    renderer->logo_height = 0;
#endif

    renderer_apply_clear_color(renderer);
    renderer_initialize_font(renderer);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glViewport(0, 0, (GLsizei)renderer->viewport_width, (GLsizei)renderer->viewport_height);

    return renderer;
}

void renderer_destroy(Renderer *renderer)
{
    if (!renderer) {
        return;
    }

#if defined(_WIN32)
    if (renderer->font_ready && renderer->font_base != 0) {
        glDeleteLists(renderer->font_base, 96);
        renderer->font_base = 0;
        renderer->font_ready = false;
    }
    if (renderer->logo_texture != 0) {
        glDeleteTextures(1, &renderer->logo_texture);
        renderer->logo_texture = 0;
    }
#endif

    free(renderer);
}

void renderer_set_clear_color(Renderer *renderer, float r, float g, float b, float a)
{
    if (!renderer) {
        return;
    }

    renderer->clear_color[0] = r;
    renderer->clear_color[1] = g;
    renderer->clear_color[2] = b;
    renderer->clear_color[3] = a;
    renderer_apply_clear_color(renderer);
}

void renderer_set_viewport(Renderer *renderer, uint32_t width, uint32_t height)
{
    if (!renderer) {
        return;
    }

    if (width == 0U) {
        width = 1U;
    }
    if (height == 0U) {
        height = 1U;
    }

    renderer->viewport_width = width;
    renderer->viewport_height = height;
    glViewport(0, 0, (GLsizei)width, (GLsizei)height);
}

uint32_t renderer_viewport_width(const Renderer *renderer)
{
    return renderer ? renderer->viewport_width : 0U;
}

uint32_t renderer_viewport_height(const Renderer *renderer)
{
    return renderer ? renderer->viewport_height : 0U;
}

void renderer_begin_scene(Renderer *renderer, const Camera *camera)
{
    if (!renderer || !camera) {
        return;
    }

    renderer->ui_active = false;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    renderer_apply_clear_color(renderer);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (renderer->viewport_width == 0U || renderer->viewport_height == 0U) {
        glViewport(0, 0, 1, 1);
    } else {
        glViewport(0, 0, (GLsizei)renderer->viewport_width, (GLsizei)renderer->viewport_height);
    }

    mat4 projection = camera_projection_matrix(camera);
    mat4 view = camera_view_matrix(camera);

    float projection_array[16];
    float view_array[16];
    mat4_to_float_array(&projection, projection_array);
    mat4_to_float_array(&view, view_array);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(projection_array);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view_array);
}

void renderer_draw_grid(Renderer *renderer, float half_extent, float spacing, float height)
{
    if (!renderer) {
        return;
    }

    if (spacing <= 0.0f) {
        spacing = 1.0f;
    }

    const int line_count = (int)((half_extent * 2.0f) / spacing) + 1;
    glDisable(GL_CULL_FACE);
    glBegin(GL_LINES);
    for (int i = -line_count; i <= line_count; ++i) {
        float x = (float)i * spacing;
        if (fabsf(x) > half_extent + 0.001f) {
            continue;
        }
        float intensity = (fabsf(x) < 0.001f) ? 0.55f : 0.25f;
        glColor3f(intensity, intensity, intensity);
        glVertex3f(x, height, -half_extent);
        glVertex3f(x, height, half_extent);
    }
    for (int i = -line_count; i <= line_count; ++i) {
        float z = (float)i * spacing;
        if (fabsf(z) > half_extent + 0.001f) {
            continue;
        }
        float intensity = (fabsf(z) < 0.001f) ? 0.55f : 0.25f;
        glColor3f(intensity, intensity, intensity);
        glVertex3f(-half_extent, height, z);
        glVertex3f(half_extent, height, z);
    }
    glEnd();
    glEnable(GL_CULL_FACE);
}

void renderer_draw_box(Renderer *renderer, vec3 center, vec3 half_extents, vec3 color)
{
    (void)renderer;

    const float hx = half_extents.x;
    const float hy = half_extents.y;
    const float hz = half_extents.z;

    const float x = center.x;
    const float y = center.y;
    const float z = center.z;

    glBegin(GL_QUADS);

    glColor3f(color.x * 0.9f, color.y * 0.9f, color.z * 0.9f);
    glVertex3f(x - hx, y - hy, z + hz);
    glVertex3f(x + hx, y - hy, z + hz);
    glVertex3f(x + hx, y + hy, z + hz);
    glVertex3f(x - hx, y + hy, z + hz);

    glColor3f(color.x * 0.8f, color.y * 0.8f, color.z * 0.8f);
    glVertex3f(x - hx, y - hy, z - hz);
    glVertex3f(x - hx, y + hy, z - hz);
    glVertex3f(x + hx, y + hy, z - hz);
    glVertex3f(x + hx, y - hy, z - hz);

    glColor3f(color.x * 1.1f, color.y * 1.1f, color.z * 1.1f);
    glVertex3f(x - hx, y + hy, z - hz);
    glVertex3f(x - hx, y + hy, z + hz);
    glVertex3f(x + hx, y + hy, z + hz);
    glVertex3f(x + hx, y + hy, z - hz);

    glColor3f(color.x * 0.7f, color.y * 0.7f, color.z * 0.7f);
    glVertex3f(x - hx, y - hy, z - hz);
    glVertex3f(x + hx, y - hy, z - hz);
    glVertex3f(x + hx, y - hy, z + hz);
    glVertex3f(x - hx, y - hy, z + hz);

    glColor3f(color.x * 0.85f, color.y * 0.85f, color.z * 0.85f);
    glVertex3f(x + hx, y - hy, z - hz);
    glVertex3f(x + hx, y + hy, z - hz);
    glVertex3f(x + hx, y + hy, z + hz);
    glVertex3f(x + hx, y - hy, z + hz);

    glColor3f(color.x * 0.85f, color.y * 0.85f, color.z * 0.85f);
    glVertex3f(x - hx, y - hy, z - hz);
    glVertex3f(x - hx, y - hy, z + hz);
    glVertex3f(x - hx, y + hy, z + hz);
    glVertex3f(x - hx, y + hy, z - hz);

    glEnd();
}

void renderer_draw_weapon_viewmodel(Renderer *renderer, float recoil_amount)
{
    if (!renderer) {
        return;
    }

    const float recoil_offset = recoil_amount * 0.015f;

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glTranslatef(0.35f, -0.35f - recoil_offset * 0.6f, -0.9f - recoil_offset);
    glRotatef(-6.0f, 0.0f, 1.0f, 0.0f);

    const float w = 0.4f;
    const float h = 0.3f;
    const float l = 0.7f;

    glBegin(GL_QUADS);
    glColor3f(0.25f, 0.25f, 0.30f);
    glVertex3f(-w, -h, 0.0f);
    glVertex3f(w, -h, 0.0f);
    glVertex3f(w, h, 0.0f);
    glVertex3f(-w, h, 0.0f);

    glColor3f(0.20f, 0.20f, 0.26f);
    glVertex3f(-w, -h, -l);
    glVertex3f(-w, h, -l);
    glVertex3f(w, h, -l);
    glVertex3f(w, -h, -l);

    glColor3f(0.18f, 0.18f, 0.22f);
    glVertex3f(-w, h, -l);
    glVertex3f(-w, h, 0.0f);
    glVertex3f(w, h, 0.0f);
    glVertex3f(w, h, -l);

    glColor3f(0.18f, 0.18f, 0.22f);
    glVertex3f(-w, -h, -l);
    glVertex3f(w, -h, -l);
    glVertex3f(w, -h, 0.0f);
    glVertex3f(-w, -h, 0.0f);

    glColor3f(0.22f, 0.22f, 0.28f);
    glVertex3f(w, -h, -l);
    glVertex3f(w, h, -l);
    glVertex3f(w, h, 0.0f);
    glVertex3f(w, -h, 0.0f);

    glVertex3f(-w, -h, -l);
    glVertex3f(-w, -h, 0.0f);
    glVertex3f(-w, h, 0.0f);
    glVertex3f(-w, h, -l);
    glEnd();

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glPopMatrix();
}

void renderer_draw_frame(Renderer *renderer)
{
    (void)renderer;
    glFlush();
}

void renderer_begin_ui(Renderer *renderer)
{
    if (!renderer || renderer->ui_active) {
        return;
    }

    renderer_initialize_font(renderer);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, renderer->viewport_width, renderer->viewport_height, 0.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    renderer->ui_active = true;
}

void renderer_draw_ui_text(Renderer *renderer, float x, float y, const char *text, float r, float g, float b, float a)
{
    if (!renderer || !renderer->font_ready || !renderer->ui_active || !text) {
        return;
    }

    glColor4f(r, g, b, a);

    float origin_x = x;
    float cursor_y = y;
    glRasterPos2f(origin_x, cursor_y);

    const unsigned char *ptr = (const unsigned char *)text;
    while (*ptr) {
        unsigned char c = *ptr++;
        if (c == '\n') {
            cursor_y += renderer->font_line_height;
            glRasterPos2f(origin_x, cursor_y);
            continue;
        }
        if (c < 32 || c >= 128) {
            continue;
        }
        glCallList(renderer->font_base + (c - 32));
    }
}
void renderer_draw_ui_rect(Renderer *renderer, float x, float y, float width, float height, float r, float g, float b, float a)
{
    (void)renderer;

    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + width, y);
    glVertex2f(x + width, y + height);
    glVertex2f(x, y + height);
    glEnd();
}

#if defined(_WIN32)
static void renderer_draw_ui_textured_rect(Renderer *renderer,
                                           GLuint texture,
                                           float x,
                                           float y,
                                           float width,
                                           float height)
{
    if (!renderer || !renderer->ui_active || texture == 0) {
        return;
    }

    GLboolean was_enabled = glIsEnabled(GL_TEXTURE_2D);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(x, y);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(x + width, y);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(x + width, y + height);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(x, y + height);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
    if (!was_enabled) {
        glDisable(GL_TEXTURE_2D);
    }
}
#endif

void renderer_draw_crosshair(Renderer *renderer, float center_x, float center_y, float size, float spread, float thickness)
{
    (void)renderer;
    if (size < 0.0f) {
        size = 0.0f;
    }
    if (thickness < 1.0f) {
        thickness = 1.0f;
    }

    glColor4f(0.95f, 0.95f, 0.95f, 0.9f);
    glLineWidth(thickness);
    glBegin(GL_LINES);
    glVertex2f(center_x - spread, center_y);
    glVertex2f(center_x - (spread + size), center_y);

    glVertex2f(center_x + spread, center_y);
    glVertex2f(center_x + spread + size, center_y);

    glVertex2f(center_x, center_y - spread);
    glVertex2f(center_x, center_y - (spread + size));

    glVertex2f(center_x, center_y + spread);
    glVertex2f(center_x, center_y + spread + size);
    glEnd();
    glLineWidth(1.0f);
}

void renderer_end_ui(Renderer *renderer)
{
    if (!renderer || !renderer->ui_active) {
        return;
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

    renderer->ui_active = false;
}

#if defined(_WIN32)
static bool renderer_load_logo(Renderer *renderer)
{
    if (!renderer) {
        return false;
    }
    if (renderer->logo_attempted) {
        return renderer->logo_texture != 0;
    }

    renderer->logo_attempted = true;

    HRESULT hr_init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool co_initialized = false;
    if (SUCCEEDED(hr_init)) {
        co_initialized = true;
    } else if (hr_init != RPC_E_CHANGED_MODE) {
        return false;
    }

    IWICImagingFactory *factory = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory,
                                  NULL,
                                  CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory,
                                  (LPVOID *)&factory);
    if (FAILED(hr) || !factory) {
        if (co_initialized) {
            CoUninitialize();
        }
        return false;
    }

    WCHAR path_w[MAX_PATH];
    const char *path = "assets/textures/WT-TB_2024_Logo.png";
    int count = MultiByteToWideChar(CP_UTF8, 0, path, -1, path_w, MAX_PATH);
    if (count <= 0) {
        IWICImagingFactory_Release(factory);
        if (co_initialized) {
            CoUninitialize();
        }
        return false;
    }

    IWICBitmapDecoder *decoder = NULL;
    hr = IWICImagingFactory_CreateDecoderFromFilename(factory,
                                                      path_w,
                                                      NULL,
                                                      GENERIC_READ,
                                                      WICDecodeMetadataCacheOnLoad,
                                                      &decoder);
    if (FAILED(hr) || !decoder) {
        IWICImagingFactory_Release(factory);
        if (co_initialized) {
            CoUninitialize();
        }
        return false;
    }

    IWICBitmapFrameDecode *frame = NULL;
    hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (FAILED(hr) || !frame) {
        IWICBitmapDecoder_Release(decoder);
        IWICImagingFactory_Release(factory);
        if (co_initialized) {
            CoUninitialize();
        }
        return false;
    }

    IWICFormatConverter *converter = NULL;
    hr = IWICImagingFactory_CreateFormatConverter(factory, &converter);
    if (FAILED(hr) || !converter) {
        IWICBitmapFrameDecode_Release(frame);
        IWICBitmapDecoder_Release(decoder);
        IWICImagingFactory_Release(factory);
        if (co_initialized) {
            CoUninitialize();
        }
        return false;
    }

    hr = IWICFormatConverter_Initialize(converter,
                                        (IWICBitmapSource *)frame,
                                        &GUID_WICPixelFormat32bppRGBA,
                                        WICBitmapDitherTypeNone,
                                        NULL,
                                        0.0f,
                                        WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        IWICFormatConverter_Release(converter);
        IWICBitmapFrameDecode_Release(frame);
        IWICBitmapDecoder_Release(decoder);
        IWICImagingFactory_Release(factory);
        if (co_initialized) {
            CoUninitialize();
        }
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    hr = IWICBitmapSource_GetSize((IWICBitmapSource *)converter, &width, &height);
    if (FAILED(hr) || width == 0 || height == 0) {
        IWICFormatConverter_Release(converter);
        IWICBitmapFrameDecode_Release(frame);
        IWICBitmapDecoder_Release(decoder);
        IWICImagingFactory_Release(factory);
        if (co_initialized) {
            CoUninitialize();
        }
        return false;
    }

    UINT stride = width * 4;
    UINT buffer_size = stride * height;
    BYTE *pixels = (BYTE *)malloc(buffer_size);
    if (!pixels) {
        IWICFormatConverter_Release(converter);
        IWICBitmapFrameDecode_Release(frame);
        IWICBitmapDecoder_Release(decoder);
        IWICImagingFactory_Release(factory);
        if (co_initialized) {
            CoUninitialize();
        }
        return false;
    }

    hr = IWICBitmapSource_CopyPixels((IWICBitmapSource *)converter, NULL, stride, buffer_size, pixels);
    if (FAILED(hr)) {
        free(pixels);
        IWICFormatConverter_Release(converter);
        IWICBitmapFrameDecode_Release(frame);
        IWICBitmapDecoder_Release(decoder);
        IWICImagingFactory_Release(factory);
        if (co_initialized) {
            CoUninitialize();
        }
        return false;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA,
                 (GLsizei)width,
                 (GLsizei)height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    free(pixels);
    IWICFormatConverter_Release(converter);
    IWICBitmapFrameDecode_Release(frame);
    IWICBitmapDecoder_Release(decoder);
    IWICImagingFactory_Release(factory);
    if (co_initialized) {
        CoUninitialize();
    }

    renderer->logo_texture = texture;
    renderer->logo_width = (int)width;
    renderer->logo_height = (int)height;
    return renderer->logo_texture != 0;
}

void renderer_draw_ui_logo(Renderer *renderer,
                           float center_x,
                           float center_y,
                           float max_width,
                           float max_height)
{
    if (!renderer || !renderer->ui_active) {
        return;
    }
    if (!renderer_load_logo(renderer)) {
        return;
    }

    if (renderer->logo_width <= 0 || renderer->logo_height <= 0) {
        return;
    }

    float aspect = (float)renderer->logo_width / (float)renderer->logo_height;
    float width = max_width;
    float height = width / aspect;
    if (height > max_height) {
        height = max_height;
        width = height * aspect;
    }
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }

    float x = center_x - width * 0.5f;
    float y = center_y - height * 0.5f;
    renderer_draw_ui_textured_rect(renderer, renderer->logo_texture, x, y, width, height);
}
#else
void renderer_draw_ui_logo(Renderer *renderer,
                           float center_x,
                           float center_y,
                           float max_width,
                           float max_height)
{
    (void)renderer;
    (void)center_x;
    (void)center_y;
    (void)max_width;
    (void)max_height;
}
#endif







