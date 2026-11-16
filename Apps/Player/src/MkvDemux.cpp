#include "MkvDemux.h"
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace {
enum : uint32_t {
    ID_EBML = 0x1A45DFA3, ID_EBMLReadVersion = 0x42F7, ID_EBMLMaxIDLength = 0x42F2, ID_EBMLMaxSizeLength = 0x42F3,
    ID_DocType = 0x4282, ID_DocTypeReadVersion = 0x4285,
    ID_Segment = 0x18538067, ID_SeekHead = 0x114D9B74, ID_Seek = 0x4DBB, ID_SeekID = 0x53AB, ID_SeekPosition = 0x53AC,
    ID_Info = 0x1549A966, ID_TimestampScale = 0x2AD7B1, ID_Duration = 0x4489,
    ID_Tracks = 0x1654AE6B, ID_TrackEntry = 0xAE, ID_TrackNumber = 0xD7, ID_TrackUID = 0x73C5, ID_TrackType = 0x83,
    ID_DefaultDuration = 0x23E383, ID_Name = 0x536E, ID_Language = 0x22B59C, ID_CodecID = 0x86, ID_CodecPrivate = 0x63A2,
    ID_Video = 0xE0, ID_PixelWidth = 0xB0, ID_PixelHeight = 0xBA, ID_Colour = 0x55B0, ID_Range = 0x55B9,
    ID_ContentEncodings = 0x6D80, ID_ContentEncoding = 0x6240, ID_ContentCompression = 0x5034,
    ID_ContentCompAlgo = 0x4254, ID_ContentCompSettings = 0x4255, ID_ContentEncryption = 0x5035,
    ID_Cluster = 0x1F43B675, ID_Timestamp = 0xE7, ID_SimpleBlock = 0xA3, ID_BlockGroup = 0xA0, ID_Block = 0xA1,
    ID_ReferenceBlock = 0xFB, ID_EncryptedBlock = 0xAF,
    ID_Cues = 0x1C53BB6B, ID_CuePoint = 0xBB, ID_CueTime = 0xB3, ID_CueTrackPositions = 0xB7, ID_CueTrack = 0xF7,
    ID_CueClusterPosition = 0xF1,
    ID_Attachments = 0x1941A469, ID_Chapters = 0x1043A770, ID_Tags = 0x1254C367,
    ID_AttachedFile = 0x61A7, ID_FileDescription = 0x467E, ID_FileName = 0x466E, ID_FileMimeType = 0x4660, ID_FileData = 0x465C, ID_FileUID = 0x46AE,
    ID_Tag = 0x7373, ID_Targets = 0x63C0, ID_TagTrackUID = 0x63C5, ID_SimpleTag = 0x67C8, ID_TagName = 0x45A3, ID_TagString = 0x4487,
};
constexpr size_t kWindow = 256 * 1024;

bool IsLevel1(uint32_t id) {
    return id == ID_SeekHead || id == ID_Info || id == ID_Tracks || id == ID_Cluster || id == ID_Cues ||
           id == ID_Attachments || id == ID_Chapters || id == ID_Tags;
}
std::string Hex(uint64_t v) { char b[32]; snprintf(b, sizeof(b), "0x%llX", (unsigned long long)v); return b; }

// EBML variable-length integer (marker stripped). Returns length or 0.
int VInt(const uint8_t* p, size_t avail, uint64_t& v, bool* allOnes = nullptr) {
    if (!avail || !p[0]) return 0;
    int len = 1; for (uint8_t m = 0x80; !(p[0] & m); m >>= 1) ++len;
    if ((size_t)len > avail) return 0;
    v = p[0] & (0xFFu >> len);
    bool ones = v == (0xFFu >> len);
    for (int i = 1; i < len; ++i) { v = (v << 8) | p[i]; if (p[i] != 0xFF) ones = false; }
    if (allOnes) *allOnes = ones;
    return len;
}
}

bool MkvDemux::Fill(uint64_t pos, size_t n) {
    if (pos >= fileSize_) return false;
    size_t want = std::max(n, kWindow);
    if (want > fileSize_ - pos) want = (size_t)(fileSize_ - pos);
    if (buf_.size() < want) buf_.resize(want);
    bufLen_ = io_->ReadAt(pos, buf_.data(), want); bufPos_ = pos;
    return bufLen_ >= std::min<uint64_t>(n, fileSize_ - pos);
}

