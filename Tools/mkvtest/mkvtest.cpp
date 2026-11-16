// mkvtest: portable test driver for MkvDemux (builds natively on any OS).
//   mkvtest file.mkv                 -> track table + per-packet lines "track pts_ms size key"
//   mkvtest file.mkv --annexb T out  -> Annex B elementary stream of track index T (SPS/PPS + all frames)
//   mkvtest file.mkv --seek MS       -> first packet after SeekNs
#include "../../Apps/Player/src/MkvDemux.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
struct FileIo : MkvIo {
    FILE* f = nullptr; uint64_t sz = 0;
    uint64_t Size() override { return sz; }
    size_t ReadAt(uint64_t pos, void* d, size_t n) override { fseek(f, (long)pos, SEEK_SET); return fread(d, 1, n, f); }
};
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    FileIo io; io.f = fopen(argv[1], "rb"); if (!io.f) { perror("open"); return 2; }
    fseek(io.f, 0, SEEK_END); io.sz = (uint64_t)ftell(io.f);
    MkvDemux d; std::string err;
    if (!d.Open(&io, err)) { fprintf(stderr, "OPEN FAIL: %s\n", err.c_str()); return 1; }
    int annex = -1; FILE* out = nullptr; long seekMs = -1;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--annexb") && i + 2 < argc) { annex = atoi(argv[i + 1]); out = fopen(argv[i + 2], "wb"); i += 2; }
        else if (!strcmp(argv[i], "--seek") && i + 1 < argc) seekMs = atol(argv[++i]);
    }
    fprintf(stderr, "doctype=%s scale=%llu duration_ms=%.3f cues=%zu\n", d.DocType().c_str(), (unsigned long long)d.TimestampScale(), d.DurationNs() / 1e6, d.Cues().size());
    for (size_t i = 0; i < d.Tracks().size(); ++i) {
        auto& t = d.Tracks()[i]; AvcConfig c; std::string e2;
        bool ok = t.codecId == "V_MPEG4/ISO/AVC" && AvcParseConfig(t.codecPrivate.data(), t.codecPrivate.size(), c, e2);
        fprintf(stderr, "track[%zu] no=%llu type=%d codec=%s name='%s' %dx%d dd=%llu range=%d priv=%zu", i, (unsigned long long)t.number, t.type,
                t.codecId.c_str(), t.name.c_str(), t.width, t.height, (unsigned long long)t.defaultDurationNs, t.colourRange, t.codecPrivate.size());
        if (ok) fprintf(stderr, " | avc profile=%d level=%d nal=%d coded=%dx%d crop=%dx%d depth=%d chroma=%d full=%d reorder=%d dpb=%d refs=%d",
                c.profile, c.level, c.nalLenSize, c.codedW, c.codedH, c.cropW, c.cropH, c.bitDepth, c.chromaFormat, (int)c.fullRange, c.maxNumReorder, c.maxDecBuffering, c.numRefFrames);
        else if (!e2.empty()) fprintf(stderr, " | avcC: %s", e2.c_str());
        fprintf(stderr, "\n");
    }
    if (seekMs >= 0) d.SeekNs((int64_t)seekMs * 1000000);
    if (annex >= 0) {
        AvcConfig c; std::string e2; auto& t = d.Tracks()[annex];
        if (!AvcParseConfig(t.codecPrivate.data(), t.codecPrivate.size(), c, e2)) { fprintf(stderr, "%s\n", e2.c_str()); return 1; }
        std::vector<uint8_t> b; AvcParamSetsAnnexB(c, b); fwrite(b.data(), 1, b.size(), out);
        for (size_t i = 0; i < d.Tracks().size(); ++i) d.Tracks()[i].enabled = (int)i == annex;
        MkvPacket pk; size_t n = 0;
        while (d.Read(pk, err)) { b.clear(); if (!AvcToAnnexB(pk.data.data(), pk.data.size(), c.nalLenSize, b)) { fprintf(stderr, "bad AU %zu\n", n); return 1; } fwrite(b.data(), 1, b.size(), out); ++n; }
        if (!err.empty()) { fprintf(stderr, "READ FAIL: %s\n", err.c_str()); return 1; }
        fclose(out); fprintf(stderr, "wrote %zu access units\n", n); return 0;
    }
    MkvPacket pk; size_t n = 0;
    while (d.Read(pk, err)) { printf("%d %lld %zu %d\n", pk.track, (long long)(pk.ptsNs / 1000000), pk.data.size(), (int)pk.key); ++n; if (seekMs >= 0) break; }
    if (!err.empty()) { fprintf(stderr, "READ FAIL: %s\n", err.c_str()); return 1; }
    fprintf(stderr, "packets=%zu\n", n);
    return 0;
}
