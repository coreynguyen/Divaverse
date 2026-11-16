#include "Renderer.h"
#include "VideoSource.h"
#include <d3dcompiler.h>
#include <cstring>
#include <algorithm>

Renderer::~Renderer() { Shutdown(); }

void Renderer::Shutdown() {
    if (ctx_) { std::lock_guard<CtxLock> g(lock_); ctx_->ClearState(); ctx_->Flush(); }
    if (memDC_) { SelectObject(memDC_, oldBmp_); DeleteDC(memDC_); memDC_ = nullptr; }
    if (dib_) { DeleteObject(dib_); dib_ = nullptr; }
    if (waitable_) { CloseHandle(waitable_); waitable_ = nullptr; }
    rtv_.Reset(); rtTex_.Reset(); staging_.Reset();
    visual_.Reset(); target_.Reset(); dcomp_.Reset(); swap_.Reset();
    vs_.Reset(); ps_.Reset(); cb_.Reset(); samp_.Reset(); blend_.Reset(); blackY_.Reset(); blackUV_.Reset();
    ctx_.Reset(); dev_.Reset();
}

bool Renderer::CreateDevice(bool wantVideo) {
    const D3D_FEATURE_LEVEL fl[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    HRESULT hr = E_FAIL;
    for (int attempt = 0; attempt < 4 && FAILED(hr); ++attempt) {
        UINT f = flags;
        bool vid = wantVideo && attempt < 2;
        if (vid) f |= D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
        if (attempt & 1) f &= ~D3D11_CREATE_DEVICE_DEBUG;          // debug layer may be absent
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, f, fl, ARRAYSIZE(fl),
                               D3D11_SDK_VERSION, &dev_, nullptr, &ctx_);
        if (SUCCEEDED(hr)) video_ = vid;
    }
    if (FAILED(hr)) return false;
    ComPtr<ID3D10Multithread> mt;
    if (SUCCEEDED(ctx_.As(&mt))) mt->SetMultithreadProtected(TRUE);   // decoder thread + render thread
    ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> ad; DXGI_ADAPTER_DESC ds{};
    if (SUCCEEDED(dev_.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&ad)) && SUCCEEDED(ad->GetDesc(&ds)))
        adapter_ = WToUtf8(ds.Description);
    dxgi->SetGPUThreadPriority(0);
    return true;
}

bool Renderer::CreateShaders(std::wstring& err) {
    ComPtr<ID3DBlob> vsb, psb, e;
    UINT f = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    if (FAILED(D3DCompile(kShaderSrc, sizeof(kShaderSrc) - 1, "divaverse.hlsl", nullptr, nullptr, "VSMain", "vs_4_0", f, 0, &vsb, &e))) {
        err = L"VS compile: " + Utf8ToW(e ? (const char*)e->GetBufferPointer() : "?"); return false; }
    if (FAILED(D3DCompile(kShaderSrc, sizeof(kShaderSrc) - 1, "divaverse.hlsl", nullptr, nullptr, "PSMain", "ps_4_0", f, 0, &psb, &e))) {
        err = L"PS compile: " + Utf8ToW(e ? (const char*)e->GetBufferPointer() : "?"); return false; }
    if (FAILED(dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs_))) return false;
    if (FAILED(dev_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps_))) return false;

    D3D11_BUFFER_DESC bd{ sizeof(ShaderCB), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
    if (FAILED(dev_->CreateBuffer(&bd, nullptr, &cb_))) return false;
    D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(dev_->CreateSamplerState(&sd, &samp_))) return false;
    D3D11_BLEND_DESC bl{}; bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; // opaque write: shader outputs premultiplied
    if (FAILED(dev_->CreateBlendState(&bl, &blend_))) return false;

    // 1x1 placeholders so the shader samples valid views with no video bound (Y=16 -> alpha 0)
    auto tex1 = [&](DXGI_FORMAT fmt, const void* px, UINT pitch, ComPtr<ID3D11ShaderResourceView>& srv) {
        D3D11_TEXTURE2D_DESC td{ 1, 1, 1, 1, fmt, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        D3D11_SUBRESOURCE_DATA init{ px, pitch, 0 }; ComPtr<ID3D11Texture2D> t;
        return SUCCEEDED(dev_->CreateTexture2D(&td, &init, &t)) && SUCCEEDED(dev_->CreateShaderResourceView(t.Get(), nullptr, &srv));
    };
    uint8_t y = 16, uv[2] = { 128, 128 };
    return tex1(DXGI_FORMAT_R8_UNORM, &y, 1, blackY_) && tex1(DXGI_FORMAT_R8G8_UNORM, uv, 2, blackUV_);
}

