#pragma once
// Application core: the desktop overlay (performer window), playback, settings, tray. The launcher window (Ui.cpp)
// and the tray flyout (Flyout.cpp) drive it through the public members below. Everything runs on the UI thread
// except decode (VideoSource), posters (Library) and audio (AudioOut).
#include "Common.h"
#include "UserData.h"
#include "Clip.h"
#include "VideoSource.h"
#include "Renderer.h"
#include "Music.h"
#include "Library.h"
#include <memory>
#include <climits>

enum Cmd : UINT {
    ID_OPEN_UI = 1000, ID_PLAYPAUSE, ID_NEXT, ID_PREV, ID_SHOWHIDE, ID_EDIT, ID_MUSIC_TOGGLE, ID_MUSIC_NEXT, ID_UNSTAGE, ID_EXIT,
    ID_LOCK, ID_CLICKTHROUGH, ID_PANIC, ID_MUSIC_PREV, ID_MUSIC_PAUSE,
};
constexpr UINT WM_APP_TRAY = WM_APP + 1, WM_APP_POSTER = WM_APP + 2, WM_APP_SHOWUI = WM_APP + 3;

struct Settings {
    int x = INT_MIN, bottom = INT_MIN;     // where the clip's ground anchor sits on screen: x, and the y of the floor line
    float scale = 1.0f, opacity = 1.0f;    // scale = stage scale: 1.0 shows one metre as kStagePxPerMetre DIPs
    int wrLook = 0x0001;                   // Wet-Rash rider: skin | bikini<<4 | ski<<8 | hair<<12 | hair style<<16
    bool clickThrough = false, lock = false, gravity = true, seasonal = true, editor = false;
    int loopMode = 0;                      // 0 repeat clip, 1 play the diva's clips in order, 2 shuffle
    int cardMode = 1; float zoom = 1.0f;   // library: 0 small square tiles, 1 large cards
    std::wstring lastStall;                // last watchdog report on this machine (survives restarts)
    std::vector<std::wstring> favs, recent, queue;   // queue: clip paths to play next, in order
    bool mirror = false, topmost = true, shadow = true, autoHide = true, fx = true, sfx = true; float sfxVol = 0.5f;
    int theme = 2;                         // ThemeId (new installs start on the neon dark theme)
    bool music = false, musicShuffle = false; bool musicNormalize = true; float musicVolume = 0.6f; int musicMode = 0, station = 0;
    int decodeMode = 0;                    // 0 auto, 1 GPU, 2 CPU
    bool hwBad = false, dcompBad = false;  // learned by the playback watchdog on this machine
    std::wstring last; std::vector<std::wstring> folders;
    bool noSave = false;
    void Load(); void Save() const;
};

class App {
public:
    int Run(HINSTANCE hi, int argc, wchar_t** argv);

    // ---- state for the UI
    Settings set; Music music; Library lib;
    Show show; int clipIdx = 0;
    bool OnStage() const { return src_ != nullptr; }
    bool Paused() const { return paused_; } bool Hidden() const { return hidden_ || panic_; } bool Editing() const { return edit_; }
    bool MusicPaused() const { return musicPaused_; }
    const ClipDesc* Cur() const { return src_ && clipIdx < (int)show.clips.size() ? &show.clips[clipIdx] : nullptr; }
    UserData user; bool qaQuiet = false; bool SelfTesting() const { return !selftest_.empty() || qaQuiet; }
    // Editor mode: change the staged clip's authored scale live and store it as a sidecar override (never in the MKV).
    void EditPhysical(double ppm, double actorHeightM, float anchorU, float groundV); bool SaveSidecarPhysical(std::wstring& err);
    MediaInfo stageInfo;                   // container facts + metadata of the clip on stage
    bool Calibrated() const;               // the clip carries a physical scale
    double StageHeightCm() const;          // performer height as shown, in "stage" centimetres (0 if unknown)
    float Progress() const;                // 0..1 inside the current clip
    int64_t Frame() const; int64_t Frames() const;
    bool IsFav(const std::wstring& divaKey) const; void ToggleFav(const std::wstring& divaKey);
    HWND Overlay() const { return hwnd_; } HINSTANCE Inst() const { return hi_; }
    std::wstring StagePath() const { return stagePath_; }
    std::wstring DiagText();               // multi-line status report (Settings -> Diagnostics, Copy report)
    std::vector<std::wstring> LibraryFolders() const;

