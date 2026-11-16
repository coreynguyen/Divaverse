#pragma once
// Still images through the Windows Imaging Component (PNG, JPEG, BMP, GIF, WebP where the OS has the codec).
// Output is always premultiplied BGRA, scaled so the longer side is at most maxDim.
#include "Common.h"

struct ImageBGRA { std::vector<uint8_t> px; int w = 0, h = 0; bool ok() const { return w > 0 && h > 0 && px.size() == (size_t)w * h * 4; } };

// The calling thread must have COM initialised. Input larger than 32 MiB or wider/taller than 16384 px is refused.
bool DecodeImageMem(const uint8_t* data, size_t n, int maxDim, ImageBGRA& out);
bool DecodeImageFile(const std::wstring& path, int maxDim, ImageBGRA& out);
bool DecodeImageResource(int rcdataId, int maxDim, ImageBGRA& out);

// Generated-thumbnail cache in %LOCALAPPDATA%\Divaverse\cache, keyed by path + size + mtime of the source media.
uint64_t CacheKey(const std::wstring& path);
bool CacheLoad(uint64_t key, ImageBGRA& out);
void CacheSave(uint64_t key, const ImageBGRA& img);
