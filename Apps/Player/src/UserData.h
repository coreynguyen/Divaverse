#pragma once
// The user's own marks on content: star ratings, tags, what has been seen, and learned clip weights.
// Kept in memory; written to %APPDATA%\Divaverse\userdata.txt at exit and at most once a minute when changed.
// Never written into content files.
#include "Common.h"
#include <unordered_map>
#include <unordered_set>

struct UserData {
    std::unordered_map<std::wstring, int> rating;                       // diva key -> 1..5 (0 / absent = not rated)
    std::unordered_map<std::wstring, std::vector<std::wstring>> tags;   // diva key -> tags
    std::unordered_set<std::wstring> seen;                              // "divaKey|clipId" already presented as new
    std::unordered_map<std::wstring, float> weight;                     // clip path (lower case) -> shuffle weight 0.15..2.5
    struct PlayStat { int plays = 0; double seconds = 0; uint64_t last = 0; };      // last: unix time of the latest play
    std::unordered_map<std::wstring, PlayStat> stat;                    // clip path (lower case) -> how much it has been watched
    void Played(const std::wstring& clipPath); void Watched(const std::wstring& clipPath, double seconds);
    PlayStat Stat(const std::wstring& clipPath) const;
    // Ratings and tags by diva id only (no file paths, no play history): a file a friend can import.
    std::string ExportShare(const std::vector<std::wstring>& favs) const;
    int ImportShare(const std::string& text, std::vector<std::wstring>& favs);   // number of entries taken, -1 = not a collection file
    bool dirty = false; double lastFlush = 0;
    void Load(); void Save(); void Flush(bool force = false);
    float Weight(const std::wstring& clipPath) const;
    void Nudge(const std::wstring& clipPath, float factor);
    int Rating(const std::wstring& key, int authored) const { auto it = rating.find(key); return it != rating.end() ? it->second : authored; }
};
// JSON text of a value (used to write sidecars).
struct JVal; std::string JsonWrite(const JVal& v, int indent = 0);