bool Renderer::Init(HWND hwnd, PresentMode mode, bool wantVideo, std::wstring& err) {
    hwnd_ = hwnd; mode_ = mode;
    RECT rc; GetClientRect(hwnd, &rc); w_ = std::max<int>(1, rc.right); h_ = std::max<int>(1, rc.bottom);
    if (!CreateDevice(wantVideo)) { err = L"D3D11 device creation failed"; return false; }
    if (!CreateShaders(err)) { if (err.empty()) err = L"shader/state creation failed"; return false; }

    if (mode_ == PresentMode::DComp) {
        ComPtr<IDXGIDevice> dxgi; dev_.As(&dxgi);
        ComPtr<IDXGIAdapter> ad; dxgi->GetAdapter(&ad);
        ComPtr<IDXGIFactory2> fac; ad->GetParent(IID_PPV_ARGS(&fac));
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = w_; sd.Height = h_; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2;
        sd.Scaling = DXGI_SCALING_STRETCH; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        sd.Flags = 0;
        HRESULT hr = fac->CreateSwapChainForComposition(dev_.Get(), &sd, nullptr, &swap_);
        auto fail = [&](const wchar_t* what, HRESULT h) { wchar_t b[96]; swprintf(b, 96, L"%ls failed (0x%08X)", what, (unsigned)h); err = b; Log("renderer: %s", WToUtf8(err).c_str()); return false; };
        if (FAILED(hr)) return fail(L"CreateSwapChainForComposition", hr);
        if (FAILED(hr = DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&dcomp_)))) return fail(L"DCompositionCreateDevice", hr);
        if (FAILED(hr = dcomp_->CreateTargetForHwnd(hwnd_, TRUE, &target_))) return fail(L"CreateTargetForHwnd", hr);
        if (FAILED(hr = dcomp_->CreateVisual(&visual_))) return fail(L"CreateVisual", hr);
        if (FAILED(hr = visual_->SetContent(swap_.Get()))) return fail(L"IDCompositionVisual::SetContent", hr);
        if (FAILED(hr = target_->SetRoot(visual_.Get()))) return fail(L"IDCompositionTarget::SetRoot", hr);
        if (FAILED(hr = dcomp_->Commit())) return fail(L"IDCompositionDevice::Commit", hr);
    }
    if (!CreateTargets()) { err = L"render target creation failed"; return false; }
    Log("renderer: %s, adapter '%s', video=%d", mode_ == PresentMode::DComp ? "DirectComposition" : "Layered", adapter_.c_str(), (int)video_);
    return true;
}

bool Renderer::CreateTargets() {
    std::lock_guard<CtxLock> g(lock_);
    rtv_.Reset();
    if (mode_ == PresentMode::DComp) {
        ComPtr<ID3D11Texture2D> bb;
        if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&bb)))) return false;
        return SUCCEEDED(dev_->CreateRenderTargetView(bb.Get(), nullptr, &rtv_));
    }
    rtTex_.Reset(); staging_.Reset();
    D3D11_TEXTURE2D_DESC td{ (UINT)w_, (UINT)h_, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &rtTex_))) return false;
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &staging_))) return false;
    if (FAILED(dev_->CreateRenderTargetView(rtTex_.Get(), nullptr, &rtv_))) return false;
    if (memDC_) { SelectObject(memDC_, oldBmp_); DeleteDC(memDC_); memDC_ = nullptr; }
    if (dib_) { DeleteObject(dib_); dib_ = nullptr; }
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w_; bi.bmiHeader.biHeight = -h_;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HDC sdc = GetDC(nullptr);
    memDC_ = CreateCompatibleDC(sdc);
    dib_ = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &dibBits_, nullptr, 0);
    ReleaseDC(nullptr, sdc);
    if (!dib_) return false;
    oldBmp_ = SelectObject(memDC_, dib_);
    return true;
}

