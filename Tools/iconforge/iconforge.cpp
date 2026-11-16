// iconforge: writes the procedural Divaverse icons to .ico files (build-time asset generation).
//   iconforge <out_dir>
// Builds anywhere (MSVC, clang, gcc): cl /O2 /EHsc /std:c++17 iconforge.cpp ..\..\Apps\Player\src\ProcIcon.cpp
#include "../../Apps/Player/src/ProcIcon.h"
#include <cstdio>
#include <string>

static bool WriteAll(const std::string& p, const std::vector<uint8_t>& b) {
    FILE* f = std::fopen(p.c_str(), "wb"); if (!f) return false;
    bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size(); std::fclose(f); return ok;
}

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : ".";
    const int sizes[] = { 16, 20, 24, 32, 40, 48, 64, 96, 128, 256 };
    const char* names[] = { "app.ico", "app_paused.ico", "app_edit.ico" };
    for (int s = 0; s < 3; ++s) {
        auto ico = ProcIconICO(sizes, (int)(sizeof(sizes) / sizeof(*sizes)), (IconState)s);
        std::string p = dir + "/" + names[s];
        if (!WriteAll(p, ico)) { std::fprintf(stderr, "write failed: %s\n", p.c_str()); return 1; }
        std::printf("%s  %zu bytes\n", p.c_str(), ico.size());
    }
    return 0;
}