const uint8_t* MkvDemux::At(uint64_t pos, size_t n) {
    if (pos + n > fileSize_) return nullptr;
    if (pos < bufPos_ || pos + n > bufPos_ + bufLen_) { if (!Fill(pos, n) || bufLen_ < n) return nullptr; }
    return buf_.data() + (pos - bufPos_);
}

bool MkvDemux::ReadHdr(uint64_t pos, uint64_t limit, Hdr& h, std::string& err) {
    if (limit > fileSize_) limit = fileSize_;
    if (pos >= limit) { err = "element header past parent end at " + Hex(pos); return false; }
    size_t avail = (size_t)std::min<uint64_t>(12, limit - pos);
    const uint8_t* p = At(pos, avail);
    if (!p) { err = "read error at " + Hex(pos); return false; }
    if (!p[0] || p[0] < 0x10) { err = "EBML ID longer than 4 bytes at " + Hex(pos); return false; }
    int il = 1; for (uint8_t m = 0x80; !(p[0] & m); m >>= 1) ++il;
    if ((size_t)il >= avail) { err = "truncated element at " + Hex(pos); return false; }
    h.id = 0; for (int i = 0; i < il; ++i) h.id = (h.id << 8) | p[i];
    bool ones = false; uint64_t sz = 0;
    int sl = VInt(p + il, avail - il, sz, &ones);
    if (!sl) { err = p[il] ? "truncated element size at " + Hex(pos) : "EBML size longer than 8 bytes at " + Hex(pos); return false; }
    h.data = pos + il + sl; h.unknown = ones; h.size = ones ? 0 : sz;
    if (!ones && (sz > limit || h.data > limit - sz)) {
        err = "element " + Hex(h.id) + " at " + Hex(pos) + " overruns its parent (size " + std::to_string(sz) + ")"; return false; }
    return true;
}

bool MkvDemux::ReadUInt(const Hdr& h, uint64_t& v) {
    if (h.size > 8) return false;
    v = 0; if (!h.size) return true;
    const uint8_t* p = At(h.data, (size_t)h.size); if (!p) return false;
    for (uint64_t i = 0; i < h.size; ++i) v = (v << 8) | p[i];
    return true;
}
bool MkvDemux::ReadFloat(const Hdr& h, double& v) {
    const uint8_t* p = At(h.data, (size_t)h.size); if (!p && h.size) return false;
    if (h.size == 4) { uint32_t u = 0; for (int i = 0; i < 4; ++i) u = (u << 8) | p[i]; float f; memcpy(&f, &u, 4); v = f; return true; }
    if (h.size == 8) { uint64_t u = 0; for (int i = 0; i < 8; ++i) u = (u << 8) | p[i]; memcpy(&v, &u, 8); return true; }
    v = 0; return h.size == 0;
}
bool MkvDemux::ReadBytes(const Hdr& h, std::vector<uint8_t>& b, uint64_t cap) {
    if (h.size > cap) return false;
    b.resize((size_t)h.size); if (!h.size) return true;
    if (h.size <= kWindow) { const uint8_t* p = At(h.data, (size_t)h.size); if (!p) return false; memcpy(b.data(), p, (size_t)h.size); return true; }
    return io_->ReadAt(h.data, b.data(), (size_t)h.size) == h.size;
}
bool MkvDemux::ReadString(const Hdr& h, std::string& s) {
    std::vector<uint8_t> b; if (!ReadBytes(h, b, 1 << 16)) return false;
    s.assign((const char*)b.data(), b.size());
    while (!s.empty() && s.back() == 0) s.pop_back();      // EBML strings may be zero padded
    return true;
}
int MkvDemux::TrackIndex(uint64_t number) const {
    for (size_t i = 0; i < tracks_.size(); ++i) if (tracks_[i].number == number) return (int)i;
    return -1;
}

// Iterate the children of a master element. f(child) returns false to abort (err set by f).
#define MKV_FOREACH(parent, child) \
    for (uint64_t _p = (parent).data, _e = (parent).data + (parent).size; _p < _e;) \
        if (Hdr child; !ReadHdr(_p, _e, child, err)) return false; \
        else if (child.unknown) { err = "unknown-size element " + Hex(child.id) + " not allowed here"; return false; } \
        else if ((_p = child.data + child.size), true)

bool MkvDemux::ParseInfo(const Hdr& h, std::string& err) {
    double dur = 0;
    MKV_FOREACH(h, c) {
        if (c.id == ID_TimestampScale) { if (!ReadUInt(c, tsScale_) || !tsScale_) { err = "bad TimestampScale"; return false; } }
        else if (c.id == ID_Duration) ReadFloat(c, dur);
    }
    durationNs_ = dur * (double)tsScale_;
    return true;
}

