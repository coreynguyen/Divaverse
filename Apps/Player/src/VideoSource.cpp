// Clip decode: MkvDemux (own EBML reader) -> MfH264Decoder (OS decoder MFT) -> slot ring of NV12 planes.
// No third-party code. Everything Media Foundation lives and dies on the decode thread.
#include "VideoSource.h"
#include "MkvDemux.h"
#include "MkvIoWin.h"
#include "MfDecoder.h"
#include <deque>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace {
struct Pending { MfFrame f; int64_t idx; };

}

struct VideoSource::Impl {
    Win32Io io; MkvDemux mkv;
    int roles = 1;
    int trk[2] = { -1, -1 };             // MkvDemux track index per role (0 colour, 1 alpha)
    AvcConfig cfg[2];
    MfH264Decoder dec[2];
    int64_t base100 = 0;                 // PTS of the first colour frame: the one timeline both tracks are measured on
    bool alphaFull = false;
    int texW[2] = {}, texH[2] = {};
    bool fullRange = false;
    std::deque<Pending> q[2]; std::deque<MfFrame> fresh;
    int64_t lastIdx[2] = { -1, -1 };
    bool mfStarted = false, comInit = false;
};

VideoSource::~VideoSource() { Stop(); delete d; d = nullptr; if (exited_) CloseHandle(exited_); }

void VideoSource::Retire(std::unique_ptr<VideoSource>& p) {
    if (!p) return;
    if (p->Stop(1500)) { p.reset(); return; }
    Log("decode thread did not stop (stage %s): abandoning it", p->stage_.load());
    (void)p.release();                                // deliberately leaked: the stuck thread still owns it
}

bool VideoSource::AlphaFullRange() const { return d && d->alphaFull; }

void VideoSource::Seek(int64_t frame) {
    {
        std::lock_guard<std::mutex> lk(m_);
        for (auto& s : slots_) if (s.st == FrameSlot::Ready) s.st = FrameSlot::Free;
        seekFrame_ = std::max<int64_t>(0, frame); ++epoch_;
    }
    cv_.notify_all();
}

bool VideoSource::HitTest(float u, float v) const {
    if (shown_ < 0 || u < 0 || v < 0 || u >= 1 || v >= 1) return false;
    const FrameSlot& s = slots_[shown_];
    if (s.mask.empty()) return layout_ == Layout::Opaque;   // no alpha map for this frame (GPU decode): never block the desktop on a guess
    int x = (int)(u * s.mw), y = (int)(v * s.mh);
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {       // one cell of slack around edges
        int xx = x + dx, yy = y + dy;
        if (xx >= 0 && yy >= 0 && xx < s.mw && yy < s.mh && s.mask[(size_t)yy * s.mw + xx]) return true;
    }
    return false;
}

bool VideoSource::FullRange() const { return d && d->fullRange; }

VideoSource::DecodeInfo VideoSource::Info() const {
    float fm = 0; if (d) for (auto& dc : d->dec) fm += dc.FeedMs();        // both tracks of a dual clip: cost per shown frame
    return { hwActive_, gpuFrames_.load(), cpuFrames_.load(), streamChanges_.load(), loops_.load(), fed_.load(), emitted_.load(), tsAnomalies_.load(), stage_.load(), fm };
}

void VideoSource::Rects(float rgb[4], float alpha[4]) const {
    auto norm = [](const RECT& r, int w, int h, float o[4]) {
        o[0] = (float)r.left / w; o[1] = (float)r.top / h; o[2] = (float)r.right / w; o[3] = (float)r.bottom / h;
    };
    norm(clip_.rgbRect, d->texW[0], d->texH[0], rgb);
    int ar = layout_ == Layout::Dual ? 1 : 0;
    norm(clip_.alphaRect, d->texW[ar], d->texH[ar], alpha);
}