bool Renderer::Resize(int w, int h) {
    w = std::max(1, w); h = std::max(1, h);
    if (w == w_ && h == h_) return true;
    w_ = w; h_ = h;
    if (mode_ == PresentMode::DComp) {
        { std::lock_guard<CtxLock> g(lock_); rtv_.Reset(); ctx_->OMSetRenderTargets(0, nullptr, nullptr); ctx_->Flush(); }
        HRESULT hr = swap_->ResizeBuffers(0, w_, h_, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) { Log("ResizeBuffers failed 0x%08X", (unsigned)hr); return false; }
    }
    return CreateTargets();
}

bool Renderer::Render(const FrameSlot* slot, bool dual, const ShaderCB& cbv) {
    {
        std::lock_guard<CtxLock> g(lock_);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx_->Map(cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, &cbv, sizeof(cbv)); ctx_->Unmap(cb_.Get(), 0); }
        ID3D11ShaderResourceView* srv[3] = { blackY_.Get(), blackUV_.Get(), blackY_.Get() };
        if (slot && slot->p[0].srvY) {
            srv[0] = slot->p[0].srvY.Get(); srv[1] = slot->p[0].srvUV.Get();
            srv[2] = (dual && slot->p[1].srvY) ? slot->p[1].srvY.Get() : slot->p[0].srvY.Get();
        }
        float clear[4] = { 0, 0, 0, 0 };
        ctx_->ClearRenderTargetView(rtv_.Get(), clear);
        D3D11_VIEWPORT vp{ 0, 0, (float)w_, (float)h_, 0, 1 };
        ctx_->RSSetViewports(1, &vp);
        ctx_->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);
        ctx_->OMSetBlendState(blend_.Get(), nullptr, 0xFFFFFFFF);
        ctx_->IASetInputLayout(nullptr);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->VSSetShader(vs_.Get(), nullptr, 0);
        ctx_->PSSetShader(ps_.Get(), nullptr, 0);
        ctx_->PSSetConstantBuffers(0, 1, cb_.GetAddressOf());
        ctx_->PSSetSamplers(0, 1, samp_.GetAddressOf());
        ctx_->PSSetShaderResources(0, 3, srv);
        ctx_->Draw(3, 0);
        ID3D11ShaderResourceView* nul[3] = {};
        ctx_->PSSetShaderResources(0, 3, nul);   // release slot textures for the decoder
        if (!capturePath_.empty()) {
            ComPtr<ID3D11Resource> res; rtv_->GetResource(&res);
            ComPtr<ID3D11Texture2D> t; res.As(&t);
            DoCapture(t.Get());
        }
    }
    if (mode_ == PresentMode::Layered) return PresentLayered();
    // Sync interval 0: never blocks the UI thread on vblank. DWM composes the newest buffer at its own cadence and
    // we present at clip rate (<= refresh), so nothing is torn or skipped.
    double t0 = NowSeconds();
    HRESULT hr = swap_->Present(0, 0);
    double dt = NowSeconds() - t0;
    if (dt > 0.1) Log("slow Present: %.0f ms (hr 0x%08X)", dt * 1000, (unsigned)hr);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        Log("device lost: 0x%08X reason 0x%08X", (unsigned)hr, (unsigned)dev_->GetDeviceRemovedReason());
        return false;
    }
    return true;
}

