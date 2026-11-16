#pragma once
// Divaverse media contract, reader side. See Documentation/DIVAVERSE_MKV_INPUT_SPEC.md.
//   defaults  <  embedded attachment "divaverse.json"  <  sidecar "<basename>.divaverse.json"   (field-level merge)
// Facts (codec, track sizes, frame rate, duration, which tracks exist) always come from the container.
#include "Common.h"
#include "Clip.h"
#include "Json.h"

struct MediaMeta {
    int schema = 0;                               // 0 = no metadata found
    bool embedded = false, sidecar = false;
    std::string divaId, divaName, showId, showTitle, clipId, title;
    int order = 0, rating = 0;
    std::vector<std::pair<std::string, std::string>> profile;       // free-form diva card rows, in authored order
    std::string layout;                           // "packed" | "opaque" ("dual" is implied by two video tracks)
    bool hasRects = false; int rgbRect[4] = {}, alphaRect[4] = {};   // x, y, w, h in pixels of the decoded picture
    bool hasLevels = false; float alphaLo = 0, alphaHi = 1;
    int canvasW = 0, canvasH = 0;
    double actorHeightM = 0, ppm = 0;
    bool hasAnchor = false; float anchorU = 0.5f, anchorV = 0.96f;
    std::string loopMode;                         // "loop" (default) | "once"
    std::string cover, thumb;                     // artwork: attachment name, or (sidecar only) a relative file path
    bool coverFromSidecar = false;
    std::string origin, contentRating;
};

struct TrackFacts { bool present = false; std::string codec, name; int w = 0, h = 0, range = 0, profile = 0, level = 0, bitDepth = 8; };

struct MediaInfo {
    std::wstring path; uint64_t fileSize = 0;
    std::string docType; double durationS = 0, fps = 0; int frames = 0, videoTracks = 0; size_t cues = 0;
    TrackFacts rgb, alpha; bool dual = false;
    std::vector<std::string> attachments;         // "name (mime, bytes)"
    bool hasCover = false, hasMetaAttachment = false, hasSidecar = false;
    MediaMeta meta;
    std::vector<std::string> errors, warnings;    // "DV-E0xx: ..." / "DV-W1xx: ..."
    bool playable() const { return errors.empty(); }
};

// Field-level merge: objects merge key by key, scalars and arrays replace, null removes the key.
// Keys under "media" (informational facts) are never taken from `over`; each attempt is recorded in `blocked`.
void JsonMergeInto(JVal& base, const JVal& over, std::vector<std::string>* blocked, const std::string& path = {});
void MetaFromJson(const JVal& j, MediaMeta& m, bool fromSidecar);

// Opens the container, reads facts + metadata (embedded, then sidecar) and fills `desc` (if given) so the clip can be
// played. Does not decode video. `desc` may carry legacy manifest values; they act as defaults below the embedded data.
bool ProbeMedia(const std::wstring& path, MediaInfo& info, ClipDesc* desc);
std::wstring SidecarPath(const std::wstring& mediaPath);     // <dir>\<basename>.divaverse.json