bool MkvDemux::ParseTrackEntry(const Hdr& h, std::string& err) {
    MkvTrack t; uint64_t v = 0;
    MKV_FOREACH(h, c) {
        switch (c.id) {
        case ID_TrackNumber: ReadUInt(c, t.number); break;
        case ID_TrackUID: ReadUInt(c, t.uid); break;
        case ID_TrackType: if (ReadUInt(c, v)) t.type = (int)v; break;
        case ID_DefaultDuration: ReadUInt(c, t.defaultDurationNs); break;
        case ID_Name: ReadString(c, t.name); break;
        case ID_Language: ReadString(c, t.language); break;
        case ID_CodecID: ReadString(c, t.codecId); break;
        case ID_CodecPrivate: if (!ReadBytes(c, t.codecPrivate, 1 << 20)) { err = "CodecPrivate too large"; return false; } break;
        case ID_Video: {
            MKV_FOREACH(c, d) {
                if (d.id == ID_PixelWidth) { if (ReadUInt(d, v)) t.width = (int)v; }
                else if (d.id == ID_PixelHeight) { if (ReadUInt(d, v)) t.height = (int)v; }
                else if (d.id == ID_Colour) { MKV_FOREACH(d, e) { if (e.id == ID_Range && ReadUInt(e, v)) t.colourRange = (int)v; } }
            }
            break; }
        case ID_ContentEncodings: {
            MKV_FOREACH(c, d) {
                if (d.id != ID_ContentEncoding) continue;
                MKV_FOREACH(d, e) {
                    if (e.id == ID_ContentEncryption) t.unsupported = "encrypted track (ContentEncryption)";
                    else if (e.id == ID_ContentCompression) {
                        uint64_t algo = 0; std::vector<uint8_t> settings;
                        MKV_FOREACH(e, f) {
                            if (f.id == ID_ContentCompAlgo) ReadUInt(f, algo);
                            else if (f.id == ID_ContentCompSettings) ReadBytes(f, settings, 1 << 16);
                        }
                        if (algo == 3) t.strippedHeader = settings;
                        else t.unsupported = "ContentCompression algo " + std::to_string(algo) + " (only header stripping is supported)";
                    }
                }
            }
            break; }
        }
    }
    if (!t.number) { err = "TrackEntry without TrackNumber"; return false; }
    if (TrackIndex(t.number) >= 0) { err = "duplicate TrackNumber " + std::to_string(t.number); return false; }
    tracks_.push_back(std::move(t));
    return true;
}

bool MkvDemux::ParseTracks(const Hdr& h, std::string& err) {
    MKV_FOREACH(h, c) { if (c.id == ID_TrackEntry && !ParseTrackEntry(c, err)) return false; }
    return true;
}

bool MkvDemux::ParseCues(const Hdr& h, std::string& err) {
    MKV_FOREACH(h, c) {
        if (c.id != ID_CuePoint) continue;
        uint64_t time = 0;
        std::vector<std::pair<uint64_t, uint64_t>> tp;
        MKV_FOREACH(c, d) {
            if (d.id == ID_CueTime) ReadUInt(d, time);
            else if (d.id == ID_CueTrackPositions) {
                uint64_t trk = 0, cp = 0;
                MKV_FOREACH(d, e) {
                    if (e.id == ID_CueTrack) ReadUInt(e, trk);
                    else if (e.id == ID_CueClusterPosition) ReadUInt(e, cp);
                }
                tp.emplace_back(trk, cp);
            }
        }
        for (auto& x : tp) {
            uint64_t abs = segData_ + x.second;
            if (abs >= segEnd_) { err = "Cue points past the segment end"; return false; }
            cues_.push_back({ (int64_t)(time * tsScale_), TrackIndex(x.first), abs });
        }
    }
    std::stable_sort(cues_.begin(), cues_.end(), [](const MkvCue& a, const MkvCue& b) { return a.timeNs < b.timeNs; });
    cuesParsed_ = true;
    return true;
}

