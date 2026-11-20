#include "MfDecoder.h"
#include <mferror.h>
#include <codecapi.h>
#include <algorithm>
#include <iterator>
#include <cstring>
#include <initguid.h>

// {62CE7E72-4C71-4D20-B15D-452831A87D9D}  Microsoft H264 Video Decoder MFT (wmcodecdsp.h: CLSID_CMSH264DecoderMFT)
DEFINE_GUID(DV_CLSID_MSH264DecoderMFT, 0x62CE7E72, 0x4C71, 0x4D20, 0xB1, 0x5D, 0x45, 0x28, 0x31, 0xA8, 0x7D, 0x9D);
// CODECAPI_AVLowLatencyMode {9C27891A-ED7A-40E1-88E8-B22727A024EE}
DEFINE_GUID(DV_CODECAPI_AVLowLatencyMode, 0x9C27891A, 0xED7A, 0x40E1, 0x88, 0xE8, 0xB2, 0x27, 0x27, 0xA0, 0x24, 0xEE);

std::wstring HrText(HRESULT hr) { wchar_t b[32]; swprintf(b, 32, L"0x%08X", (unsigned)hr); return b; }

bool MfFrame::Lock() {
    if (tex || !sample) return false;
    if (y) return true;
    if (!buf_ && FAILED(sample->ConvertToContiguousBuffer(&buf_))) return false;
    BYTE* p = nullptr; LONG pitch2 = 0;
    if (SUCCEEDED(buf_.As(&buf2d_)) && SUCCEEDED(buf2d_->Lock2D(&p, &pitch2)) && pitch2 > 0) {
        y = p; pitch = (int)pitch2;
    } else {
        buf2d_.Reset(); DWORD len = 0;
        if (FAILED(buf_->Lock(&p, nullptr, &len))) return false;
        y = p; pitch = stride;
        if (len < (DWORD)(pitch * rows + pitch * ((h + 1) / 2))) { buf_->Unlock(); y = nullptr; return false; }
    }
    uv = y + (size_t)pitch * rows;
    return true;
}
void MfFrame::Unlock() {
    if (!y) return;
    if (buf2d_) buf2d_->Unlock2D(); else if (buf_) buf_->Unlock();
    y = uv = nullptr;
}

void MfH264Decoder::Shutdown() {
    if (mft_) {
        if (streaming_) { mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0); streaming_ = false; }
        if (mgr_) mft_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, 0);
    }
    mft_.Reset(); mgr_.Reset(); pendingTs_.clear();
}

bool MfH264Decoder::SetOutputType(std::wstring& err) {
    HRESULT hr = S_OK;
    for (DWORD i = 0;; ++i) {
        ComPtr<IMFMediaType> t;
        hr = mft_->GetOutputAvailableType(0, i, &t);
        if (FAILED(hr)) { err = L"H.264 MFT offers no NV12 output (" + HrText(hr) + L")"; return false; }
        GUID sub{};
        if (FAILED(t->GetGUID(MF_MT_SUBTYPE, &sub)) || sub != MFVideoFormat_NV12) continue;
        if (FAILED(hr = mft_->SetOutputType(0, t.Get(), 0))) { err = L"SetOutputType(NV12) " + HrText(hr); return false; }
        UINT32 w = 0, h = 0; MFGetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, &w, &h);
        outW_ = (int)w; outH_ = (int)h;
        UINT32 st = 0; stride_ = SUCCEEDED(t->GetUINT32(MF_MT_DEFAULT_STRIDE, &st)) && (INT32)st > 0 ? (int)(INT32)st : outW_;
        break;
    }
    MFT_OUTPUT_STREAM_INFO si{};
    if (FAILED(hr = mft_->GetOutputStreamInfo(0, &si))) { err = L"GetOutputStreamInfo " + HrText(hr); return false; }
    providesSamples_ = (si.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
    outSize_ = si.cbSize; outAlign_ = si.cbAlignment;
    DWORD need = (DWORD)(stride_ * outH_ * 3 / 2);
    if (outSize_ < need) outSize_ = need;
    Log("mft: output NV12 %dx%d stride %d, %s samples, cb=%lu", outW_, outH_, stride_, providesSamples_ ? "MFT-allocated" : "caller-allocated", (unsigned long)outSize_);
    return true;
}

