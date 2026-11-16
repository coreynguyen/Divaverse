#include "Audio.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <initguid.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

DEFINE_GUID(DV_CLSID_MMDeviceEnumerator, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
DEFINE_GUID(DV_IID_IMMDeviceEnumerator, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
DEFINE_GUID(DV_IID_IAudioClient, 0x1CB9AD4C, 0xDBFA, 0x4C32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);
DEFINE_GUID(DV_IID_IAudioRenderClient, 0xF294ACFC, 0x3146, 0x4483, 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2);
DEFINE_GUID(DV_IID_ISimpleAudioVolume, 0x87CE5498, 0x68D6, 0x44E5, 0x92, 0x15, 0x6D, 0xA4, 0x7E, 0xF8, 0x83, 0xD8);
DEFINE_GUID(DV_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);

namespace { std::wstring Hx(HRESULT h) { wchar_t b[16]; swprintf(b, 16, L"0x%08X", (unsigned)h); return b; } }

struct AudioOut::Impl {
    ComPtr<IAudioClient> client; ComPtr<IAudioRenderClient> render; ComPtr<ISimpleAudioVolume> volume;
    HANDLE event = nullptr; UINT32 bufFrames = 0;
    bool mf = false, com = false, started = false;
};

bool AudioOut::Open(std::unique_ptr<AudioSource> src, float volume, bool nullSink, std::wstring& err) {
    Close();
    src_ = std::move(src); nullSink_ = nullSink; vol_ = volume; gain_ = volume * volume; eof_ = false; failed_ = false; frames_ = 0; paused_ = false;
    d = new Impl;
    ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    d->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    run_ = true;
    th_ = std::thread([this] { ThreadMain(); });
    WaitForSingleObject(ready_, INFINITE);
    if (!initOk_) { err = initErr_; Close(); return false; }
    return true;
}

void AudioOut::Close() {
    run_ = false;
    if (d && d->event) SetEvent(d->event);
    if (th_.joinable()) th_.join();
    if (ready_) { CloseHandle(ready_); ready_ = nullptr; }
    if (d && d->event) CloseHandle(d->event);
    delete d; d = nullptr; src_.reset();
}

void AudioOut::Pause(bool on) { paused_ = on; pauseDirty_ = true; if (d && d->event) SetEvent(d->event); }
void AudioOut::SetVolume(float v) { vol_ = v < 0 ? 0 : v > 1 ? 1 : v; volDirty_ = true; if (d && d->event) SetEvent(d->event); }

bool AudioOut::Init(std::wstring& err) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); d->com = SUCCEEDED(hr);
    if (FAILED(hr = MFStartup(MF_VERSION, MFSTARTUP_LITE))) { err = L"MFStartup " + Hx(hr); return false; }
    d->mf = true;
    if (nullSink_) { rate_ = 48000; ch_ = 2; return src_->Prepare(rate_, ch_, err); }

    ComPtr<IMMDeviceEnumerator> en; ComPtr<IMMDevice> dev;
    if (FAILED(hr = CoCreateInstance(DV_CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, DV_IID_IMMDeviceEnumerator, (void**)en.GetAddressOf())) ||
        FAILED(hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) ||
        FAILED(hr = dev->Activate(DV_IID_IAudioClient, CLSCTX_ALL, nullptr, (void**)d->client.GetAddressOf()))) {
        err = L"no audio output device (" + Hx(hr) + L")"; return false; }
    WAVEFORMATEX* mix = nullptr;
    if (FAILED(hr = d->client->GetMixFormat(&mix)) || !mix) { err = L"GetMixFormat " + Hx(hr); return false; }
    // Render straight at the engine's rate and channel count: no resampler in the path. The stream itself is
    // plain float32; AUTOCONVERTPCM lets the engine take it even when its mix format is extensible / >2 ch layout.
    int rate = (int)mix->nSamplesPerSec, ch = (int)mix->nChannels;
    CoTaskMemFree(mix);
    WAVEFORMATEXTENSIBLE wf{};
    wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE; wf.Format.nChannels = (WORD)ch; wf.Format.nSamplesPerSec = (DWORD)rate;
    wf.Format.wBitsPerSample = 32; wf.Format.nBlockAlign = (WORD)(ch * 4); wf.Format.nAvgBytesPerSec = (DWORD)(rate * ch * 4);
    wf.Format.cbSize = 22; wf.Samples.wValidBitsPerSample = 32;
    wf.dwChannelMask = ch == 1 ? 0x4 : ch == 2 ? 0x3 : 0;                 // mono / stereo; 0 = unspecified for others
    wf.SubFormat = DV_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    if (FAILED(hr = d->client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 1000000 /* 100 ms */, 0, &wf.Format, nullptr))) { err = L"IAudioClient::Initialize " + Hx(hr); return false; }
    d->client->SetEventHandle(d->event);
    d->client->GetBufferSize(&d->bufFrames);
    if (FAILED(hr = d->client->GetService(DV_IID_IAudioRenderClient, (void**)d->render.GetAddressOf()))) { err = L"IAudioRenderClient " + Hx(hr); return false; }
    d->client->GetService(DV_IID_ISimpleAudioVolume, (void**)d->volume.GetAddressOf());
    if (d->volume) d->volume->SetMasterVolume(1.0f, nullptr);      // session volume is process-wide: leave it at unity, gain is per stream
    rate_ = rate; ch_ = ch;
    return src_->Prepare(rate_, ch_, err);
}

