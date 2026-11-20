/* Sample content plugin for Divaverse (interface: Apps/Player/src/DvPlugin.h).
 * It offers two generated clips so the interface can be tried without any content, and it is the skeleton to start a
 * real plugin from: replace what scan / open / read / close do, keep their shape.
 *
 * Build (MSVC):   cl /LD /O2 sample_plugin.c /I ..\..\Apps\Player\src /Fe:SamplePlugin.dll
 * Build (mingw):  x86_64-w64-mingw32-gcc -O2 -shared -static -I ../../Apps/Player/src sample_plugin.c -o SamplePlugin.dll
 * Install:        copy the DLL into  <folder of Divaverse.exe>\Plugins\
 */
#include "DvPlugin.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const DvpHost* g_host;

typedef struct Clip { int kind, w, h; int64_t frames; } Clip;

static void scan(DvpEmit emit, void* user) {
    /* A real plugin walks its content here and reports every playable item. */
    DvpItem a = { L"bounce", L"Bounce", L"shapes", L"Sample shapes", 0, 4, 3.0 };
    DvpItem b = { L"pulse",  L"Pulse",  L"shapes", L"Sample shapes", 1, 0, 3.0 };
    emit(user, &a); emit(user, &b);
}

static void* open_(const wchar_t* id, DvpClipInfo* info) {
    Clip* c = (Clip*)calloc(1, sizeof(Clip)); if (!c) return NULL;
    c->kind = wcscmp(id, L"pulse") == 0; c->w = 320; c->h = 480; c->frames = 90;
    if (wcscmp(id, L"bounce") != 0 && !c->kind) { free(c); return NULL; }
    info->width = c->w; info->height = c->h; info->fps_num = 30; info->fps_den = 1; info->frames = c->frames;
    info->anchor_u = 0.5f; info->ground_v = 0.96f; info->pixels_per_metre = 0;
    return c;
}

/* One frame, BGRA, straight alpha, top row first. A real plugin decodes frame `frame` of its clip here. */
static int read_(void* clip, int64_t frame, uint8_t* bgra, int stride) {
    Clip* c = (Clip*)clip; float t = (float)(frame % c->frames) / (float)c->frames, ph = t * 6.2831853f;
    float cx = c->w * 0.5f, floor_ = c->h * 0.96f, rad = c->kind ? 70 + 30 * sinf(ph) : 70;
    float cy = c->kind ? floor_ - 150 : floor_ - rad - fabsf(sinf(ph)) * 220;
    for (int y = 0; y < c->h; ++y) {
        uint8_t* row = bgra + (size_t)y * stride;
        for (int x = 0; x < c->w; ++x) {
            float dx = x - cx, dy = y - cy, d = sqrtf(dx * dx + dy * dy), a = rad - d; a = a < 0 ? 0 : a > 1.5f ? 1 : a / 1.5f;     /* soft edge */
            float shade = 0.55f + 0.45f * (1 - d / rad) + 0.25f * (-dx - dy) / rad; if (shade < 0.2f) shade = 0.2f; if (shade > 1.2f) shade = 1.2f;
            float r = (c->kind ? 90 : 255) * shade, g = (c->kind ? 200 : 79) * shade, b = (c->kind ? 255 : 168) * shade;
            row[x * 4 + 0] = (uint8_t)(b > 255 ? 255 : b); row[x * 4 + 1] = (uint8_t)(g > 255 ? 255 : g); row[x * 4 + 2] = (uint8_t)(r > 255 ? 255 : r); row[x * 4 + 3] = (uint8_t)(a * 255);
        }
    }
    return 1;
}

static void close_(void* clip) { free(clip); }

static const DvpPlugin g_plugin = { DVP_ABI, L"sample", L"Sample shapes", L"1.0", scan, open_, read_, close_, NULL };

__declspec(dllexport) const DvpPlugin* DvPluginInit(const DvpHost* host) {
    if (!host || host->abi != DVP_ABI) return NULL;
    g_host = host; g_host->log("sample plugin ready");
    return &g_plugin;
}