bool MkvDemux::ParseAttachments(const Hdr& h, std::string& err) {
    MKV_FOREACH(h, c) {
        if (c.id != ID_AttachedFile) continue;
        MkvAttachment a;
        MKV_FOREACH(c, d) {
            if (d.id == ID_FileName) ReadString(d, a.name);
            else if (d.id == ID_FileMimeType) ReadString(d, a.mime);
            else if (d.id == ID_FileDescription) ReadString(d, a.description);
            else if (d.id == ID_FileUID) ReadUInt(d, a.uid);
            else if (d.id == ID_FileData) { a.dataPos = d.data; a.size = d.size; }      // payload stays on disk
        }
        if (!a.name.empty() && a.dataPos) attachments_.push_back(std::move(a));
    }
    attParsed_ = true;
    return true;
}

bool MkvDemux::ParseTags(const Hdr& h, std::string& err) {
    MKV_FOREACH(h, c) {
        if (c.id != ID_Tag) continue;
        uint64_t trackUid = 0; std::vector<MkvTag> found;
        MKV_FOREACH(c, d) {
            if (d.id == ID_Targets) { MKV_FOREACH(d, e) { if (e.id == ID_TagTrackUID) ReadUInt(e, trackUid); } }
            else if (d.id == ID_SimpleTag) {
                MkvTag t;
                MKV_FOREACH(d, e) { if (e.id == ID_TagName) ReadString(e, t.name); else if (e.id == ID_TagString) ReadString(e, t.value); }
                if (!t.name.empty()) found.push_back(std::move(t));
            }
        }
        for (auto& t : found) { t.trackUid = trackUid; tags_.push_back(std::move(t)); }
    }
    tagsParsed_ = true;
    return true;
}

const MkvAttachment* MkvDemux::FindAttachment(const char* name) const {
    for (auto& a : attachments_) {
        size_t i = 0;
        for (; name[i] && i < a.name.size(); ++i) { char x = name[i], y = a.name[i]; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) break; }
        if (!name[i] && i == a.name.size()) return &a;
    }
    return nullptr;
}
bool MkvDemux::ReadAttachment(const MkvAttachment& a, std::vector<uint8_t>& out, uint64_t cap) {
    if (a.size > cap || a.dataPos + a.size > fileSize_) return false;
    out.resize((size_t)a.size);
    return !a.size || io_->ReadAt(a.dataPos, out.data(), (size_t)a.size) == a.size;
}
std::string MkvDemux::Tag(const char* name) const {
    for (auto& t : tags_) if (!t.trackUid && t.name == name) return t.value;
    return {};
}