void AudioOut::ThreadMain() {
    std::wstring err;
    initOk_ = Init(err); initErr_ = err;
    SetEvent(ready_);
    if (initOk_) {
        std::vector<float> scratch;
        auto meter = [&](const float* p, uint32_t n) {
            float pk = 0; for (uint32_t i = 0; i < n * (uint32_t)ch_; i += 7) pk = std::max(pk, std::fabs(p[i]));
            level_ = std::max(pk, level_.load() * 0.85f);
        };
        if (!nullSink_) { d->client->Start(); d->started = true; }
        while (run_) {
            if (nullSink_) {
                if (eof_) { WaitForSingleObject(d->event, 100); continue; }
                scratch.resize(4096 * (size_t)ch_);
                uint32_t got = src_->Render(scratch.data(), 4096);
                frames_ += got; meter(scratch.data(), got);
                if (got < 4096) eof_ = true;
                continue;
            }
            WaitForSingleObject(d->event, 100);
            if (!run_) break;
            if (pauseDirty_.exchange(false)) { if (paused_) d->client->Stop(); else d->client->Start(); }
            if (paused_ || eof_) continue;
            UINT32 pad = 0;
            if (FAILED(d->client->GetCurrentPadding(&pad))) { failed_ = true; break; }       // device removed / invalidated
            UINT32 avail = d->bufFrames - pad;
            if (!avail) continue;
            BYTE* dst = nullptr;
            if (FAILED(d->render->GetBuffer(avail, &dst)) || !dst) { failed_ = true; break; }
            uint32_t got = src_->Render((float*)dst, avail);
            if (got < avail) { memset((float*)dst + (size_t)got * ch_, 0, (size_t)(avail - got) * ch_ * sizeof(float)); eof_ = true; }
            {   // per-stream gain: perceptual volume curve, optional loudness levelling, soft ceiling
                float* f = (float*)dst; size_t n = (size_t)got * ch_; float target = vol_.load(); target *= target;
                if (normalize_.load() && n) {
                    double sq = 0; for (size_t i = 0; i < n; ++i) sq += (double)f[i] * f[i];
                    float rms = (float)std::sqrt(sq / n), secs = (float)got / (float)rate_;
                    if (rms > 0.003f) loud_ += (rms - loud_) * std::min(1.f, secs / (rms > loud_ ? 1.0f : 4.0f));      // gated, quick to rise, slow to fall
                    float want = std::clamp(0.14f / std::max(loud_, 0.01f), 0.35f, 3.2f);                              // aim at about -17 dBFS RMS
                    normGain_ += (want - normGain_) * std::min(1.f, secs / 1.5f); target *= normGain_;
                }
                float g0 = gain_, step = n ? (target - g0) / (float)got : 0;
                for (uint32_t i = 0; i < got; ++i) { g0 += step; for (int c = 0; c < ch_; ++c) { float x = f[(size_t)i * ch_ + c] * g0; f[(size_t)i * ch_ + c] = std::fabs(x) > 0.8f ? (x > 0 ? 1.f : -1.f) * (0.8f + 0.2f * std::tanh((std::fabs(x) - 0.8f) / 0.2f)) : x; } }
                gain_ = target;
            }
            meter((const float*)dst, got);
            d->render->ReleaseBuffer(avail, 0);
            frames_ += got;
        }
    }
    if (d->started) d->client->Stop();
    if (src_) src_->Shutdown();
    d->volume.Reset(); d->render.Reset(); d->client.Reset();
    if (d->mf) MFShutdown();
    if (d->com) CoUninitialize();
}

// ------------------------------------------------------------------ file source
namespace {
struct FileSource : AudioSource {
    std::wstring path; ComPtr<IMFSourceReader> reader; int ch = 2;
    std::vector<float> fifo; size_t fifoAt = 0; bool ended = false;
    bool Prepare(int rate, int channels, std::wstring& err) override {
        ch = channels;
        HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader);
        if (FAILED(hr)) { err = L"cannot open audio file (" + Hx(hr) + L")"; return false; }
        reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
        reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
        ComPtr<IMFMediaType> t; MFCreateMediaType(&t);                     // decode + resample to the device format
        t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio); t->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
        t->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)rate); t->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)channels);
        t->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32); t->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, (UINT32)channels * 4);
        t->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32)(rate * channels * 4));
        if (FAILED(hr = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, t.Get()))) {
            err = L"no decodable audio stream (" + Hx(hr) + L")"; return false; }
        return true;
    }
    uint32_t Render(float* dst, uint32_t n) override {
        uint32_t done = 0;
        while (done < n) {
            size_t have = (fifo.size() - fifoAt) / (size_t)ch;
            if (have) {
                uint32_t k = (uint32_t)std::min<size_t>(have, n - done);
                memcpy(dst + (size_t)done * ch, fifo.data() + fifoAt, (size_t)k * ch * sizeof(float));
                fifoAt += (size_t)k * ch; done += k; continue;
            }
            if (ended) break;
            fifo.clear(); fifoAt = 0;
            DWORD fl = 0; ComPtr<IMFSample> s;
            HRESULT hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &fl, nullptr, &s);
            if (FAILED(hr) || (fl & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR))) { ended = true; if (!s) break; }
            if (!s) continue;
            ComPtr<IMFMediaBuffer> b; BYTE* p = nullptr; DWORD len = 0;
            if (FAILED(s->ConvertToContiguousBuffer(&b)) || FAILED(b->Lock(&p, nullptr, &len))) continue;
            fifo.assign((const float*)p, (const float*)p + len / sizeof(float));
            b->Unlock();
        }
        return done;
    }
    void Shutdown() override { reader.Reset(); }
};
}
std::unique_ptr<AudioSource> MakeFileSource(const std::wstring& path) { auto s = std::make_unique<FileSource>(); s->path = path; return s; }