bool VideoSource::Open(const ClipDesc& clip, ID3D11Device* dev, ID3D11DeviceContext* ctx, CtxLock* lock,
                       bool allowHw, std::wstring& err) {
    clip_ = clip; dev_ = dev; ctx_ = ctx; lock_ = lock; allowHw_ = allowHw;
    d = new Impl;
    stop_ = false; go_ = false; initDone_ = false;
    exited_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    th_ = std::thread([this] { ThreadMain(); });
    std::unique_lock<std::mutex> lk(m_);
    cv_.wait(lk, [&] { return initDone_; });
    if (!initOk_) { err = initErr_; lk.unlock(); Stop(); return false; }
    return true;
}

bool VideoSource::InitOnThread(std::wstring& err) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    d->comInit = SUCCEEDED(hr);
    if (FAILED(hr = MFStartup(MF_VERSION, MFSTARTUP_LITE))) { err = L"Media Foundation unavailable (MFStartup " + HrText(hr) + L")"; return false; }
    d->mfStarted = true;

    if (!d->io.Open(clip_.path)) { err = L"cannot open file (error " + std::to_wstring(GetLastError()) + L")"; return false; }
    std::string e8;
    if (!d->mkv.Open(&d->io, e8)) { err = L"not a playable Matroska file: " + Utf8ToW(e8); return false; }

    auto& tracks = d->mkv.Tracks();
    std::vector<int> vids;
    for (int i = 0; i < (int)tracks.size(); ++i) if (tracks[i].type == 1) vids.push_back(i);
    for (auto& t : tracks) t.enabled = false;            // audio, subtitles, unused video: payload never read
    if (vids.empty()) { err = L"no video track"; return false; }

    layout_ = clip_.layout;
    if (layout_ == Layout::Auto) layout_ = vids.size() >= 2 ? Layout::Dual : Layout::Packed;
    int rgbV = clip_.rgbTrack, aV = clip_.alphaTrack;
    // Prefer explicit track-role names written by divapack over positional tracks.
    for (int k = 0; k < (int)vids.size(); ++k) {
        if (tracks[vids[k]].name == "divaverse:rgb") rgbV = k;
        if (tracks[vids[k]].name == "divaverse:alpha") aV = k;
    }
    if (layout_ == Layout::Dual) {
        if (rgbV < 0 || aV < 0 || rgbV >= (int)vids.size() || aV >= (int)vids.size() || rgbV == aV) { err = L"dual layout: bad track roles"; return false; }
        d->roles = 2; d->trk[0] = vids[rgbV]; d->trk[1] = vids[aV];
    } else {
        if (rgbV < 0 || rgbV >= (int)vids.size()) rgbV = 0;
        d->roles = 1; d->trk[0] = vids[rgbV];
    }

    int fpsNum = clip_.fpsNum, fpsDen = clip_.fpsDen > 0 ? clip_.fpsDen : 1;
    const MkvTrack& t0 = tracks[d->trk[0]];
    if (fpsNum > 0) fps_ = (double)fpsNum / fpsDen;
    else if (t0.defaultDurationNs) {
        fps_ = 1e9 / (double)t0.defaultDurationNs;
        fpsNum = (int)std::llround(fps_ * 1000); fpsDen = 1000;
        for (int n : { 24, 25, 30, 48, 50, 60, 120 }) if (std::fabs(fps_ - n) < 0.005) { fpsNum = n; fpsDen = 1; fps_ = n; }
        for (int n : { 24000, 30000, 60000 }) if (std::fabs(fps_ - n / 1001.0) < 0.005) { fpsNum = n; fpsDen = 1001; fps_ = n / 1001.0; }
    }
    if (!(fps_ > 1 && fps_ < 241)) { fps_ = 24.0; fpsNum = 24; fpsDen = 1; }
    clipFrames_ = clip_.frames;

    bool hw = allowHw_;
    for (int k = 0; k < d->roles; ++k) {
        MkvTrack& t = tracks[d->trk[k]];
        if (t.codecId != "V_MPEG4/ISO/AVC") { err = L"codec " + Utf8ToW(t.codecId) + L" is not supported by this build (H.264 only)"; return false; }
        if (!t.unsupported.empty()) { err = Utf8ToW(t.unsupported); return false; }
        if (!AvcParseConfig(t.codecPrivate.data(), t.codecPrivate.size(), d->cfg[k], e8)) { err = L"H.264 configuration: " + Utf8ToW(e8); return false; }
        const AvcConfig& c = d->cfg[k];
        if (c.bitDepth != 8 || c.chromaFormat != 1) { err = L"only 8-bit 4:2:0 H.264 is supported (stream is " + std::to_wstring(c.bitDepth) + L"-bit, chroma_format_idc " + std::to_wstring(c.chromaFormat) + L")"; return false; }
        t.enabled = true;
        d->texW[k] = c.cropW; d->texH[k] = c.cropH;
        if (k == 0) d->fullRange = t.colourRange == 2 || (t.colourRange == 0 && c.fullRange);
    }
    bool forceHw = GetEnvironmentVariableW(L"DIVAVERSE_FORCE_HW", nullptr, 0) != 0;   // test hook: skip the NV12 capability gate
    if (hw && !forceHw) {
        UINT fs = 0;
        if (FAILED(dev_->CheckFormatSupport(DXGI_FORMAT_NV12, &fs)) || !(fs & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE)) {
            Log("NV12 not sampleable on this device -> software decode"); hw = false;
        }
    }
    for (int k = 0; k < d->roles; ++k) {
        std::wstring e;
        if (!d->dec[k].Init(d->cfg[k], fpsNum, fpsDen, hw ? dev_ : nullptr, lowLatency_, e)) {
            if (hw) {                                     // GPU path refused: same MFT without a device manager
                Log("hardware decode init failed (%s) -> software decode", WToUtf8(e).c_str());
                for (int j = 0; j <= k; ++j) d->dec[j].Shutdown();
                hw = false; k = -1; continue;
            }
            err = e; return false;
        }
    }
    hwActive_ = hw;
    d->alphaFull = d->roles == 2 ? tracks[d->trk[1]].colourRange == 2 || (tracks[d->trk[1]].colourRange == 0 && d->cfg[1].fullRange) : d->fullRange;
    {   // One timeline for both tracks: the first colour frame is frame 0. An alpha track that starts elsewhere is a
        // defect of the file and must stay visible as unpaired frames, so its offset is never normalised away.
        MkvPacket pk; std::string e; int64_t first[2] = { INT64_MIN, INT64_MIN }; int seen = 0;
        for (int guard = 0; guard < 128 && seen < d->roles && d->mkv.Read(pk, e); ++guard) {
            int k = pk.track == d->trk[0] ? 0 : pk.track == d->trk[1] ? 1 : -1;
            if (k >= 0 && first[k] == INT64_MIN) { first[k] = pk.ptsNs / 100; ++seen; }
        }
        d->mkv.Rewind();
        d->base100 = first[0] == INT64_MIN ? 0 : first[0];
        if (d->roles == 2 && first[1] != INT64_MIN) trackOffsetMs_ = (double)(first[1] - d->base100) / 1e4;
        if (trackOffsetMs_ != 0) Log("DV-W110: alpha track starts %.1f ms %s the colour track", std::fabs(trackOffsetMs_), trackOffsetMs_ > 0 ? "after" : "before");
    }
    if (allowHw_ && GetEnvironmentVariableW(L"DIVAVERSE_QA_STALL", nullptr, 0)) { qaStall_ = true; hwActive_ = true; }

    if (!clip_.rectsKnown) {
        int W = d->texW[0], H = d->texH[0];
        if (layout_ == Layout::Packed)      { clip_.rgbRect = { 0, 0, W, H / 2 }; clip_.alphaRect = { 0, H / 2, W, H }; }
        else if (layout_ == Layout::Dual)   { clip_.rgbRect = { 0, 0, W, H }; clip_.alphaRect = { 0, 0, d->texW[1], d->texH[1] }; }
        else                                { clip_.rgbRect = { 0, 0, W, H }; clip_.alphaRect = clip_.rgbRect; }
        clip_.rectsKnown = true;
    }
    perfW_ = clip_.rgbRect.right - clip_.rgbRect.left;
    perfH_ = clip_.rgbRect.bottom - clip_.rgbRect.top;
    const AvcConfig& c0 = d->cfg[0];
    Log("open %s: layout=%s %dx%d tex %dx%d fps=%.3f (%d/%d) h264 profile %d level %d reorder %d, cues %zu, decode=%s range=%s",
        WToUtf8(clip_.path).c_str(), layout_ == Layout::Dual ? "dual" : layout_ == Layout::Packed ? "packed" : "opaque",
        perfW_, perfH_, d->texW[0], d->texH[0], fps_, fpsNum, fpsDen, c0.profile, c0.level, c0.maxNumReorder, d->mkv.Cues().size(),
        hw ? "MFT+D3D11" : "MFT software", d->fullRange ? "full" : "limited");
    return true;
}

