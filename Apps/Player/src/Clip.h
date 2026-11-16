#pragma once
#include "Common.h"

// Stream layout of a Divaverse clip (see Documentation/FORMAT.md)
enum class Layout {
    Auto,    // decide from the file: 2+ video streams -> Dual, else Packed (top/bottom halves)
    Packed,  // one video stream; colour in rgb_rect, alpha (luma) in alpha_rect
    Dual,    // colour in rgb_track, alpha (luma) in alpha_track; paired by PTS
    Opaque   // no alpha (plain video) -- rendered fully opaque
};

struct ClipDesc {
    std::wstring id;
    std::wstring path;
    Layout layout = Layout::Auto;
    int width = 0, height = 0;        // performer size (colour rect)
    int fpsNum = 0, fpsDen = 1;       // 0 -> take from stream
    int frames = 0;                   // 0 -> discover at EOF
    int rgbTrack = 0, alphaTrack = 1; // index among *video* streams
    bool rectsKnown = false;
    RECT rgbRect{}, alphaRect{};      // pixel rects inside the decoded frame(s)
    float groundY = 0.96f;            // normalized feet baseline (ground anchor v)
    float alphaLo = 0.f, alphaHi = 1.f; // optional runtime levels
    // ---- creator-authored presentation metadata (embedded divaverse.json / sidecar); see DIVAVERSE_MKV_INPUT_SPEC.md
    std::wstring title;
    float anchorU = 0.5f;             // ground anchor u (normalized canvas x)
    int canvasW = 0, canvasH = 0;     // logical canvas; 0 -> colour rect size
    double ppm = 0;                   // canvas pixels per metre at the reference plane; 0 -> uncalibrated
    double actorHeightM = 0;
    bool loopOnce = false;
};

struct Show {
    std::wstring root, title, showId, divaId, divaName;
    std::vector<std::pair<std::wstring, std::wstring>> profile; int rating = 0;   // optional diva card details
    std::vector<ClipDesc> clips;
};

// Accepts: show directory (with manifest.json), a manifest .json, or a single .mkv
// (optionally with a sibling <stem>.clip.json written by divapack).
bool LoadShowOrClip(const std::wstring& path, Show& out, std::wstring& err);
