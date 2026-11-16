#pragma once
// Background music. Two sources: procedural stations (Synth.h, the default) and the user's own audio files.
// Library:   %APPDATA%\Divaverse\music.txt   (UTF-8, tab separated: flags, weight, plays, skips, path, title, artist, album)
// Playlists: %APPDATA%\Divaverse\Playlists\<name>.m3u8   (plain M3U8, readable by other players)
// Play statistics and weights live in memory and are flushed at most once a minute and at exit.
#include "Common.h"
#include "Audio.h"
#include "Image.h"
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>

struct MusicTrack {
    std::wstring path, name, artist, album;
    bool enabled = true, fav = false, tagged = false;
    float weight = 1.f;                 // shuffle weight 0.15..2.5; skipping lowers it, finishing / replaying raises it; a favourite counts as 3
    int plays = 0, skips = 0;
};
struct MusicAlbum { std::wstring name, artist; std::vector<int> tracks; };
struct MusicPlaylist { std::wstring name; std::vector<std::wstring> paths; };

class Music {
public:
    ~Music();
    void Load();
    void Save();                                             // library + playlists, now
    void Flush(bool force = false);                          // writes only if something changed (rate limited unless forced)
    int  AddFiles(const std::vector<std::wstring>& paths);
    int  AddFolder(const std::wstring& dir);                 // recursive
    const std::vector<MusicTrack>& Tracks() const { return tracks_; }
    const std::vector<MusicAlbum>& Albums();                 // grouped by album tag (folder name when untagged)
    std::vector<MusicPlaylist>& Playlists() { return lists_; }
    void Toggle(int i);
    void Remove(int i);
    void ToggleFav(int i);
    int  FindTrack(const std::wstring& path) const;

    // What Next / shuffle draws from: 0 everything, 1 one album, 2 one playlist.
    void SetScope(int kind, int index);
    int  ScopeKind() const { return scopeKind_; } int ScopeIndex() const { return scopeIndex_; }
    std::wstring ScopeName();
    int  NewPlaylist(); void DeletePlaylist(int i); void AddToPlaylist(int list, int track); void RemoveFromPlaylist(int list, int k);
    int  ImportPlaylist(const std::wstring& file);           // .m3u .m3u8 .pls .wpl; returns the playlist index or -1

    void SetEnabled(bool on);
    bool Enabled() const { return enabled_; }
    void SetSuspended(bool on);
    void SetVolume(float v);
    float Volume() const { return volume_; }
    void SetNormalize(bool on);
    bool Normalize() const { return normalize_; }
    void SetShuffle(bool on) { shuffle_ = on; }
    bool Shuffle() const { return shuffle_; }
    // mode 0 = generated station, 1 = own files
    void Select(int mode, int index);
    int  Mode() const { return mode_; }
    int  Station() const { return station_; }
    int  CurrentFile() const { return mode_ == 1 ? current_ : -1; }
    void Next(bool byUser = true);
    void Prev();
    void Poll();
    bool Playing() const { return out_ != nullptr && !suspended_; }
    float Level() const { return out_ ? out_->Level() : 0.f; }
    std::wstring NowPlaying() const;
    std::wstring LastError() const { return lastErr_; }
    static bool IsAudioFile(const std::wstring& path);
    static bool IsPlaylistFile(const std::wstring& path);
    // Album art embedded in the file (through the shell's thumbnail provider), else cover/folder image beside it.
    static bool CoverOf(const std::wstring& path, int size, ImageBGRA& out);
    int  Generation() const { return gen_; }                 // bumps when tracks / tags change

private:
    bool Start();
    int  Pick(int from);
    std::vector<int> Pool() const;
    void TagWorker();
    void Touch() { dirty_ = true; }
    std::vector<MusicTrack> tracks_; std::vector<MusicAlbum> albums_; int albumsGen_ = -1; std::vector<MusicPlaylist> lists_;
    std::vector<int> recent_;
    std::unique_ptr<AudioOut> out_;
    bool enabled_ = false, suspended_ = false, shuffle_ = false, normalize_ = true, dirty_ = false; float volume_ = 0.6f;
    int mode_ = 0, station_ = 0, current_ = -1, scopeKind_ = 0, scopeIndex_ = 0, gen_ = 0; std::wstring lastErr_; double lastFlush_ = 0;
    struct Tag { std::wstring path, title, artist, album; };
    std::thread tagTh_; std::mutex tagMx_; std::vector<std::wstring> tagTodo_; std::vector<Tag> tagDone_; std::atomic<bool> tagStop_{false}, tagBusy_{false};
};
