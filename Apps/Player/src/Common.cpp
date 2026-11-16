#include "Common.h"
#include <shlobj.h>
#include <cstdarg>
#include <cstdio>
#include <share.h>
#include <algorithm>

static FILE* g_log = nullptr;
static std::mutex g_logMx;
static std::vector<std::string> g_tail;             // last lines, for the in-app diagnostics report

std::vector<std::string> LogTail(size_t n) {
    std::lock_guard<std::mutex> g(g_logMx);
    return std::vector<std::string>(g_tail.end() - (ptrdiff_t)std::min(n, g_tail.size()), g_tail.end());
}
std::wstring LogDir() {
    PWSTR p = nullptr; std::wstring d;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &p))) { d = std::wstring(p) + L"\\Divaverse"; CoTaskMemFree(p); }
    return d;
}

std::wstring AppDataDir() {
    PWSTR p = nullptr; std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &p))) {
        r = std::wstring(p) + L"\\Divaverse"; CoTaskMemFree(p);
        CreateDirectoryW(r.c_str(), nullptr);
    }
    return r;
}

void LogInit() {
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &p))) {
        std::wstring d = std::wstring(p) + L"\\Divaverse"; CoTaskMemFree(p);
        CreateDirectoryW(d.c_str(), nullptr);
        // The previous run's log survives one restart, so a hang or crash can still be read afterwards.
        MoveFileExW((d + L"\\player.log").c_str(), (d + L"\\player.prev.log").c_str(), MOVEFILE_REPLACE_EXISTING);
        g_log = _wfsopen((d + L"\\player.log").c_str(), L"w", _SH_DENYNO);   // readable while running
    }
}

void Log(const char* fmt, ...) {
    char buf[2048];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf) - 2, fmt, ap); va_end(ap);
    size_t n = strlen(buf); buf[n] = '\n'; buf[n + 1] = 0;
    OutputDebugStringA(buf);
    std::lock_guard<std::mutex> g(g_logMx);
    SYSTEMTIME st; GetLocalTime(&st);
    char line[2100]; snprintf(line, sizeof(line), "%02d:%02d:%02d.%03d %s", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
    if (g_log) { fputs(line, g_log); fflush(g_log); }
    g_tail.emplace_back(line); if (g_tail.size() > 400) g_tail.erase(g_tail.begin(), g_tail.begin() + 100);
}

std::wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n); return w;
}
std::string WToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr); return s;
}
std::wstring DirOf(const std::wstring& p) {
    size_t k = p.find_last_of(L"\\/"); return k == std::wstring::npos ? L"." : p.substr(0, k);
}
std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (b.size() > 1 && (b[1] == L':' || b[0] == L'\\' || b[0] == L'/')) return b;
    std::wstring r = a; if (!r.empty() && r.back() != L'\\' && r.back() != L'/') r += L'\\';
    for (wchar_t c : b) r += (c == L'/') ? L'\\' : c;
    return r;
}
bool FileExists(const std::wstring& p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
bool DirExists(const std::wstring& p)  { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES &&  (a & FILE_ATTRIBUTE_DIRECTORY); }
bool ReadFileUtf8(const std::wstring& p, std::string& out) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    if (sz.QuadPart > (16 << 20)) { CloseHandle(h); return false; }
    out.resize((size_t)sz.QuadPart); DWORD rd = 0;
    BOOL ok = ReadFile(h, out.data(), (DWORD)out.size(), &rd, nullptr); CloseHandle(h);
    if (out.size() >= 3 && (uint8_t)out[0] == 0xEF && (uint8_t)out[1] == 0xBB && (uint8_t)out[2] == 0xBF) out.erase(0, 3);
    return ok && rd == out.size();
}
double NowSeconds() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart / (double)f.QuadPart;
}