bool MkvDemux::Open(MkvIo* io, std::string& err) {
    io_ = io; fileSize_ = io->Size(); buf_.clear(); bufLen_ = 0; tracks_.clear(); cues_.clear(); cuesParsed_ = false;
    attachments_.clear(); tags_.clear(); attParsed_ = tagsParsed_ = false;
    if (fileSize_ < 16) { err = "file too small"; return false; }
    Hdr h;
    if (!ReadHdr(0, fileSize_, h, err)) return false;
    if (h.id != ID_EBML || h.unknown) { err = "not an EBML file"; return false; }
    uint64_t readVer = 1, docReadVer = 1, maxId = 4, maxSize = 8;
    {
        MKV_FOREACH(h, c) {
            if (c.id == ID_EBMLReadVersion) ReadUInt(c, readVer);
            else if (c.id == ID_EBMLMaxIDLength) ReadUInt(c, maxId);
            else if (c.id == ID_EBMLMaxSizeLength) ReadUInt(c, maxSize);
            else if (c.id == ID_DocType) ReadString(c, docType_);
            else if (c.id == ID_DocTypeReadVersion) ReadUInt(c, docReadVer);
        }
    }
    if (docType_ != "matroska" && docType_ != "webm") { err = "unsupported DocType '" + docType_ + "'"; return false; }
    if (readVer > 1 || maxId > 4 || maxSize > 8) { err = "unsupported EBML header (version/ID/size length)"; return false; }
    if (docReadVer > 4) { err = "unsupported DocTypeReadVersion " + std::to_string(docReadVer); return false; }

    uint64_t pos = h.data + h.size;
    for (;;) {                                          // Void etc. between the EBML header and the Segment
        if (!ReadHdr(pos, fileSize_, h, err)) return false;
        if (h.id == ID_Segment) break;
        if (h.unknown) { err = "unknown-size element before Segment"; return false; }
        pos = h.data + h.size;
    }
    segData_ = h.data; segEnd_ = h.unknown ? fileSize_ : h.data + h.size;

    uint64_t seekTracks = 0, seekInfo = 0, seekCues = 0, seekAtt = 0, seekTags = 0; bool haveTracks = false, haveInfo = false;
    firstCluster_ = 0;
    for (pos = segData_; pos < segEnd_;) {
        if (segEnd_ - pos < 2) break;
        if (!ReadHdr(pos, segEnd_, h, err)) return false;
        if (h.id == ID_Cluster) { firstCluster_ = pos; break; }
        if (h.unknown) { err = "unknown-size element " + Hex(h.id) + " (only Segment and Cluster may be unknown-size)"; return false; }
        if (h.id == ID_SeekHead) {
            MKV_FOREACH(h, c) {
                if (c.id != ID_Seek) continue;
                uint64_t id = 0, sp = 0;
                MKV_FOREACH(c, d) { if (d.id == ID_SeekID) ReadUInt(d, id); else if (d.id == ID_SeekPosition) ReadUInt(d, sp); }
                if (id == ID_Tracks) seekTracks = segData_ + sp; else if (id == ID_Info) seekInfo = segData_ + sp;
                else if (id == ID_Cues) seekCues = segData_ + sp;
                else if (id == ID_Attachments) seekAtt = segData_ + sp; else if (id == ID_Tags) seekTags = segData_ + sp;
            }
        } else if (h.id == ID_Info) { if (!ParseInfo(h, err)) return false; haveInfo = true; }
        else if (h.id == ID_Tracks) { if (!ParseTracks(h, err)) return false; haveTracks = true; }
        else if (h.id == ID_Cues) { if (!ParseCues(h, err)) return false; }
        else if (h.id == ID_Attachments) { if (!ParseAttachments(h, err)) return false; }
        else if (h.id == ID_Tags) { if (!ParseTags(h, err)) return false; }
        pos = h.data + h.size;
    }
    auto at = [&](uint64_t p, uint32_t id, Hdr& o) {
        std::string e2; return p && p < segEnd_ && ReadHdr(p, segEnd_, o, e2) && o.id == id && !o.unknown; };
    if (!haveInfo && at(seekInfo, ID_Info, h)) { if (!ParseInfo(h, err)) return false; }
    if (!haveTracks && at(seekTracks, ID_Tracks, h)) { if (!ParseTracks(h, err)) return false; haveTracks = true; }
    if (!haveTracks || tracks_.empty()) { err = "no Tracks element before the first Cluster (and none via SeekHead)"; return false; }
    if (!firstCluster_) { err = "no Cluster"; return false; }
    if (!cuesParsed_ && at(seekCues, ID_Cues, h)) { if (!ParseCues(h, err)) return false; }
    // Attachments / Tags written after the clusters are optional extras: a damaged one must not make the clip unplayable.
    std::string soft;
    if (!attParsed_ && at(seekAtt, ID_Attachments, h)) ParseAttachments(h, soft);
    if (!tagsParsed_ && at(seekTags, ID_Tags, h)) ParseTags(h, soft);
    Rewind();
    return true;
}

void MkvDemux::Rewind() { pos_ = firstCluster_; inCluster_ = false; pending_.clear(); pendingAt_ = 0; }

void MkvDemux::SeekNs(int64_t ns) {
    Rewind();
    for (auto& c : cues_) { if (c.timeNs > ns) break; pos_ = c.clusterPos; }
}