bool Renderer::PresentLayered() {
    // Readback path: costs one GPU->CPU copy per presented frame. Used only when DComp is unavailable
    // or explicitly requested (--layered). Video frames still arrive at 24-60 Hz, not display rate.
    {
        std::lock_guard<CtxLock> g(lock_);
        ctx_->CopyResource(staging_.Get(), rtTex_.Get());
        D3D11_MAPPED_SUBRESOURCE m;
        HRESULT hr = ctx_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &m);
        if (FAILED(hr)) return hr != DXGI_ERROR_DEVICE_REMOVED;
        for (int y = 0; y < h_; ++y)
            memcpy((uint8_t*)dibBits_ + (size_t)y * w_ * 4, (uint8_t*)m.pData + (size_t)y * m.RowPitch, (size_t)w_ * 4);
        ctx_->Unmap(staging_.Get(), 0);
    }
    SIZE sz{ w_, h_ }; POINT src{ 0, 0 };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };     // DIB is premultiplied BGRA
    if (UpdateLayeredWindow(hwnd_, nullptr, &pos_, &sz, memDC_, &src, 0, &bf, ULW_ALPHA)) { presentRun_ = 0; return true; }
    // A failed update is not a lost device (the caller would rebuild everything), but it must not pass silently:
    // it is counted, logged when the error changes, and shown in the diagnostics.
    DWORD e = GetLastError();
    if (!presentFail_++ || e != presentErr_) Log("UpdateLayeredWindow failed: error %lu (%dx%d)", e, w_, h_);
    presentErr_ = e;
    // A few failures in a row are treated as transient (display mode switch, session lock). A long run of them is
    // reported as a failed present so the caller rebuilds the renderer instead of showing nothing forever.
    if (++presentRun_ < 90) return true;
    presentRun_ = 0; Log("layered presentation failed 90 times in a row -> renderer reset");
    return false;
}

void Renderer::DoCapture(ID3D11Texture2D* src) {   // caller holds lock_
    D3D11_TEXTURE2D_DESC td; src->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> st;
    std::wstring path; path.swap(capturePath_);
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &st))) return;
    ctx_->CopyResource(st.Get(), src);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx_->Map(st.Get(), 0, D3D11_MAP_READ, 0, &m))) return;
    BITMAPFILEHEADER fh{}; BITMAPV5HEADER bh{};
    bh.bV5Size = sizeof(bh); bh.bV5Width = (LONG)td.Width; bh.bV5Height = -(LONG)td.Height; bh.bV5Planes = 1; bh.bV5BitCount = 32;
    bh.bV5Compression = BI_BITFIELDS; bh.bV5RedMask = 0x00FF0000; bh.bV5GreenMask = 0x0000FF00; bh.bV5BlueMask = 0x000000FF;
    bh.bV5AlphaMask = 0xFF000000; bh.bV5CSType = 0x73524742; /* LCS_sRGB */ bh.bV5SizeImage = td.Width * td.Height * 4;
    fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(bh); fh.bfSize = fh.bfOffBits + bh.bV5SizeImage;
    size_t covered = 0;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD wr; WriteFile(f, &fh, sizeof(fh), &wr, nullptr); WriteFile(f, &bh, sizeof(bh), &wr, nullptr);
        for (UINT y = 0; y < td.Height; ++y) {
            const uint8_t* row = (const uint8_t*)m.pData + (size_t)y * m.RowPitch;
            for (UINT x = 0; x < td.Width; ++x) covered += row[4 * x + 3] > 0;
            WriteFile(f, row, td.Width * 4, &wr, nullptr);
        }
        CloseHandle(f);
    }
    ctx_->Unmap(st.Get(), 0);
    captureCoverage_ = (double)covered / ((double)td.Width * td.Height);
    captureDone_ = true;
    Log("captured %ux%u -> %s (alpha coverage %.3f)", td.Width, td.Height, WToUtf8(path).c_str(), captureCoverage_);
}
