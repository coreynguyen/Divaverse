#include "UserData.h"
#include "Json.h"
#include <algorithm>
#include <cwctype>
#include <cstdio>
#include <cmath>
#include <ctime>

static std::wstring File() { return AppDataDir() + L"\\userdata.txt"; }
static std::wstring Low(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }
void UserData::Load() {
    std::string txt; if (!ReadFileUtf8(File(), txt)) return;
    size_t i = 0;
    while (i < txt.size()) {
        size_t e = txt.find('\n', i); if (e == std::string::npos) e = txt.size();
        std::string l = txt.substr(i, e - i); i = e + 1; while (!l.empty() && l.back() == '\r') l.pop_back();
        if (l.size() < 3 || l[1] != '\t') continue;
        size_t t2 = l.find('\t', 2); std::wstring key = Utf8ToW(l.substr(2, t2 == std::string::npos ? t2 : t2 - 2)); std::string val = t2 == std::string::npos ? "" : l.substr(t2 + 1);
        switch (l[0]) {
        case 'R': rating[key] = std::clamp(atoi(val.c_str()), 0, 5); break;
        case 'S': seen.insert(key); break;
        case 'P': { PlayStat p; unsigned long long l = 0; if (sscanf(val.c_str(), "%d\t%lf\t%llu", &p.plays, &p.seconds, &l) >= 2) { p.last = l; stat[key] = p; } break; }
        case 'W': weight[key] = std::clamp((float)atof(val.c_str()), 0.15f, 2.5f); break;
        case 'T': { auto& v = tags[key]; size_t p = 0; while (p <= val.size()) { size_t c = val.find('\t', p); std::string tg = val.substr(p, c == std::string::npos ? c : c - p); if (!tg.empty()) v.push_back(Utf8ToW(tg)); if (c == std::string::npos) break; p = c + 1; } break; }
        }
    }
}
void UserData::Save() {
    std::string out; char b[32];
    for (auto& kv : rating) if (kv.second > 0) out += "R\t" + WToUtf8(kv.first) + "\t" + std::to_string(kv.second) + "\r\n";
    for (auto& kv : tags) if (!kv.second.empty()) { out += "T\t" + WToUtf8(kv.first); for (auto& t : kv.second) out += "\t" + WToUtf8(t); out += "\r\n"; }
    for (auto& k : seen) out += "S\t" + WToUtf8(k) + "\r\n";
    for (auto& kv : weight) if (std::fabs(kv.second - 1.f) > 0.01f) { snprintf(b, sizeof(b), "%.3f", kv.second); out += "W\t" + WToUtf8(kv.first) + "\t" + b + "\r\n"; }
    for (auto& kv : stat) { char pb[96]; snprintf(pb, sizeof(pb), "\t%d\t%.0f\t%llu\r\n", kv.second.plays, kv.second.seconds, (unsigned long long)kv.second.last); out += "P\t" + WToUtf8(kv.first) + pb; }
    WriteFileAtomic(File(), out);
    dirty = false; lastFlush = NowSeconds();
}
void UserData::Flush(bool force) { if (dirty && (force || NowSeconds() - lastFlush > 60)) Save(); }
float UserData::Weight(const std::wstring& p) const { auto it = weight.find(Low(p)); return it == weight.end() ? 1.f : it->second; }
void UserData::Nudge(const std::wstring& p, float f) { float& w = weight.try_emplace(Low(p), 1.f).first->second; w = std::clamp(w * f, 0.15f, 2.5f); dirty = true; }

