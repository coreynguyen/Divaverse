#pragma once
#include "Common.h"
#include "MkvDemux.h"
#include <algorithm>
// Positional reads on a plain synchronous handle; MkvDemux keeps a bounded (256 KiB) window.
struct Win32Io : MkvIo {
    HANDLE h = INVALID_HANDLE_VALUE; uint64_t size = 0;
    ~Win32Io() override { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    bool Open(const std::wstring& p) {
        h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER li{}; GetFileSizeEx(h, &li); size = (uint64_t)li.QuadPart; return true;
    }
    uint64_t Size() override { return size; }
    size_t ReadAt(uint64_t pos, void* dst, size_t n) override {
        size_t done = 0;
        while (done < n) {
            OVERLAPPED ov{}; ov.Offset = (DWORD)(pos + done); ov.OffsetHigh = (DWORD)((pos + done) >> 32);
            DWORD want = (DWORD)std::min<size_t>(n - done, 1u << 30), got = 0;
            if (!ReadFile(h, (uint8_t*)dst + done, want, &got, &ov) || !got) break;
            done += got;
        }
        return done;
    }
};
