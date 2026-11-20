#pragma once
// Audio output: WASAPI shared mode, event driven, float32 at the device mix format.
// A source is anything that fills interleaved float frames: the procedural synth (Synth.h) or a decoded
// file (FileSource, Media Foundation source reader resampling to the mix format).
// Every COM / MF object lives on the output thread.
#include "Common.h"
#include <thread>
#include <atomic>
#include <memory>

struct AudioSource {
    virtual ~AudioSource() = default;
    // Called once on the output thread before the first Render. false = cannot play.
    virtual bool Prepare(int rate, int channels, std::wstring& err) = 0;
    // Fill n interleaved frames. Return frames written; fewer than n = end of stream.
    virtual uint32_t Render(float* dst, uint32_t n) = 0;
    virtual void Shutdown() {}
    virtual double Duration() { return 0; }      // seconds, 0 = unknown / endless (asked after Prepare, on the output thread)
};

class AudioOut {
public:
    ~AudioOut() { Close(); }
    // nullSink: no device; the source is pulled as fast as possible at 48 kHz stereo (verification).
    bool Open(std::unique_ptr<AudioSource> src, float volume, bool nullSink, std::wstring& err);
    void Close();
    void Pause(bool on);
    void SetVolume(float v);
    void SetNormalize(bool on) { normalize_ = on; }      // rides the gain toward a common loudness (quiet songs up, loud songs down)
    double Seconds() const { return rate_ ? (double)frames_.load() / rate_ : 0; }
    bool Done() const { return eof_.load(); }
    bool Failed() const { return failed_.load(); }
    uint64_t Frames() const { return frames_.load(); }
    float Level() const { return level_.load(); }          // recent output peak 0..1 (for the visualiser)
    static constexpr int kBands = 16;
    float Band(int i) const { return i >= 0 && i < kBands ? bands_[i].load() : 0.f; }      // spectrum 60 Hz .. 14 kHz, log spaced, 0..1 with decay
    double Duration() const { return dur_.load(); }
    void Fade(float to, float seconds) { fadeTime_ = seconds > 0.01f ? seconds : 0.01f; fadeGoal_ = to; }      // multiplies the volume; ramps on the output thread
    void PresetFade(float v) { fadeCur_ = v; fadeGoal_ = v; }                                                   // before Open: start silent for a fade-in
    int SampleRate() const { return rate_; } int Channels() const { return ch_; }
    AudioSource* Source() { return src_.get(); }

private:
    void ThreadMain();
    bool Init(std::wstring& err); bool OpenDevice(int keepRate, int keepCh, std::wstring& err); bool Reopen(const char* why);
    struct Impl; Impl* d = nullptr;
    std::unique_ptr<AudioSource> src_; bool nullSink_ = false;
    std::thread th_;
    HANDLE ready_ = nullptr; bool initOk_ = false; std::wstring initErr_;
    std::atomic<bool> run_{false}, eof_{false}, failed_{false}, devChanged_{false}, paused_{false}, pauseDirty_{false}, volDirty_{false};
    std::atomic<bool> normalize_{false}; float gain_ = 0, loud_ = 0.14f, normGain_ = 1;
    std::atomic<float> vol_{1.f}, level_{0.f}; std::atomic<uint64_t> frames_{0};
    std::atomic<float> bands_[kBands] = {}; std::atomic<double> dur_{0}; std::atomic<float> fadeGoal_{1.f}, fadeTime_{1.f}; float fadeCur_ = 1.f;
    int rate_ = 0, ch_ = 0;
};

// Decoded audio file (MP3, AAC/M4A, WAV, FLAC, WMA: whatever the OS decodes).
std::unique_ptr<AudioSource> MakeFileSource(const std::wstring& path);