    // ---- commands
    bool OpenPath(const std::wstring& path);
    bool OpenClip(int idx);
    bool OpenDiva(int diva, int clip);     // library indices
    void SeekFrame(int64_t f); void StepFrame(int dir);
    void SetLoopMode(int m); void SetClickThrough(bool on); void SetLock(bool on); void Panic(bool on); void SetMusicPaused(bool on);
    void PerformerMenu(POINT pt);
    void Enqueue(const std::wstring& clipPath, bool front = false); bool PlayQueued();   // playback queue
    void MoveToNextMonitor();              // cycles the performer through the connected displays
    void SetGameOpen(bool on) { gameOpen_ = on; UpdateSuspend(); }      // the mini game has its own music: ours pauses while it runs
    bool gameOpen_ = false;
    void UpdateMusicState() { musicPaused_ = false; UpdateSuspend(); UpdateMediaKeys(); }
    void Unstage();
    void SetPaused(bool on); void SetHidden(bool on); void SetEdit(bool on);
    void SetScale(float s, bool save); void SetOpacity(float o, bool save);
    void SetMirror(bool on); void SetShadow(bool on); void SetTopmost(bool on); void SetAutoHide(bool on);
    void ResetPosition();
    void SetTheme(int id);
    void SetDecodeMode(int m);
    void RescanLibrary();
    void AddLibraryFolder(const std::wstring& dir); void RemoveLibraryFolder(int i);
    void OnCommand(UINT id);
    void Notify(const std::wstring& title, const std::wstring& text);
    void Quit();
    static bool Autostart(); static void SetAutostart(bool on);

private:
    static LRESULT CALLBACK WndProcThunk(HWND h, UINT m, WPARAM w, LPARAM l);
    LRESULT WndProc(UINT m, WPARAM w, LPARAM l);
    bool CreateOverlay(); bool InitRenderer();
    bool StartClip(int idx, bool hw);
    void Tick(); void Watchdog(double now);
    void ApplyStyles(); void ApplyGeometry(bool keepAnchor = true); void EnsureVisible();
    void UpdateSuspend(); void UpdateOverlayVisibility();
    void ShowTrayMenu(POINT pt);
    void TrayAdd(); void TrayUpdate(); void TrayRemove();
    void PollFullscreen(double now); void PollPointer(); void UpdateMediaKeys(); void RegisterHotkeys(); void StageSize(int& w, int& h) const;
    bool WantHw() const;
    void SelftestFinish();
    double MediaTime(double now) const { return suspended_ ? suspendMedia_ : now - clockOrigin_; }

    HINSTANCE hi_ = nullptr; HWND hwnd_ = nullptr;
    Renderer r_; std::unique_ptr<VideoSource> src_; std::wstring stagePath_;
    PresentMode mode_ = PresentMode::DComp; bool cliSw_ = false, cliLayered_ = false, lowLatency_ = false;
    bool edit_ = false, paused_ = false, hidden_ = false, autoHidden_ = false, suspended_ = false;
    float editAnim_ = 0, fadeIn_ = 1;
    bool clockValid_ = false; double clockOrigin_ = 0, suspendMedia_ = 0;
    int64_t shown_ = -1; bool dirty_ = true;
    double t0_ = 0, lastTick_ = 0, lastStats_ = 0, lastFsPoll_ = 0, lastMusicPoll_ = 0, lastProgress_ = 0, openedAt_ = 0, busySince_ = 0;
    int stallLevel_ = 0; std::vector<std::string> events_;
    HICON icons_[3] = {}; HICON bigIcon_ = nullptr;
    UINT wmTaskbarCreated_ = 0; bool trayAdded_ = false;
    HANDLE timer_ = nullptr;
    bool panic_ = false, musicPaused_ = false, interactive_ = false, pressed_ = false, dragging_ = false, mediaKeys_ = false;
    POINT pressPt_{}, pressAnchor_{}; double lastPtrPoll_ = 0;
    bool falling_ = false; double fallY_ = 0, fallV_ = 0, fallT_ = 0; int floorY_ = 0;
    bool recreating_ = false, quitting_ = false, clipHw_ = false, startHidden_ = false;
    bool SwitchToLayered();
    // internal QA hooks (not part of the user experience)
    std::wstring selftest_; double selftestStart_ = 0, selftestSecs_ = 3.0; bool selftestRequested_ = false; uint64_t presented_ = 0;
    std::vector<std::pair<std::wstring, std::wstring>> expect_;
    std::wstring qaWheel_; int qaDrop_ = 0; bool qaHw_ = false; int qaSeek_ = -1, qaSeekWait_ = -1, qaSeekLanded_ = -1;
};
extern App* g_app;

// Launcher window (Ui.cpp)
void UiShow(bool activate = true);
void UiInvalidate();
bool UiVisible();
HWND UiWindow();
void UiThemeChanged();
void UiToast(const std::wstring& text);
bool UiScreenshot(const std::wstring& bmpPath, int page);
// Wet-Rash mini game (WetRash.cpp)
void WetRashOpen(); void WetRashClose(); bool WetRashShot(const std::wstring& bmpPath, int scene);   // QA: render a page and save the window pixels

// Tray flyout (Flyout.cpp)
struct FlyItem { UINT id; std::wstring text; int icon; bool checked = false, enabled = true, sep = false; std::wstring hint; };
void FlyoutShow(HWND owner, POINT anchor, std::vector<FlyItem> items);   // posts WM_COMMAND(id) to owner on pick
void FlyoutClose();
bool FlyoutOpen();
// Radial menu (Wheel.cpp): up to 8 wedges, first one at 12 o'clock, clockwise
struct WheelItem { UINT id; std::wstring label; int icon; bool enabled = true; };
void WheelShow(HWND owner, POINT centre, const std::wstring& title, std::vector<WheelItem> items);   // posts WM_COMMAND(id) to owner on pick
void WheelClose(); bool WheelOpen(); bool WheelScreenshot(const std::wstring& bmp);
