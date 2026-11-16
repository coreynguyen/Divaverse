#pragma once
#include "Common.h"
#include "Shaders.h"
#include <d3d11_1.h>
#include <dxgi1_3.h>
#include <dcomp.h>

struct FrameSlot;

enum class PresentMode {
    DComp,     // flip-model swap chain (premultiplied alpha) bound via DirectComposition. GPU-only path.
    Layered    // fallback: render offscreen, read back, UpdateLayeredWindow. Per-pixel hit testing.
};

class Renderer {
public:
    ~Renderer();
    bool Init(HWND hwnd, PresentMode mode, bool wantVideo, std::wstring& err);
    void Shutdown();
    bool Resize(int w, int h);

    // slot may be null (draws only overlays). Returns false on device loss.
    bool Render(const FrameSlot* slot, bool dual, const ShaderCB& cb);
    void SetWindowPos(POINT pt) { pos_ = pt; }      // Layered mode needs the screen position
    void RequestCapture(const std::wstring& bmpPath) { capturePath_ = bmpPath; }   // next Render() -> 32bpp BMP
    bool CaptureDone() const { return captureDone_; }
    double CaptureCoverage() const { return captureCoverage_; }   // fraction of pixels with alpha > 0

    ID3D11Device*        Device()  const { return dev_.Get(); }
    ID3D11DeviceContext* Context() const { return ctx_.Get(); }
    CtxLock*             Lock()          { return &lock_; }
    bool                 VideoCapable() const { return video_; }
    PresentMode          Mode() const { return mode_; }
    HANDLE               FrameWaitable() const { return waitable_; }
    unsigned             PresentFailures() const { return presentFail_; }   // layered path: UpdateLayeredWindow errors
    int W() const { return w_; } int H() const { return h_; }
    std::string AdapterName() const { return adapter_; }

private:
    bool CreateDevice(bool wantVideo);
    bool CreateShaders(std::wstring& err);
    bool CreateTargets();
    bool PresentLayered();
    void DoCapture(ID3D11Texture2D* src);

    HWND hwnd_ = nullptr; PresentMode mode_ = PresentMode::DComp;
    int w_ = 0, h_ = 0; bool video_ = false; POINT pos_{};
    std::string adapter_;
    CtxLock lock_;
    ComPtr<ID3D11Device> dev_; ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<IDXGISwapChain1> swap_; HANDLE waitable_ = nullptr;
    ComPtr<IDCompositionDevice> dcomp_; ComPtr<IDCompositionTarget> target_; ComPtr<IDCompositionVisual> visual_;
    ComPtr<ID3D11Texture2D> rtTex_, staging_;          // layered mode
    ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<ID3D11VertexShader> vs_; ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11Buffer> cb_; ComPtr<ID3D11SamplerState> samp_; ComPtr<ID3D11BlendState> blend_;
    ComPtr<ID3D11ShaderResourceView> blackY_, blackUV_;
    std::wstring capturePath_; bool captureDone_ = false; double captureCoverage_ = 0;
    unsigned presentFail_ = 0, presentRun_ = 0; DWORD presentErr_ = 0;
    HDC memDC_ = nullptr; HBITMAP dib_ = nullptr; void* dibBits_ = nullptr; HGDIOBJ oldBmp_ = nullptr;
};