bool VideoSource::EnsurePlanes(Planes& p, int w, int h, bool nv12) {
    if (p.srvY && p.w == w && p.h == h && p.nv12 == nv12) return true;
    p = Planes{}; p.w = w; p.h = h; p.nv12 = nv12;
    D3D11_TEXTURE2D_DESC td{}; td.MipLevels = 1; td.ArraySize = 1; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{}; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
    if (nv12) {
        td.Width = w; td.Height = h; td.Format = DXGI_FORMAT_NV12;
        if (FAILED(dev_->CreateTexture2D(&td, nullptr, &p.texA))) return false;
        sd.Format = DXGI_FORMAT_R8_UNORM;   if (FAILED(dev_->CreateShaderResourceView(p.texA.Get(), &sd, &p.srvY))) return false;
        sd.Format = DXGI_FORMAT_R8G8_UNORM; if (FAILED(dev_->CreateShaderResourceView(p.texA.Get(), &sd, &p.srvUV))) return false;
    } else {
        td.Width = w; td.Height = h; td.Format = DXGI_FORMAT_R8_UNORM;
        if (FAILED(dev_->CreateTexture2D(&td, nullptr, &p.texA))) return false;
        td.Width = (w + 1) / 2; td.Height = (h + 1) / 2; td.Format = DXGI_FORMAT_R8G8_UNORM;
        if (FAILED(dev_->CreateTexture2D(&td, nullptr, &p.texB))) return false;
        sd.Format = DXGI_FORMAT_R8_UNORM;   if (FAILED(dev_->CreateShaderResourceView(p.texA.Get(), &sd, &p.srvY))) return false;
        sd.Format = DXGI_FORMAT_R8G8_UNORM; if (FAILED(dev_->CreateShaderResourceView(p.texB.Get(), &sd, &p.srvUV))) return false;
    }
    return true;
}