bool MkvDemux::ParseBlock(const Hdr& h, bool simple, bool keyHint, std::string& err) {
    if (h.size < 4) { err = "block too small at " + Hex(h.data); return false; }
    if (h.size > kMaxBlock) { err = "block larger than " + std::to_string(kMaxBlock >> 20) + " MiB at " + Hex(h.data); return false; }
    size_t hn = (size_t)std::min<uint64_t>(h.size, 12);
    const uint8_t* p = At(h.data, hn);
    if (!p) { err = "read error in block at " + Hex(h.data); return false; }
    uint64_t trackNo = 0; int tl = VInt(p, hn, trackNo);
    if (!tl || (size_t)tl + 3 > hn) { err = "bad block header at " + Hex(h.data); return false; }
    int16_t rel = (int16_t)((p[tl] << 8) | p[tl + 1]); uint8_t flags = p[tl + 2];
    int ti = TrackIndex(trackNo);
    if (ti < 0 || !tracks_[ti].enabled) return true;                 // not selected: payload never read
    const MkvTrack& t = tracks_[ti];
    if (!t.unsupported.empty()) { err = "track " + std::to_string(trackNo) + ": " + t.unsupported; return false; }
    bool key = simple ? (flags & 0x80) != 0 : keyHint;
    int lacing = (flags >> 1) & 3;
    int64_t ts = (clusterTs_ + rel) * (int64_t)tsScale_;
    uint64_t off = h.data + tl + 3, len = h.size - tl - 3;

    auto emit = [&](uint64_t fo, uint64_t fl, int64_t pts) -> bool {
        pending_.emplace_back(); MkvPacket& pk = pending_.back();
        pk.track = ti; pk.ptsNs = pts; pk.key = key;
        size_t sh = t.strippedHeader.size();
        pk.data.resize(sh + (size_t)fl);
        if (sh) memcpy(pk.data.data(), t.strippedHeader.data(), sh);
        if (!fl) return true;
        if (fl <= kWindow) { const uint8_t* q = At(fo, (size_t)fl); if (!q) return false; memcpy(pk.data.data() + sh, q, (size_t)fl); return true; }
        return io_->ReadAt(fo, pk.data.data() + sh, (size_t)fl) == fl;       // large frame: straight from disk
    };
    if (lacing == 0) { if (!emit(off, len, ts)) { err = "read error in block payload at " + Hex(off); return false; } return true; }

    if (len < 1) { err = "laced block without frame count"; return false; }
    const uint8_t* q = At(off, 1); if (!q) { err = "read error"; return false; }
    int n = q[0] + 1; ++off; --len;
    std::vector<uint64_t> sizes(n);
    if (lacing == 2) {                                               // fixed-size
        if (len % n) { err = "fixed lacing: payload not divisible by frame count"; return false; }
        for (auto& s : sizes) s = len / n;
    } else {
        size_t hb = (size_t)std::min<uint64_t>(len, (uint64_t)n * 8 + 8);   // size table upper bound (EBML) ...
        if (lacing == 1) hb = (size_t)std::min<uint64_t>(len, kWindow);      // ... Xiph sizes are 255-terminated runs
        const uint8_t* s = At(off, hb); if (!s) { err = "read error in lace header"; return false; }
        size_t i = 0; uint64_t sum = 0;
        if (lacing == 1) {                                           // Xiph
            for (int k = 0; k < n - 1; ++k) {
                uint64_t v = 0; uint8_t b;
                do { if (i >= hb) { err = "Xiph lacing: truncated size table"; return false; } b = s[i++]; v += b; } while (b == 255);
                sizes[k] = v; sum += v;
            }
        } else {                                                     // EBML
            int64_t prev = 0;
            for (int k = 0; k < n - 1; ++k) {
                uint64_t v = 0; int l = VInt(s + i, hb - i, v);
                if (!l) { err = "EBML lacing: truncated size table"; return false; }
                i += l;
                if (k == 0) prev = (int64_t)v;
                else prev += (int64_t)v - (((int64_t)1 << (7 * l - 1)) - 1);          // signed delta
                if (prev < 0) { err = "EBML lacing: negative frame size"; return false; }
                sizes[k] = (uint64_t)prev; sum += sizes[k];
            }
        }
        if (sum + i > len) { err = "laced frame sizes exceed the block"; return false; }
        sizes[n - 1] = len - i - sum; off += i;
    }
    for (int k = 0; k < n; ++k) {
        if (!emit(off, sizes[k], ts + (int64_t)(k * t.defaultDurationNs))) { err = "read error in laced frame"; return false; }
        off += sizes[k];
    }
    return true;
}

bool MkvDemux::ParseBlockGroup(const Hdr& h, std::string& err) {
    Hdr blk{}; bool haveBlk = false, hasRef = false;
    MKV_FOREACH(h, c) {
        if (c.id == ID_Block) { blk = c; haveBlk = true; }
        else if (c.id == ID_ReferenceBlock) hasRef = true;
    }
    return !haveBlk || ParseBlock(blk, false, !hasRef, err);
}

