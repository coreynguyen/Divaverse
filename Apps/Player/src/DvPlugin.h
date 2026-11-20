/* Divaverse content plugin interface, version 1. Plain C, no other headers of Divaverse are needed.
 *
 * A plugin is a DLL in the "Plugins" folder beside Divaverse.exe that exports
 *
 *     const DvpPlugin* DvPluginInit(const DvpHost* host);
 *
 * It lets Divaverse list and play content that Divaverse itself cannot read. Divaverse knows nothing about what is
 * behind a plugin: remove the DLL and its items are simply gone. Nothing else in Divaverse depends on it.
 *
 * What a plugin provides
 *   scan   every playable item, with the performer ("group") it belongs to
 *   open   one item as a clip: size, frame rate, frame count, where the floor is
 *   read   one frame of an open clip as BGRA with STRAIGHT (not premultiplied) alpha, top row first
 *   close
 *
 * Threads: scan runs on the library thread. open / read / close of one clip always come from one thread, but two
 * clips can be open at the same time on different threads (playback and a thumbnail). Strings are UTF-16 and must
 * stay valid until the call that received or returned them is over (the plugin's id / name / version: for ever).
 * Video only in version 1.
 */
#ifndef DV_PLUGIN_H
#define DV_PLUGIN_H
#include <stdint.h>
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif

#define DVP_ABI 1u

typedef struct DvpHost {
    uint32_t abi;                       /* DVP_ABI of the host */
    void (*log)(const char* utf8);      /* one line into Divaverse's log */
    const wchar_t* data_dir;            /* a folder the plugin may keep its own settings and cache in (exists) */
} DvpHost;

typedef struct DvpItem {
    const wchar_t* id;                  /* unique inside the plugin and stable between runs; any text */
    const wchar_t* title;
    const wchar_t* group_id;            /* items with the same group_id are one performer card */
    const wchar_t* group_name;
    int order;                          /* position inside the group */
    int rating;                         /* 0 = none, 1..5 */
    double seconds;                     /* 0 = unknown */
} DvpItem;

typedef struct DvpClipInfo {
    int width, height;                  /* pixels */
    int fps_num, fps_den;
    int64_t frames;                     /* > 0; the clip loops */
    float anchor_u, ground_v;           /* where her feet are, 0..1 of the picture (typical: 0.5, 0.96) */
    double pixels_per_metre;            /* 0 = unknown */
} DvpClipInfo;

typedef void (*DvpEmit)(void* user, const DvpItem* item);

typedef struct DvpPlugin {
    uint32_t abi;                       /* DVP_ABI the plugin was built for */
    const wchar_t* id;                  /* short, letters / digits / _ - only: becomes part of item addresses */
    const wchar_t* name;
    const wchar_t* version;
    void  (*scan)(DvpEmit emit, void* user);
    void* (*open)(const wchar_t* item_id, DvpClipInfo* info);                 /* NULL = cannot open */
    int   (*read)(void* clip, int64_t frame, uint8_t* bgra, int stride);      /* 1 = ok. Mostly sequential, any frame may be asked */
    void  (*close)(void* clip);
    void  (*shutdown)(void);            /* may be NULL */
} DvpPlugin;

#ifdef __cplusplus
}
#endif
#endif