bool VideoSource::Upload(FrameSlot& s, int plane, void* mf) {
    MfFrame& f = *(MfFrame*)mf;
    Planes& p = s.p[plane];
    if (f.tex) {                                          // decoder surface (texture-array slice) -> slot, GPU->GPU
        int w = f.w & ~1, h = f.h & ~1;
        if (!EnsurePlanes(p, w, h, true)) { Log("NV12 slot alloc failed"); return false; }
        D3D11_BOX box{ 0, 0, 0, (UINT)w, (UINT)h, 1 };
        {
            std::lock_guard<CtxLock> g(*lock_);
            ctx_->CopySubresourceRegion(p.texA.Get(), 0, 0, 0, 0, f.tex.Get(), f.sub, &box);
            ctx_->Flush();                                // the copy is submitted before the surface goes back to the decoder
        }
        f.tex.Reset(); f.sample.Reset();                  // return the decoder surface to its pool right now
        return true;
    }
    if (!EnsurePlanes(p, f.w, f.h, false)) { Log("plane alloc failed"); return false; }
    if (!f.Lock()) { Log("cannot lock decoder output buffer"); return false; }
    if (layout_ != Layout::Opaque && plane == (layout_ == Layout::Dual ? 1 : 0)) {   // coarse alpha map for click hit-testing
        const RECT& ar = clip_.alphaRect; int aw = ar.right - ar.left, ah = ar.bottom - ar.top;
        if (aw > 0 && ah > 0 && ar.right <= f.w && ar.bottom <= f.h) {
            s.mw = std::max(1, aw / 4); s.mh = std::max(1, ah / 4); s.mask.resize((size_t)s.mw * s.mh);
            int thr = d->alphaFull ? 64 : 16 + 55;                                    // about 25 % opacity
            for (int y = 0; y < s.mh; ++y) {
                const uint8_t* row = f.y + (size_t)(ar.top + std::min(ah - 1, y * 4 + 2)) * f.pitch + ar.left;
                uint8_t* o = &s.mask[(size_t)y * s.mw];
                for (int x = 0; x < s.mw; ++x) o[x] = row[std::min(aw - 1, x * 4 + 2)] > thr;
            }
        }
    }
    {
        std::lock_guard<CtxLock> g(*lock_);
        ctx_->UpdateSubresource(p.texA.Get(), 0, nullptr, f.y, (UINT)f.pitch, 0);       // NV12 luma  -> R8
        ctx_->UpdateSubresource(p.texB.Get(), 0, nullptr, f.uv, (UINT)f.pitch, 0);      // NV12 CbCr  -> R8G8
    }
    f.Unlock();
    return true;
}

