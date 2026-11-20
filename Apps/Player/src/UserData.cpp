#include "UserData.h"
#include "Json.h"
#include <algorithm>
#include <cwctype>
#include <cstdio>
#include <cmath>

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
    WriteFileAtomic(File(), out);
    dirty = false; lastFlush = NowSeconds();
}
void UserData::Flush(bool force) { if (dirty && (force || NowSeconds() - lastFlush > 60)) Save(); }
float UserData::Weight(const std::wstring& p) const { auto it = weight.find(Low(p)); return it == weight.end() ? 1.f : it->second; }
void UserData::Nudge(const std::wstring& p, float f) { float& w = weight.try_emplace(Low(p), 1.f).first->second; w = std::clamp(w * f, 0.15f, 2.5f); dirty = true; }

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