bool MfH264Decoder::Init(const AvcConfig& cfg, int fpsNum, int fpsDen, ID3D11Device* dev, bool lowLatency, std::wstring& err) {
    cfg_ = cfg; dur100_ = fpsNum > 0 ? (int64_t)10000000 * fpsDen / fpsNum : 0;
    HRESULT hr = CoCreateInstance(DV_CLSID_MSH264DecoderMFT, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mft_));
    if (FAILED(hr)) {                                   // not registered under the well-known CLSID: ask the MFT registry
        MFT_REGISTER_TYPE_INFO in{ MFMediaType_Video, MFVideoFormat_H264 };
        IMFActivate** acts = nullptr; UINT32 n = 0;
        HRESULT hr2 = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER, &in, nullptr, &acts, &n);
        if (SUCCEEDED(hr2)) {
            for (UINT32 i = 0; i < n; ++i) { if (!mft_) acts[i]->ActivateObject(IID_PPV_ARGS(&mft_)); acts[i]->Release(); }
            CoTaskMemFree(acts);
        }
        if (!mft_) { err = L"no H.264 decoder MFT on this system (CoCreateInstance " + HrText(hr) + L")"; return false; }
    }
    ComPtr<IMFAttributes> attrs; mft_->GetAttributes(&attrs);
    if (dev) {
        UINT32 aware = 0;
        if (!attrs || FAILED(attrs->GetUINT32(MF_SA_D3D11_AWARE, &aware)) || !aware) { err = L"H.264 MFT is not D3D11 aware"; Shutdown(); return false; }
        if (FAILED(hr = MFCreateDXGIDeviceManager(&mgrToken_, &mgr_)) || FAILED(hr = mgr_->ResetDevice(dev, mgrToken_))) {
            err = L"DXGI device manager " + HrText(hr); Shutdown(); return false; }
        if (FAILED(hr = mft_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, (ULONG_PTR)mgr_.Get()))) {
            err = L"MFT_MESSAGE_SET_D3D_MANAGER " + HrText(hr); mgr_.Reset(); Shutdown(); return false; }
    }
    if (attrs && lowLatency) attrs->SetUINT32(DV_CODECAPI_AVLowLatencyMode, TRUE);

    ComPtr<IMFMediaType> in;
    MFCreateMediaType(&in);
    in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    MFSetAttributeSize(in.Get(), MF_MT_FRAME_SIZE, (UINT32)cfg.cropW, (UINT32)cfg.cropH);
    if (fpsNum > 0) MFSetAttributeRatio(in.Get(), MF_MT_FRAME_RATE, (UINT32)fpsNum, (UINT32)fpsDen);
    MFSetAttributeRatio(in.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    in->SetUINT32(MF_MT_INTERLACE_MODE, cfg.frameMbsOnly ? MFVideoInterlace_Progressive : MFVideoInterlace_MixedInterlaceOrProgressive);
    if (FAILED(hr = mft_->SetInputType(0, in.Get(), 0))) { err = L"SetInputType(H264) " + HrText(hr); Shutdown(); return false; }
    if (!SetOutputType(err)) { Shutdown(); return false; }
    mft_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    streaming_ = true; needParams_ = true;
    return true;
}

std::atomic<int> g_decTrace{0};
#define DECTRACE(...) do { if (g_decTrace.load(std::memory_order_relaxed) > 0) { --g_decTrace; Log(__VA_ARGS__); } } while (0)

bool MfH264Decoder::Pull(std::deque<MfFrame>& out, std::wstring& err) {
    for (int guard = 0; guard < 256; ++guard) {
        MFT_OUTPUT_DATA_BUFFER ob{}; DWORD status = 0; ComPtr<IMFSample> mine;
        if (!providesSamples_) {
            ComPtr<IMFMediaBuffer> b;
            HRESULT h2 = outAlign_ > 1 ? MFCreateAlignedMemoryBuffer(outSize_, outAlign_ - 1, &b) : MFCreateMemoryBuffer(outSize_, &b);
            if (FAILED(h2) || FAILED(MFCreateSample(&mine)) || FAILED(mine->AddBuffer(b.Get()))) { err = L"output sample allocation failed"; return false; }
            ob.pSample = mine.Get();
        }
        double tq = NowSeconds(); HRESULT hr = mft_->ProcessOutput(0, 1, &ob, &status);
        DECTRACE("dectrace out hr=0x%08X status=0x%X sample=%d flags=0x%X ms=%.2f pending=%zu", (unsigned)hr, (unsigned)status, ob.pSample ? 1 : 0, (unsigned)ob.dwStatus, (NowSeconds() - tq) * 1000, pendingTs_.size());
        if (ob.pEvents) ob.pEvents->Release();
        ComPtr<IMFSample> got;
        if (providesSamples_) got.Attach(ob.pSample); else got = mine;      // MFT-allocated: we own the returned ref
        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) return true;
        if (hr == MF_E_TRANSFORM_STREAM_CHANGE || (ob.dwStatus & MFT_OUTPUT_DATA_BUFFER_FORMAT_CHANGE)) {
            ++streamChanges_;
            if (!SetOutputType(err)) return false;
            if (hr == MF_E_TRANSFORM_STREAM_CHANGE) continue;
        }
        if (FAILED(hr)) { err = L"ProcessOutput " + HrText(hr); return false; }
        if (!got) continue;
        DWORD total = 0; got->GetTotalLength(&total);
        MfFrame f; f.sample = got;
        f.w = std::min(cfg_.cropW, outW_); f.h = std::min(cfg_.cropH, outH_); f.rows = outH_; f.stride = stride_;
        ComPtr<IMFMediaBuffer> b0; ComPtr<IMFDXGIBuffer> dx;
        if (SUCCEEDED(got->GetBufferByIndex(0, &b0)) && SUCCEEDED(b0.As(&dx)) &&
            SUCCEEDED(dx->GetResource(IID_PPV_ARGS(&f.tex)))) { dx->GetSubresourceIndex(&f.sub); ++gpuFrames_; }
        else { if (!total) continue; ++cpuFrames_; }
        // Timestamp: decoders echo the input PTS in presentation order. If one does not, the smallest pending
        // input PTS is by definition the next frame to present.
        LONGLONG t = 0; bool have = SUCCEEDED(got->GetSampleTime(&t));
        auto it = have ? pendingTs_.find(t) : pendingTs_.end();
        if (it != pendingTs_.end()) pendingTs_.erase(pendingTs_.begin(), std::next(it));
        else if (!pendingTs_.empty()) { t = *pendingTs_.begin(); pendingTs_.erase(pendingTs_.begin()); }
        f.t100 = t;
        out.push_back(std::move(f));
    }
    err = L"ProcessOutput did not settle"; return false;
}