bool MkvDemux::Read(MkvPacket& out, std::string& err) {
    err.clear();
    for (;;) {
        if (pendingAt_ < pending_.size()) { std::swap(out, pending_[pendingAt_++]); return true; }
        pending_.clear(); pendingAt_ = 0;
        uint64_t limit = (inCluster_ && !clusterUnknown_) ? clusterEnd_ : segEnd_;
        if (pos_ >= limit || limit - pos_ < 2) {
            if (inCluster_ && !clusterUnknown_ && pos_ >= limit) { inCluster_ = false; continue; }
            return false;                                            // end of segment
        }
        Hdr h;
        if (!ReadHdr(pos_, limit, h, err)) return false;
        if (!inCluster_) {
            if (h.id == ID_Cluster) {
                inCluster_ = true; clusterUnknown_ = h.unknown; clusterEnd_ = h.unknown ? segEnd_ : h.data + h.size;
                clusterTs_ = 0; pos_ = h.data; continue;
            }
            if (h.unknown) { err = "unknown-size element " + Hex(h.id) + " at " + Hex(pos_); return false; }
            pos_ = h.data + h.size; continue;
        }
        if (clusterUnknown_ && (IsLevel1(h.id) || h.id == ID_Segment || h.id == ID_EBML)) { inCluster_ = false; continue; }
        if (h.unknown) { err = "unknown-size element " + Hex(h.id) + " inside a Cluster at " + Hex(pos_); return false; }
        switch (h.id) {
        case ID_Timestamp: { uint64_t v; if (!ReadUInt(h, v)) { err = "bad Cluster Timestamp"; return false; } clusterTs_ = (int64_t)v; break; }
        case ID_SimpleBlock: if (!ParseBlock(h, true, false, err)) return false; break;
        case ID_BlockGroup: if (!ParseBlockGroup(h, err)) return false; break;
        case ID_EncryptedBlock: err = "EncryptedBlock is not supported"; return false;
        }
        pos_ = h.data + h.size;
    }
}

// ---------------------------------------------------------------- H.264 ----
namespace {
struct Bits {
    const uint8_t* p; size_t n; size_t bit = 0; bool ok = true;
    uint32_t u(int c) { uint32_t v = 0; while (c--) { if (bit >= n * 8) { ok = false; return 0; } v = (v << 1) | ((p[bit >> 3] >> (7 - (bit & 7))) & 1); ++bit; } return v; }
    uint32_t ue() { int z = 0; while (ok && !u(1)) { if (++z > 31) { ok = false; return 0; } } return z ? ((1u << z) - 1 + u(z)) : 0; }
    int32_t se() { uint32_t k = ue(); return (k & 1) ? (int32_t)((k + 1) / 2) : -(int32_t)(k / 2); }
};
void Hrd(Bits& b) {
    uint32_t cnt = b.ue() + 1; b.u(4); b.u(4);
    for (uint32_t i = 0; i < cnt && b.ok && i < 32; ++i) { b.ue(); b.ue(); b.u(1); }
    b.u(5); b.u(5); b.u(5); b.u(5);
}
bool ParseSps(const std::vector<uint8_t>& nal, AvcConfig& c) {
    std::vector<uint8_t> r; r.reserve(nal.size());
    for (size_t i = 1; i < nal.size(); ++i) {                        // skip NAL header, strip emulation prevention
        if (i + 2 < nal.size() && nal[i] == 0 && nal[i + 1] == 0 && nal[i + 2] == 3) { r.push_back(0); r.push_back(0); i += 2; continue; }
        r.push_back(nal[i]);
    }
    Bits b{ r.data(), r.size() };
    uint32_t profile = b.u(8); b.u(8); b.u(8); b.ue();
    c.chromaFormat = 1; c.bitDepth = 8;
    switch (profile) { case 100: case 110: case 122: case 244: case 44: case 83: case 86: case 118: case 128: case 138: case 139: case 134: case 135: {
        c.chromaFormat = (int)b.ue(); if (c.chromaFormat == 3) b.u(1);
        c.bitDepth = 8 + (int)b.ue(); b.ue(); b.u(1);
        if (b.u(1)) for (int i = 0; i < (c.chromaFormat != 3 ? 8 : 12); ++i) if (b.u(1)) {
            int last = 8, next = 8, sz = i < 6 ? 16 : 64;
            for (int j = 0; j < sz; ++j) { if (next) next = (last + b.se() + 256) % 256; last = next ? next : last; }
        }
    } }
    b.ue();
    uint32_t poc = b.ue();
    if (poc == 0) b.ue();
    else if (poc == 1) { b.u(1); b.se(); b.se(); uint32_t n = b.ue(); for (uint32_t i = 0; i < n && b.ok && i < 256; ++i) b.se(); }
    c.numRefFrames = (int)b.ue(); b.u(1);
    uint32_t wmb = b.ue() + 1, hmu = b.ue() + 1;
    c.frameMbsOnly = b.u(1) != 0; if (!c.frameMbsOnly) b.u(1);
    b.u(1);
    uint32_t cl = 0, cr = 0, ct = 0, cb = 0;
    if (b.u(1)) { cl = b.ue(); cr = b.ue(); ct = b.ue(); cb = b.ue(); }
    c.codedW = (int)wmb * 16; c.codedH = (int)hmu * 16 * (c.frameMbsOnly ? 1 : 2);
    int ux = (c.chromaFormat == 1 || c.chromaFormat == 2) ? 2 : 1;
    int uy = (c.chromaFormat == 1 ? 2 : 1) * (c.frameMbsOnly ? 1 : 2);
    c.cropW = c.codedW - ux * (int)(cl + cr); c.cropH = c.codedH - uy * (int)(ct + cb);
    c.hasVui = b.u(1) != 0;
    if (c.hasVui) {
        if (b.u(1)) { if (b.u(8) == 255) { b.u(16); b.u(16); } }
        if (b.u(1)) b.u(1);
        if (b.u(1)) { b.u(3); c.fullRange = b.u(1) != 0; if (b.u(1)) { b.u(8); b.u(8); b.u(8); } }
        if (b.u(1)) { b.ue(); b.ue(); }
        if (b.u(1)) { b.u(32); b.u(32); b.u(1); }
        bool nh = b.u(1) != 0; if (nh) Hrd(b);
        bool vh = b.u(1) != 0; if (vh) Hrd(b);
        if (nh || vh) b.u(1);
        b.u(1);
        c.bitstreamRestriction = b.u(1) != 0;
        if (c.bitstreamRestriction) { b.u(1); b.ue(); b.ue(); b.ue(); b.ue(); c.maxNumReorder = (int)b.ue(); c.maxDecBuffering = (int)b.ue(); }
    }
    return b.ok && c.cropW > 0 && c.cropH > 0;
}
}

