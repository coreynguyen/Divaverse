#pragma once
// Procedural Divaverse mark: squircle badge, gradient halo arc, four-point sparkle.
// Pure C++ (no OS deps) so the same code renders the runtime tray icons and the
// build-time app.ico (Tools/iconforge).
#include <cstdint>
#include <vector>

enum class IconState { Normal = 0, Paused = 1, Edit = 2 };

// Straight-alpha RGBA8, row-major, top-down.
void ProcIconRGBA(int size, IconState st, std::vector<uint8_t>& rgba);

// Complete .ico file (32bpp DIB entries) containing the given sizes.
std::vector<uint8_t> ProcIconICO(const int* sizes, int count, IconState st);