bool MfH264Decoder::Feed(const uint8_t* avcc, size_t n, int64_t t100, bool key, std::deque<MfFrame>& out, std::wstring& err) {
    au_.clear();
    bool idr = false;
    size_t mark = 0;
    if (needParams_ || key) { AvcParamSetsAnnexB(cfg_, au_); mark = au_.size(); }
    if (!AvcToAnnexB(avcc, n, cfg_.nalLenSize, au_, &idr)) { err = L"malformed H.264 access unit"; return false; }
    if (!needParams_ && key && !idr) au_.erase(au_.begin(), au_.begin() + mark);    // non-IDR recovery point: no parameter sets needed
    if (needParams_ && !idr) return true;                 // after a flush nothing decodes before an IDR: skip
    ComPtr<IMFMediaBuffer> b; ComPtr<IMFSample> s; BYTE* p = nullptr;
    if (FAILED(MFCreateMemoryBuffer((DWORD)au_.size(), &b)) || FAILED(b->Lock(&p, nullptr, nullptr))) { err = L"input buffer allocation failed"; return false; }
    memcpy(p, au_.data(), au_.size()); b->Unlock(); b->SetCurrentLength((DWORD)au_.size());
    MFCreateSample(&s); s->AddBuffer(b.Get());
    s->SetSampleTime(t100); if (dur100_) s->SetSampleDuration(dur100_);
    if (idr) s->SetUINT32(MFSampleExtension_CleanPoint, TRUE);
    if (needParams_) s->SetUINT32(MFSampleExtension_Discontinuity, TRUE);
    needParams_ = false;
    pendingTs_.insert(t100);
    double tf = NowSeconds(); struct Cost { double t; std::atomic<float>& m; ~Cost() { float v = (float)((NowSeconds() - t) * 1000); m = m + (v - m) * 0.1f; } } cost{ tf, feedMs_ };
    for (int tries = 0; tries < 64; ++tries) {
        double tq = NowSeconds(); HRESULT hr = mft_->ProcessInput(0, s.Get(), 0);
        DECTRACE("dectrace in t=%lld idr=%d bytes=%zu hr=0x%08X try=%d ms=%.2f d3d=%d", (long long)t100, idr ? 1 : 0, au_.size(), (unsigned)hr, tries, (NowSeconds() - tq) * 1000, mgr_ ? 1 : 0);
        if (hr == MF_E_NOTACCEPTING) {
            size_t before = out.size();
            if (!Pull(out, err)) return false;
            if (out.size() == before && tries > 2) { err = L"H.264 MFT accepts no input and produces no output"; return false; }
            continue;
        }
        if (FAILED(hr)) { err = L"ProcessInput " + HrText(hr); return false; }
        return Pull(out, err);
    }
    err = L"ProcessInput stuck"; return false;
}

bool MfH264Decoder::Drain(std::deque<MfFrame>& out, std::wstring& err) {
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
    mft_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
    return Pull(out, err);
}

void MfH264Decoder::Flush() {
    mft_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    pendingTs_.clear(); needParams_ = true;
}
