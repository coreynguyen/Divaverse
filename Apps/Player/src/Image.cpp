#include "Image.h"
#include <wincodec.h>
#include <shlobj.h>
#include <algorithm>
#include <cwctype>

static bool FromDecoder(IWICImagingFactory* f, IWICBitmapDecoder* dec, int maxDim, ImageBGRA& out) {
    ComPtr<IWICBitmapFrameDecode> fr; UINT w = 0, h = 0;
    if (FAILED(dec->GetFrame(0, &fr)) || FAILED(fr->GetSize(&w, &h)) || !w || !h || w > 16384 || h > 16384) return false;
    UINT dw = w, dh = h;
    if (maxDim > 0 && (int)std::max(w, h) > maxDim) { double k = (double)maxDim / std::max(w, h); dw = std::max(1u, (UINT)(w * k + .5)); dh = std::max(1u, (UINT)(h * k + .5)); }
    ComPtr<IWICBitmapSource> src = fr;
    if (dw != w || dh != h) {
        ComPtr<IWICBitmapScaler> sc;
        if (SUCCEEDED(f->CreateBitmapScaler(&sc)) && SUCCEEDED(sc->Initialize(fr.Get(), dw, dh, WICBitmapInterpolationModeFant))) src = sc;
        else { dw = w; dh = h; }
    }
    ComPtr<IWICFormatConverter> cv;
    if (FAILED(f->CreateFormatConverter(&cv)) ||
        FAILED(cv->Initialize(src.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return false;
    out.w = (int)dw; out.h = (int)dh; out.px.resize((size_t)dw * dh * 4);
    if (FAILED(cv->CopyPixels(nullptr, dw * 4, (UINT)out.px.size(), out.px.data()))) { out = ImageBGRA{}; return false; }
    return true;
}
static ComPtr<IWICImagingFactory> Factory() {
    ComPtr<IWICImagingFactory> f;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
    return f;
}
bool DecodeImageMem(const uint8_t* data, size_t n, int maxDim, ImageBGRA& out) {
    out = ImageBGRA{};
    if (!data || n < 8 || n > (32u << 20)) return false;
    auto f = Factory(); if (!f) return false;
    ComPtr<IWICStream> st; ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateStream(&st)) || FAILED(st->InitializeFromMemory((BYTE*)data, (DWORD)n))) return false;
    if (FAILED(f->CreateDecoderFromStream(st.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec))) return false;
    return FromDecoder(f.Get(), dec.Get(), maxDim, out);
}
bool DecodeImageFile(const std::wstring& path, int maxDim, ImageBGRA& out) {
    out = ImageBGRA{};
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) || fa.nFileSizeHigh || fa.nFileSizeLow > (32u << 20)) return false;
    auto f = Factory(); if (!f) return false;
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) return false;
    return FromDecoder(f.Get(), dec.Get(), maxDim, out);
}
bool DecodeImageResource(int id, int maxDim, ImageBGRA& out) {
    HMODULE m = GetModuleHandleW(nullptr);
    HRSRC r = FindResourceW(m, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA); if (!r) return false;
    HGLOBAL g = LoadResource(m, r); if (!g) return false;
    return DecodeImageMem((const uint8_t*)LockResource(g), SizeofResource(m, r), maxDim, out);
}

static std::wstring CacheDir() {
    std::wstring d = LogDir() + L"\\cache"; CreateDirectoryW(d.c_str(), nullptr); return d;
}
uint64_t CacheKey(const std::wstring& path) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { for (int i = 0; i < 8; ++i) { h ^= (v >> (i * 8)) & 255; h *= 1099511628211ull; } };
    for (wchar_t c : path) mix((uint64_t)towlower(c));
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) {
        mix(((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow); mix(((uint64_t)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime);
    }
    return h;
}
static std::wstring CacheFile(uint64_t key) { wchar_t b[40]; swprintf(b, 40, L"\\%016llx.thumb", (unsigned long long)key); return CacheDir() + b; }
bool CacheLoad(uint64_t key, ImageBGRA& out) {
    HANDLE f = CreateFileW(CacheFile(key).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    uint32_t hdr[3] = {}; DWORD rd = 0; bool ok = false;
    if (ReadFile(f, hdr, sizeof(hdr), &rd, nullptr) && rd == sizeof(hdr) && hdr[0] == 0x31545644 /* 'DVT1' */ && hdr[1] && hdr[2] && hdr[1] <= 4096 && hdr[2] <= 4096) {
        out.w = (int)hdr[1]; out.h = (int)hdr[2]; out.px.resize((size_t)out.w * out.h * 4);
        ok = ReadFile(f, out.px.data(), (DWORD)out.px.size(), &rd, nullptr) && rd == out.px.size();
    }
    CloseHandle(f);
    if (!ok) out = ImageBGRA{};
    return ok;
}
void CacheSave(uint64_t key, const ImageBGRA& img) {
    if (!img.ok()) return;
    HANDLE f = CreateFileW(CacheFile(key).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    uint32_t hdr[3] = { 0x31545644, (uint32_t)img.w, (uint32_t)img.h }; DWORD wr;
    WriteFile(f, hdr, sizeof(hdr), &wr, nullptr); WriteFile(f, img.px.data(), (DWORD)img.px.size(), &wr, nullptr);
    CloseHandle(f);
}
