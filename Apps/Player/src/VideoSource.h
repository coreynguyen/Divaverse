#pragma once
#include "Common.h"
#include "Clip.h"
#include <d3d11.h>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <memory>

// One decoded picture on the GPU, exposed as two SRVs:
//   srvY  : R8_UNORM   luma   (W   x H)
//   srvUV : R8G8_UNORM chroma (W/2 x H/2)
// HW path : a single NV12 texture with two plane views (GPU->GPU copy from the decoder's surface pool).
// SW path : two plain textures (R8 + R8G8) filled with UpdateSubresource.
struct Planes {
    ComPtr<ID3D11Texture2D> texA, texB;           // NV12 -> texA only
    ComPtr<ID3D11ShaderResourceView> srvY, srvUV;
    int w = 0, h = 0; bool nv12 = false;
};

struct FrameSlot {
    Planes p[2];          // [0] colour, [1] alpha (Dual only)
    int64_t frame = -1;   // global, monotonic across loops
    std::vector<uint8_t> mask; int mw = 0, mh = 0;   // coarse solid/transparent map of this frame (CPU decode) for hit testing
    enum State { Free, Ready, Shown } st = Free;
};

class VideoSource {
public:
    static constexpr int kSlots = 6;

    VideoSource() = default;
    ~VideoSource();
    bool Open(const ClipDesc& clip, ID3D11Device* dev, ID3D11DeviceContext* ctx, CtxLock* lock,
              bool allowHw, std::wstring& err);
    void Start();
    // false = the decode thread did not exit within the timeout (blocked inside the decoder). The object must then be
    // leaked, never destroyed: use Retire().
    bool Stop(DWORD timeoutMs = INFINITE);
    static void Retire(std::unique_ptr<VideoSource>& p);
    // Jump to a frame of the clip (both tracks together). Frames already queued are discarded; the caller re-anchors its clock.
    void Seek(int64_t frame);
    // Is the performer solid at this point of the colour rect (u, v in 0..1)? Uses the frame currently shown.
    bool HitTest(float u, float v) const;
    bool AlphaFullRange() const;
    double TrackOffsetMs() const { return trackOffsetMs_; }   // alpha PTS minus colour PTS at the first frame; must be 0

    // Render thread. Returns the newest Ready frame with frame <= target (marks it Shown and frees
    // the previously shown / skipped ones). Returns the current Shown slot if nothing newer is due.
    // earliest receives the smallest Ready frame number (or -1) for clock (re)synchronisation.
    const FrameSlot* Acquire(int64_t target, int64_t* earliest);

    double  Fps() const { return fps_; }
    int64_t ClipFrames() const { return clipFrames_.load(); }
    // true while frames arrive as D3D11 surfaces (before the first frame: a device manager was accepted)
    bool    HwActive() const { return cpuFrames_.load() ? false : hwActive_; }
    bool    Dual() const { return layout_ == Layout::Dual; }
    bool    HasAlpha() const { return layout_ != Layout::Opaque; }
    int     PerfW() const { return perfW_; }
    int     PerfH() const { return perfH_; }
    // normalized UV rects (u0,v0,u1,v1) inside the colour / alpha textures
    void    Rects(float rgb[4], float alpha[4]) const;
    uint64_t Dropped() const { return dropped_.load(); }
    uint64_t Unpaired() const { return unpaired_.load(); }
    bool    Failed() const { return failed_.load(); }
    bool    FullRange() const;
    // Decode-path facts for the tray tooltip / selftest JSON
    struct DecodeInfo { bool d3dManaged; int gpuFrames, cpuFrames, streamChanges; uint64_t loops, fed, emitted, tsAnomalies; const char* stage; };
    DecodeInfo Info() const;
    void SetLowLatency(bool on) { lowLatency_ = on; }      // call before Open

private:
    struct Impl; Impl* d = nullptr;
    void ThreadMain();
    bool InitOnThread(std::wstring& err);
    void DecodeLoop();
    bool Upload(FrameSlot& s, int plane, void* mfframe);
    bool EnsurePlanes(Planes& p, int w, int h, bool nv12);

    ID3D11Device* dev_ = nullptr; ID3D11DeviceContext* ctx_ = nullptr; CtxLock* lock_ = nullptr;
    ClipDesc clip_; Layout layout_ = Layout::Packed;
    double fps_ = 24.0; int perfW_ = 0, perfH_ = 0;
    bool hwActive_ = false;
    FrameSlot slots_[kSlots];
    std::mutex m_; std::condition_variable cv_;
    std::thread th_; std::atomic<bool> stop_{false}, failed_{false}, go_{false};
    bool qaStall_ = false;                                  // QA: emulate a GPU decoder that stops delivering frames
    bool allowHw_ = false, lowLatency_ = false, initDone_ = false, initOk_ = false; std::wstring initErr_;
    std::atomic<const char*> stage_{"init"}; std::atomic<uint64_t> fed_{0}, emitted_{0}, tsAnomalies_{0};
    std::atomic<int> gpuFrames_{0}, cpuFrames_{0}, streamChanges_{0}; std::atomic<uint64_t> loops_{0};
    std::atomic<int64_t> clipFrames_{0};
    std::atomic<int> epoch_{0}; std::atomic<int64_t> seekFrame_{-1}; double trackOffsetMs_ = 0; HANDLE exited_ = nullptr;
    std::atomic<uint64_t> dropped_{0}, unpaired_{0};
    int shown_ = -1;
    std::vector<uint8_t> uvScratch_;
};
