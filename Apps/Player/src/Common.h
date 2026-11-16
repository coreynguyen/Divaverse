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
