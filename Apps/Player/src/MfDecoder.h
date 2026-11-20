#pragma once
// H.264 decode through the Windows built-in decoder MFT (CLSID_CMSH264DecoderMFT, msmpeg2vdec.dll).
// No source reader, no topology: Annex B access units in, NV12 out.
//   dev != null : DXVA2/D3D11VA through an IMFDXGIDeviceManager -> frames are ID3D11Texture2D array slices
//   dev == null : software decode -> NV12 in system memory
// All calls must come from one thread (the decode thread) that has COM + MFStartup initialised.
#include "Common.h"
#include "MkvDemux.h"
#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mfobjects.h>
#include <deque>
#include <set>
#include <atomic>

struct MfFrame {
    int64_t t100 = 0;                       // presentation time, 100 ns units (as fed on input)
    int w = 0, h = 0;                       // visible size
    int rows = 0;                           // buffer height (luma rows before the chroma plane)
    int stride = 0;                         // default stride for plain memory buffers
    ComPtr<IMFSample> sample;               // owns the surface / memory
    ComPtr<ID3D11Texture2D> tex; UINT sub = 0;      // GPU frame (tex != null)
    // CPU frame: valid between Lock() and Unlock()
    const uint8_t* y = nullptr; const uint8_t* uv = nullptr; int pitch = 0;
    bool Lock(); void Unlock();
private:
    ComPtr<IMFMediaBuffer> buf_; ComPtr<IMF2DBuffer> buf2d_;
};

class MfH264Decoder {
public:
    ~MfH264Decoder() { Shutdown(); }
    bool Init(const AvcConfig& cfg, int fpsNum, int fpsDen, ID3D11Device* dev, bool lowLatency, std::wstring& err);
    void Shutdown();
    // One access unit (length-prefixed NALs as stored in Matroska). Decoded frames are appended to out
    // in presentation order.
    bool Feed(const uint8_t* avcc, size_t n, int64_t t100, bool key, std::deque<MfFrame>& out, std::wstring& err);
    bool Drain(std::deque<MfFrame>& out, std::wstring& err);     // end of stream: emit everything still buffered
    void Flush();                                                 // discard state; next Feed starts with SPS/PPS
    bool D3DManaged() const { return mgr_ != nullptr; }           // a device manager was accepted by the MFT
    int  GpuFrames() const { return gpuFrames_; }                 // frames that actually arrived as D3D11 surfaces
    int  CpuFrames() const { return cpuFrames_; }
    int  StreamChanges() const { return streamChanges_; }
    float FeedMs() const { return feedMs_; }                      // smoothed cost of one access unit (input + whatever output it released)

private:
    bool SetOutputType(std::wstring& err);
    bool Pull(std::deque<MfFrame>& out, std::wstring& err);

    AvcConfig cfg_; int64_t dur100_ = 0;
    ComPtr<IMFTransform> mft_; ComPtr<IMFDXGIDeviceManager> mgr_; UINT mgrToken_ = 0;
    bool providesSamples_ = false; DWORD outSize_ = 0, outAlign_ = 0;
    int outW_ = 0, outH_ = 0, stride_ = 0;
    bool needParams_ = true, streaming_ = false;
    std::multiset<int64_t> pendingTs_;
    std::vector<uint8_t> au_;
    int gpuFrames_ = 0, cpuFrames_ = 0, streamChanges_ = 0; std::atomic<float> feedMs_{0};
};

std::wstring HrText(HRESULT hr);
extern std::atomic<int> g_decTrace;          // > 0: every decoder call is logged (counts down per line); Settings -> Record GPU trace
