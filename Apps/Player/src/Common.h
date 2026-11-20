#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>

using Microsoft::WRL::ComPtr;

// Logging: OutputDebugString + %LOCALAPPDATA%\Divaverse\player.log
void LogInit();
void Log(const char* fmt, ...);
std::vector<std::string> LogTail(size_t n);
std::wstring LogDir();

std::wstring Utf8ToW(const std::string& s);
std::string  WToUtf8(const std::wstring& s);
std::wstring DirOf(const std::wstring& path);
std::wstring JoinPath(const std::wstring& a, const std::wstring& b);
bool FileExists(const std::wstring& p);
bool DirExists(const std::wstring& p);
bool ReadFileUtf8(const std::wstring& p, std::string& out);
std::wstring AppDataDir(); // %APPDATA%\Divaverse (created)

// Shared lock for the D3D11 immediate context: the decode thread's uploads / surface copies and the
// render thread go through it. The decoder MFT itself relies on ID3D10Multithread protection.
using CtxLock = std::recursive_mutex;

// Monotonic seconds (QPC)
double NowSeconds();

// Content plugins (DvPlugin.h): 1 = the Player loads Plugins\*.dll and plays what they offer. Set to 0 and the
// support is compiled out; nothing else changes.
#ifndef DV_PLUGINS
#define DV_PLUGINS 1
#endif
#define DV_VERSION_W L"0.15.0"
#define DV_VERSION   "0.15.0"

// Writes the whole file to <path>.tmp, flushes it, then swaps it in with one rename. A crash or power cut leaves
// either the old file or the new one, never a truncated mix.
bool WriteFileAtomic(const std::wstring& path, const void* data, size_t n);
inline bool WriteFileAtomic(const std::wstring& path, const std::string& s) { return WriteFileAtomic(path, s.data(), s.size()); }

// Crash capture: an unhandled exception (and abort / pure call / invalid CRT parameter) writes
// %LOCALAPPDATA%\Divaverse\crash\crash-<time>.dmp plus a .txt with the faulting module and the log tail.
void CrashInstall();
std::wstring CrashDir();
uint64_t NewestCrashTime(std::wstring* dmp = nullptr);     // FILETIME of the newest dump (0 = none)
uint64_t FileWriteTime(const std::wstring& p);