bool AvcParseConfig(const uint8_t* p, size_t n, AvcConfig& c, std::string& err) {
    c = AvcConfig{};
    if (n >= 4 && p[0] == 0 && p[1] == 0 && (p[2] == 1 || (p[2] == 0 && p[3] == 1))) {
        err = "CodecPrivate is Annex B, expected avcC (AVCDecoderConfigurationRecord)"; return false; }
    if (n < 7 || p[0] != 1) { err = "bad avcC"; return false; }
    c.profile = p[1]; c.compat = p[2]; c.level = p[3]; c.nalLenSize = (p[4] & 3) + 1;
    if (c.nalLenSize == 3) { err = "avcC: NAL length size 3 is invalid"; return false; }
    size_t i = 5; int ns = p[i++] & 31;
    auto sets = [&](int cnt, std::vector<std::vector<uint8_t>>& o) {
        for (int k = 0; k < cnt; ++k) {
            if (i + 2 > n) return false;
            size_t l = (p[i] << 8) | p[i + 1]; i += 2;
            if (!l || i + l > n) return false;
            o.emplace_back(p + i, p + i + l); i += l;
        }
        return true;
    };
    if (!sets(ns, c.sps) || i >= n) { err = "avcC: truncated SPS"; return false; }
    int np = p[i++];
    if (!sets(np, c.pps)) { err = "avcC: truncated PPS"; return false; }
    if (c.sps.empty() || c.pps.empty()) { err = "avcC without SPS/PPS"; return false; }
    if ((c.sps[0][0] & 31) != 7) { err = "avcC: first parameter set is not an SPS"; return false; }
    if (!ParseSps(c.sps[0], c)) { err = "SPS parse failed"; return false; }
    return true;
}

void AvcParamSetsAnnexB(const AvcConfig& c, std::vector<uint8_t>& out) {
    static const uint8_t sc[4] = { 0, 0, 0, 1 };
    for (auto* v : { &c.sps, &c.pps }) for (auto& s : *v) { out.insert(out.end(), sc, sc + 4); out.insert(out.end(), s.begin(), s.end()); }
}

bool AvcToAnnexB(const uint8_t* p, size_t n, int ls, std::vector<uint8_t>& out, bool* hasIdr) {
    static const uint8_t sc[4] = { 0, 0, 0, 1 };
    if (hasIdr) *hasIdr = false;
    size_t i = 0;
    while (i < n) {
        if (i + ls > n) return false;
        size_t l = 0; for (int k = 0; k < ls; ++k) l = (l << 8) | p[i + k];
        i += ls;
        if (!l || l > n - i) return false;
        if (hasIdr && (p[i] & 31) == 5) *hasIdr = true;
        out.insert(out.end(), sc, sc + 4); out.insert(out.end(), p + i, p + i + l);
        i += l;
    }
    return true;
}
