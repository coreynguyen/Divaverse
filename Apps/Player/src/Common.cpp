#include "Common.h"
#include <shlobj.h>
#include <cstdarg>
#include <cstdio>
#include <share.h>
#include <algorithm>
#include <exception>
#include <cstdlib>
#include <csignal>
#include <dbghelp.h>

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

bool WriteFileAtomic(const std::wstring& path, const void* data, size_t n) {
    std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { Log("write %s: create failed (%lu)", WToUtf8(tmp).c_str(), GetLastError()); return false; }
    DWORD w = 0; BOOL ok = WriteFile(f, data, (DWORD)n, &w, nullptr) && w == (DWORD)n;
    ok = ok && FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok) { DeleteFileW(tmp.c_str()); Log("write %s: failed (%lu)", WToUtf8(tmp).c_str(), GetLastError()); return false; }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        Log("write %s: rename failed (%lu)", WToUtf8(path).c_str(), GetLastError()); DeleteFileW(tmp.c_str()); return false; }
    return true;
}

uint64_t FileWriteTime(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA a; if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return 0;
    return ((uint64_t)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
}

// ------------------------------------------------------------------------------------ crash capture
namespace {
wchar_t g_crashDir[MAX_PATH];
typedef BOOL(WINAPI* MiniDumpWriteDump_t)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1)) return EXCEPTION_EXECUTE_HANDLER;     // a second thread crashing meanwhile: the first report wins
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t base[MAX_PATH]; swprintf(base, MAX_PATH, L"%ls\\crash-%04d%02d%02d-%02d%02d%02d", g_crashDir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    CreateDirectoryW(g_crashDir, nullptr);
    // text first: it needs nothing but the CRT and survives if dbghelp itself fails
    DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0; void* addr = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;
    HMODULE mod = nullptr; wchar_t modName[MAX_PATH] = L"?";
    if (addr && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)addr, &mod)) GetModuleFileNameW(mod, modName, MAX_PATH);
    char txt[4096]; int n = snprintf(txt, sizeof(txt), "Divaverse %s crashed\r\nexception 0x%08lX at %p (%ls + 0x%llX)\r\nthread %lu\r\n\r\n--- log tail ---\r\n",
        DV_VERSION, (unsigned long)code, addr, modName, (unsigned long long)((uintptr_t)addr - (uintptr_t)mod), GetCurrentThreadId());
    if (g_logMx.try_lock()) {                                                   // the crash may be inside Log(): never wait for it
        size_t from = g_tail.size() > 60 ? g_tail.size() - 60 : 0;
        for (size_t i = from; i < g_tail.size() && n < (int)sizeof(txt) - 300; ++i) n += snprintf(txt + n, sizeof(txt) - n, "%s", g_tail[i].c_str());
        if (g_log) fflush(g_log);
        g_logMx.unlock();
    }
    std::wstring tp = std::wstring(base) + L".txt";
    HANDLE ft = CreateFileW(tp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (ft != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(ft, txt, (DWORD)n, &w, nullptr); CloseHandle(ft); }
    if (HMODULE dbg = LoadLibraryW(L"dbghelp.dll")) {
        if (auto write = (MiniDumpWriteDump_t)GetProcAddress(dbg, "MiniDumpWriteDump")) {
            std::wstring dp = std::wstring(base) + L".dmp";
            HANDLE fd = CreateFileW(dp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            if (fd != INVALID_HANDLE_VALUE) {
                MINIDUMP_EXCEPTION_INFORMATION mi{ GetCurrentThreadId(), ep, FALSE };
                write(GetCurrentProcess(), GetCurrentProcessId(), fd, (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory | MiniDumpWithThreadInfo), ep ? &mi : nullptr, nullptr, nullptr);
                CloseHandle(fd);
            }
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;                                           // end the process quietly; the next start reports it
}
void RaiseFatal() { RaiseException(0xE0D1AF01, EXCEPTION_NONCONTINUABLE, 0, nullptr); }
}
std::wstring CrashDir() { return LogDir() + L"\\crash"; }
void CrashInstall() {
    std::wstring d = CrashDir(); wcsncpy(g_crashDir, d.c_str(), MAX_PATH - 1);
    SetUnhandledExceptionFilter(CrashFilter);
    std::set_terminate([] { RaiseFatal(); std::abort(); });
    _set_purecall_handler([] { RaiseFatal(); });
    _set_invalid_parameter_handler([](const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) { RaiseFatal(); });
    signal(SIGABRT, [](int) { RaiseFatal(); });
}
uint64_t NewestCrashTime(std::wstring* dmp) {
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((CrashDir() + L"\\crash-*.txt").c_str(), &fd); uint64_t best = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    do { uint64_t t = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
         if (t > best) { best = t; if (dmp) { *dmp = CrashDir() + L"\\" + fd.cFileName; dmp->resize(dmp->size() - 4); } } } while (FindNextFileW(h, &fd));
    FindClose(h); return best;
}