void VideoSource::Start() { go_ = true; cv_.notify_all(); }

bool VideoSource::Stop(DWORD timeoutMs) {
    stop_ = true; cv_.notify_all();
    if (!th_.joinable()) return true;
    if (exited_ && WaitForSingleObject(exited_, timeoutMs) == WAIT_TIMEOUT) { th_.detach(); return false; }
    th_.join();
    return true;
}

void VideoSource::ThreadMain() {
    using SetDescFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);               // Win10 1607+, resolved at runtime
    if (auto fn = (SetDescFn)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"))
        fn(GetCurrentThread(), L"Divaverse decode");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    std::wstring err;
    bool ok = InitOnThread(err);
    {
        std::lock_guard<std::mutex> lk(m_);
        initOk_ = ok; initErr_ = err; initDone_ = true;
    }
    cv_.notify_all();
    if (ok) {
        { std::unique_lock<std::mutex> lk(m_); cv_.wait(lk, [&] { return go_.load() || stop_.load(); }); }
        if (!stop_) DecodeLoop();
    }
    // teardown on the thread that created the MF objects
    for (auto& q : d->q) q.clear();
    d->fresh.clear();
    for (auto& s : slots_) { /* slot textures are plain D3D11 objects: released with the VideoSource */ (void)s; }
    for (auto& dc : d->dec) dc.Shutdown();
    if (d->mfStarted) { MFShutdown(); d->mfStarted = false; }
    if (d->comInit) { CoUninitialize(); d->comInit = false; }
    if (exited_) SetEvent(exited_);
}

