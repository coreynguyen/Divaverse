#pragma once
// Content library. The unit is one self-contained MKV per clip; clips are grouped into Divas by the diva_id in
// their metadata. Legacy show folders (manifest.json) are still read. Covers and thumbnails are resolved on a
// worker thread:  sidecar artwork  >  embedded attachment  >  image beside the media  >  frame decoded from the video.
#include "Common.h"
#include "Clip.h"
#include "Meta.h"
#include "Image.h"
#include <thread>
#include <atomic>

struct LibClip {
    ClipDesc desc; MediaInfo info;
    std::wstring title, folderArt;                // folderArt: directory searched for cover images (legacy shows)
    double seconds = 0; uint64_t added = 0;       // file time, for "recently added"
    ImageBGRA thumb; bool thumbReady = false, thumbFailed = false, thumbIsArt = false;   // thumbIsArt: authored artwork, not a decoded frame
    int diva = -1;
};
struct LibDiva {
    std::wstring key, name;                       // key = diva_id (or a synthetic one for loose files)
    std::vector<int> clips;
    std::vector<std::pair<std::wstring, std::wstring>> profile; int rating = 0;
    ImageBGRA cover; bool coverReady = false, coverTried = false; std::wstring coverSource;
    ImageBGRA banner; bool bannerReady = false;   // tall art for the diva page (attachment banner.png|jpg, or a file beside the media)
    double seconds = 0; uint64_t added = 0;
};
struct LibProblem { std::wstring path; std::string what; };

class Library {
public:
    ~Library() { Unwatch(); Stop(); }
    // Scan (container headers + metadata only) and then artwork, both on a worker thread. The previous results stay
    // visible until the new scan is complete and swapped in; `msg` is posted to `notify` then and whenever artwork
    // arrives. All accessors below must be used under Mutex().
    void Rescan(const std::vector<std::wstring>& folders, HWND notify, UINT msg);
    void Stop();
    bool Scanning() const { return scanning_; }
    bool WaitScan(double seconds);                // QA: block until the scan part is done
    double LastScanSeconds() const { return lastScanS_; }
    // Watches the folders (and their subfolders) for clips or artwork being added, removed or changed; after two quiet
    // seconds `msg` is posted to `notify` once. Calling it again with the same folders keeps the running watch.
    void Watch(const std::vector<std::wstring>& folders, HWND notify, UINT msg);
    void Unwatch();
    std::mutex& Mutex() { return mx_; }
    std::vector<LibClip>& Clips() { return clips_; }
    std::vector<LibDiva>& Divas() { return divas_; }
    const std::vector<LibProblem>& Problems() const { return problems_; }
    int Generation() const { return gen_; }
    int FindClip(const std::wstring& path) const;
    Show MakeShow(int diva) const;                // playable description of a diva's clips
private:
    void ScanDir(const std::wstring& dir, int depth);
    void AddManifest(const std::wstring& dir);
    void AddMkv(const std::wstring& file, const ClipDesc* legacy, const Show* show, const std::wstring& artDir);
    void Group();
    void Worker(HWND notify, UINT msg, int gen);
    std::vector<LibClip> clips_; std::vector<LibDiva> divas_; std::vector<LibProblem> problems_;
    std::vector<Show> shows_;                     // legacy manifests met during the scan (diva names / profiles)
    std::vector<int> clipShow_;
    std::mutex mx_; std::thread th_; std::atomic<bool> stop_{false}; int gen_ = 0;
    std::atomic<bool>* abort_ = nullptr;          // the owning library's stop flag, while scanning into a scratch instance
    std::atomic<bool> scanning_{false}; double lastScanS_ = 0;
    std::thread wth_; HANDLE wstop_ = nullptr; std::vector<std::wstring> watched_;
};

// One frame of a clip as premultiplied BGRA, scaled to targetH. Must run on a thread with COM + MFStartup.
bool DecodePoster(const ClipDesc& clip, int targetH, int frameNo, std::vector<uint8_t>& bgra, int& w, int& h, std::wstring& err);