void UserData::Played(const std::wstring& p) { PlayStat& s = stat[Low(p)]; ++s.plays; s.last = (uint64_t)time(nullptr); dirty = true; }
void UserData::Watched(const std::wstring& p, double secs) { if (secs <= 0) return; stat[Low(p)].seconds += secs; dirty = true; }
UserData::PlayStat UserData::Stat(const std::wstring& p) const { auto it = stat.find(Low(p)); return it == stat.end() ? PlayStat{} : it->second; }
static bool Shareable(const std::wstring& key) { return key.rfind(L"file:", 0) != 0 && key.rfind(L"show:", 0) != 0 && key.find(L'\\') == std::wstring::npos && key.find(L'/') == std::wstring::npos; }   // synthetic keys hold paths
std::string UserData::ExportShare(const std::vector<std::wstring>& favs) const {
    std::string out = "#divaverse-collection 1\r\n";
    for (auto& kv : rating) if (kv.second > 0 && Shareable(kv.first)) out += "R\t" + WToUtf8(kv.first) + "\t" + std::to_string(kv.second) + "\r\n";
    for (auto& kv : tags) if (!kv.second.empty() && Shareable(kv.first)) { out += "T\t" + WToUtf8(kv.first); for (auto& t : kv.second) out += "\t" + WToUtf8(t); out += "\r\n"; }
    for (auto& f : favs) if (Shareable(f)) out += "F\t" + WToUtf8(f) + "\r\n";
    return out;
}
int UserData::ImportShare(const std::string& txt, std::vector<std::wstring>& favs) {
    if (txt.rfind("#divaverse-collection", 0) != 0 && txt.rfind("\xEF\xBB\xBF#divaverse-collection", 0) != 0) return -1;
    int n = 0; size_t i = 0;
    while (i < txt.size()) {
        size_t e = txt.find('\n', i); if (e == std::string::npos) e = txt.size();
        std::string l = txt.substr(i, e - i); i = e + 1; while (!l.empty() && l.back() == '\r') l.pop_back();
        if (l.size() < 3 || l[1] != '\t') continue;
        size_t t2 = l.find('\t', 2); std::wstring key = Utf8ToW(l.substr(2, t2 == std::string::npos ? t2 : t2 - 2)); std::string val = t2 == std::string::npos ? "" : l.substr(t2 + 1);
        if (key.empty() || key.size() > 200 || !Shareable(key)) continue;
        if (l[0] == 'R') { if (!rating.count(key)) { rating[key] = std::clamp(atoi(val.c_str()), 1, 5); ++n; } }          // the user's own rating wins
        else if (l[0] == 'T') { auto& v = tags[key]; size_t p = 0; while (p <= val.size()) { size_t c = val.find('\t', p); std::wstring tg = Utf8ToW(val.substr(p, c == std::string::npos ? c : c - p));
                if (!tg.empty() && tg.size() <= 24 && v.size() < 32 && std::find(v.begin(), v.end(), tg) == v.end()) { v.push_back(tg); ++n; } if (c == std::string::npos) break; p = c + 1; } }
        else if (l[0] == 'F') { if (std::find(favs.begin(), favs.end(), key) == favs.end()) { favs.push_back(key); ++n; } }
    }
    if (n) dirty = true;
    return n;
}

static void Esc(std::string& o, const std::string& s) {
    o += '"';
    for (unsigned char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += (char)c; } else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; } else o += (char)c; }
    o += '"';
}
std::string JsonWrite(const JVal& v, int ind) {
    std::string o, pad((size_t)ind * 2 + 2, ' '), end((size_t)ind * 2, ' '); char b[40];
    switch (v.t) {
    case JVal::Null: return "null";
    case JVal::Bool: return v.b ? "true" : "false";
    case JVal::Num: if (v.n == std::floor(v.n) && std::fabs(v.n) < 1e15) snprintf(b, sizeof(b), "%.0f", v.n); else snprintf(b, sizeof(b), "%.6g", v.n); return b;
    case JVal::Str: Esc(o, v.s); return o;
    case JVal::Arr: o = "["; for (size_t i = 0; i < v.a.size(); ++i) { if (i) o += ", "; o += JsonWrite(v.a[i], ind + 1); } return o + "]";
    case JVal::Obj: o = "{\r\n"; for (size_t i = 0; i < v.o.size(); ++i) { o += pad; Esc(o, v.o[i].first); o += ": " + JsonWrite(v.o[i].second, ind + 1) + (i + 1 < v.o.size() ? ",\r\n" : "\r\n"); } return o + end + "}";
    }
    return "null";
}