void VideoSource::DecodeLoop() {
    int64_t loopBase = 0, maxIdx = -1, skipBelow = -1;
    bool eof = false; int myEpoch = epoch_.load();
    // Seeking: each track restarts at ITS OWN last keyframe at or before the target (the tracks need not share a GOP
    // structure), everything before the target is decoded but not shown, so the first frame shown is exactly the target.
    std::vector<int64_t> keys[2]; bool keysBuilt = false; int64_t gate[2] = { -1, -1 };     // gate >= 0: discard until that keyframe
    auto gridIdx = [&](int64_t ptsNs) { return (int64_t)llround((double)(ptsNs / 100 - d->base100) * fps_ / 1e7); };
    MkvPacket pkt; std::string e8; std::wstring err;
    int roleOf[64]; for (int& x : roleOf) x = -1;
    for (int k = 0; k < d->roles; ++k) if (d->trk[k] < 64) roleOf[d->trk[k]] = k;

    auto collect = [&](int k) {                          // decoder output -> pairing queue, indexed on the frame grid
        for (auto& f : d->fresh) {
            // Matroska stores 1 ms ticks, so 1/24 s arrives as 41/42 ms: snap to the grid.
            int64_t idx = llround((double)(f.t100 - d->base100) * fps_ / 1e7);
            if (idx < 0) continue;                                    // before the timeline origin
            // The decoder emits in presentation order. If its timestamps disagree (not echoed, restamped), trust
            // the order: the next frame is lastIdx + 1. Playback can never stall or skip on a timestamp quirk.
            if (idx <= d->lastIdx[k]) { idx = d->lastIdx[k] + 1; ++tsAnomalies_; }
            d->lastIdx[k] = idx;
            if (idx < skipBelow) continue;                            // seek pre-roll: decoded, not shown
            d->q[k].push_back({ std::move(f), idx });
        }
        d->fresh.clear();
        gpuFrames_ = d->dec[0].GpuFrames() + (d->roles == 2 ? d->dec[1].GpuFrames() : 0);
        cpuFrames_ = d->dec[0].CpuFrames() + (d->roles == 2 ? d->dec[1].CpuFrames() : 0);
        streamChanges_ = d->dec[0].StreamChanges() + (d->roles == 2 ? d->dec[1].StreamChanges() : 0);
    };
    auto emit = [&](MfFrame* a, MfFrame* b, int64_t idx) -> bool {
        int si = -1;
        if (qaStall_ && emitted_ >= 6) { stage_ = "qa-stall"; while (!stop_) Sleep(20); return false; }
        stage_ = "wait-slot";
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [&] {
                if (stop_) return true;
                if (epoch_.load() != myEpoch) return true;
                for (int i = 0; i < kSlots; ++i) if (slots_[i].st == FrameSlot::Free) { si = i; return true; }
                return false;
            });
            if (stop_) return false;
            if (epoch_.load() != myEpoch || si < 0) return true;      // a seek arrived: this frame is obsolete
        }
        // Free slots are owned exclusively by this thread until marked Ready.
        stage_ = "upload";
        bool ok = Upload(slots_[si], 0, a) && (!b || Upload(slots_[si], 1, b));
        if (ok) { std::lock_guard<std::mutex> lk(m_); if (epoch_.load() == myEpoch) { slots_[si].frame = loopBase + idx; slots_[si].st = FrameSlot::Ready; ++emitted_; } }
        stage_ = "run";
        if (idx > maxIdx) maxIdx = idx;
        return ok;
    };

    while (!stop_) {
        if (epoch_.load() != myEpoch) {                               // seek: both tracks restart from the same cue
            myEpoch = epoch_.load(); int64_t tgt = seekFrame_.load();
            for (int k = 0; k < d->roles; ++k) { d->q[k].clear(); d->lastIdx[k] = -1; d->dec[k].Flush(); }
            d->fresh.clear(); eof = false; loopBase = 0; maxIdx = -1; skipBelow = tgt;
            stage_ = "seek";
            if (!keysBuilt) {                                         // one pass over the file, first seek only
                MkvPacket p; std::string e; d->mkv.Rewind();
                while (!stop_ && d->mkv.Read(p, e)) { int k = p.track >= 0 && p.track < 64 ? roleOf[p.track] : -1; if (k >= 0 && p.key) keys[k].push_back(gridIdx(p.ptsNs)); }
                for (auto& v : keys) std::sort(v.begin(), v.end());
                keysBuilt = true;
            }
            int64_t start = tgt;
            for (int k = 0; k < d->roles; ++k) {
                int64_t kf = 0; for (int64_t x : keys[k]) { if (x > tgt) break; kf = x; }
                gate[k] = kf; start = std::min(start, kf);
            }
            d->mkv.SeekNs((d->base100 + (int64_t)((double)start / fps_ * 1e7)) * 100 - 1000000);   // 1 ms earlier: cue times are rounded to ms
        }
        // 1) emit whatever is complete
        if (d->roles == 1) {
            if (!d->q[0].empty()) {
                Pending p = std::move(d->q[0].front()); d->q[0].pop_front();
                if (!emit(&p.f, nullptr, p.idx)) { if (!stop_) failed_ = true; break; }
                continue;
            }
        } else if (!d->q[0].empty() && !d->q[1].empty()) {
            if (d->q[0].front().idx == d->q[1].front().idx) {
                Pending pc = std::move(d->q[0].front()), pa = std::move(d->q[1].front());
                d->q[0].pop_front(); d->q[1].pop_front();
                if (!emit(&pc.f, &pa.f, pc.idx)) { if (!stop_) failed_ = true; break; }
            } else {
                // Never blend a mask with an unrelated colour frame: drop the orphan.
                int k = d->q[0].front().idx < d->q[1].front().idx ? 0 : 1;
                Log("unpaired %s frame %lld dropped", k == 0 ? "colour" : "alpha", (long long)d->q[k].front().idx);
                d->q[k].pop_front(); ++unpaired_;
            }
            continue;
        }
        // 2) end of file: drop leftovers, loop
        if (eof) {
            for (int k = 0; k < d->roles; ++k) {
                if (d->roles == 2 && !d->q[k].empty()) Log("%zu unpaired frames at EOF (role %d)", d->q[k].size(), k);
                unpaired_ += d->q[k].size();
                d->q[k].clear(); d->lastIdx[k] = -1;
            }
            int64_t n = clip_.frames > 0 ? clip_.frames : maxIdx + 1;
            if (n <= 0 || maxIdx < 0) { Log("no frames decoded"); failed_ = true; break; }
            clipFrames_ = n; loopBase += n; maxIdx = -1; skipBelow = -1; gate[0] = gate[1] = -1; ++loops_;
            d->mkv.Rewind();
            for (int k = 0; k < d->roles; ++k) d->dec[k].Flush();     // next Feed re-sends SPS/PPS with the IDR
            eof = false;
            continue;
        }
        // 3) demux + decode
        stage_ = "demux";
        if (!d->mkv.Read(pkt, e8)) {
            if (!e8.empty()) { Log("demux error: %s", e8.c_str()); failed_ = true; break; }
            bool ok = true; stage_ = "drain";
            for (int k = 0; k < d->roles && ok; ++k) { ok = d->dec[k].Drain(d->fresh, err); collect(k); }
            if (!ok) { Log("drain failed: %s", WToUtf8(err).c_str()); failed_ = true; break; }
            eof = true; continue;
        }
        int k = pkt.track >= 0 && pkt.track < 64 ? roleOf[pkt.track] : -1;
        if (k < 0) continue;
        if (gate[k] >= 0) { if (!pkt.key || gridIdx(pkt.ptsNs) < gate[k]) continue; gate[k] = -1; }
        stage_ = "decode"; ++fed_;
        bool ok = d->dec[k].Feed(pkt.data.data(), pkt.data.size(), pkt.ptsNs / 100, pkt.key, d->fresh, err);
        collect(k);
        if (!ok) { Log("decode error (role %d): %s", k, WToUtf8(err).c_str()); failed_ = true; break; }
    }
}

const FrameSlot* VideoSource::Acquire(int64_t target, int64_t* earliest) {
    std::lock_guard<std::mutex> lk(m_);
    int best = -1; int64_t e = -1;
    for (int i = 0; i < kSlots; ++i) {
        if (slots_[i].st != FrameSlot::Ready) continue;
        if (e < 0 || slots_[i].frame < e) e = slots_[i].frame;
        if (slots_[i].frame <= target && (best < 0 || slots_[i].frame > slots_[best].frame)) best = i;
    }
    if (earliest) *earliest = e;
    if (best >= 0) {
        for (int i = 0; i < kSlots; ++i)
            if (slots_[i].st == FrameSlot::Ready && slots_[i].frame < slots_[best].frame) { slots_[i].st = FrameSlot::Free; ++dropped_; }
        if (shown_ >= 0) slots_[shown_].st = FrameSlot::Free;
        slots_[best].st = FrameSlot::Shown; shown_ = best;
        cv_.notify_one();
    }
    return shown_ >= 0 ? &slots_[shown_] : nullptr;
}
