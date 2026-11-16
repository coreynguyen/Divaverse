#pragma once
// Matroska / EBML demuxer (read side of the Divaverse clip format, see Documentation/FORMAT.md).
// Portable C++17, no OS or third-party dependencies: all I/O goes through MkvIo.
//
// Handled:  EBML header (DocType matroska/webm, read version <= 4), Segment (known or unknown size),
//           SeekHead, Info (TimestampScale), Tracks (video/audio entries, CodecPrivate, Name, Colour.Range,
//           DefaultDuration), Cluster (known or unknown size), SimpleBlock, BlockGroup (Block +
//           ReferenceBlock -> keyframe flag), Xiph / EBML / fixed-size lacing, Cues,
//           ContentCompression header stripping (algo 3), Void / CRC-32 anywhere.
// Rejected explicitly (Open/Read return false with a message):
//           ID length > 4, size length > 8, unknown-size elements other than Segment/Cluster,
//           elements overrunning their parent or the file, ContentEncryption, ContentCompression
//           algos other than header stripping (zlib/bzlib/lzo), EncryptedBlock, blocks > kMaxBlock.
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

struct MkvIo {
    virtual ~MkvIo() = default;
    virtual uint64_t Size() = 0;
    // Reads up to n bytes at absolute offset pos. Returns bytes read (short only at EOF / error).
    virtual size_t ReadAt(uint64_t pos, void* dst, size_t n) = 0;
};

struct MkvTrack {
    uint64_t number = 0, uid = 0;
    int type = 0;                         // 1 video, 2 audio, 0x11 subtitle ...
    std::string codecId, name, language;
    std::vector<uint8_t> codecPrivate;
    int width = 0, height = 0;            // PixelWidth / PixelHeight
    uint64_t defaultDurationNs = 0;
    int colourRange = 0;                  // Matroska Colour.Range: 0 unspecified, 1 limited, 2 full
    std::vector<uint8_t> strippedHeader;  // ContentCompression algo 3 payload (prepended to every frame)
    std::string unsupported;              // non-empty -> frames of this track cannot be used
    bool enabled = true;                  // demuxer-side selection (Read skips disabled tracks)
};

struct MkvPacket {
    int track = -1;                       // index into Tracks()
    int64_t ptsNs = 0;                    // absolute presentation time
    bool key = false;
    std::vector<uint8_t> data;            // one frame (de-laced, stripped header restored)
};

struct MkvAttachment { std::string name, mime, description; uint64_t uid = 0, dataPos = 0, size = 0; };
struct MkvTag { std::string name, value; uint64_t trackUid = 0; };          // trackUid 0 = applies to the whole file

struct MkvCue { int64_t timeNs; int track; uint64_t clusterPos; };   // clusterPos = absolute file offset

class MkvDemux {
public:
    static constexpr uint64_t kMaxBlock = 64u << 20;

    bool Open(MkvIo* io, std::string& err);            // io must outlive the demuxer
    const std::vector<MkvTrack>& Tracks() const { return tracks_; }
    std::vector<MkvTrack>& Tracks() { return tracks_; }
    uint64_t TimestampScale() const { return tsScale_; }
    double DurationNs() const { return durationNs_; }
    const std::vector<MkvCue>& Cues() const { return cues_; }
    bool CuesParsed() const { return cuesParsed_; }
    std::string DocType() const { return docType_; }
    // Attachments are indexed at Open (name, MIME type, size, offset); the payload is read on demand.
    const std::vector<MkvAttachment>& Attachments() const { return attachments_; }
    const MkvAttachment* FindAttachment(const char* name) const;             // case-insensitive file name
    bool ReadAttachment(const MkvAttachment& a, std::vector<uint8_t>& out, uint64_t cap);
    const std::vector<MkvTag>& Tags() const { return tags_; }
    std::string Tag(const char* name) const;                                 // file-level tag, "" if absent
    uint64_t FileSize() const { return fileSize_; }

    // Next frame of an enabled track. false = end of stream (err empty) or error (err set).
    bool Read(MkvPacket& out, std::string& err);
    // Position on the first cluster.
    void Rewind();
    // Position on the cluster holding the last cue <= ns (any track). Without Cues: first cluster.
    // The caller discards packets until the first keyframe at/after the wanted time.
    void SeekNs(int64_t ns);

private:
    struct Hdr { uint32_t id; uint64_t size; uint64_t data; bool unknown; };   // data = payload offset
    bool ReadHdr(uint64_t pos, uint64_t limit, Hdr& h, std::string& err);
    bool Fill(uint64_t pos, size_t n);
    const uint8_t* At(uint64_t pos, size_t n);          // nullptr if not n bytes available
    bool ReadUInt(const Hdr& h, uint64_t& v);
    bool ReadFloat(const Hdr& h, double& v);
    bool ReadString(const Hdr& h, std::string& s);
    bool ReadBytes(const Hdr& h, std::vector<uint8_t>& b, uint64_t cap);
    bool ParseInfo(const Hdr& h, std::string& err);
    bool ParseTracks(const Hdr& h, std::string& err);
    bool ParseTrackEntry(const Hdr& h, std::string& err);
    bool ParseSeekHead(const Hdr& h, std::string& err);
    bool ParseCues(const Hdr& h, std::string& err);
    bool ParseAttachments(const Hdr& h, std::string& err);
    bool ParseTags(const Hdr& h, std::string& err);
    bool ParseBlock(const Hdr& h, bool simple, bool keyHint, std::string& err);
    bool ParseBlockGroup(const Hdr& h, std::string& err);
    int  TrackIndex(uint64_t number) const;

    MkvIo* io_ = nullptr; uint64_t fileSize_ = 0;
    std::vector<uint8_t> buf_; uint64_t bufPos_ = 0; size_t bufLen_ = 0;    // bounded read window
    std::string docType_;
    uint64_t segData_ = 0, segEnd_ = 0;
    uint64_t tsScale_ = 1000000; double durationNs_ = 0;
    std::vector<MkvTrack> tracks_;
    std::vector<MkvCue> cues_; bool cuesParsed_ = false;
    std::vector<MkvAttachment> attachments_; std::vector<MkvTag> tags_; bool attParsed_ = false, tagsParsed_ = false;
    uint64_t firstCluster_ = 0;
    // read cursor
    uint64_t pos_ = 0, clusterEnd_ = 0; bool inCluster_ = false, clusterUnknown_ = false;
    int64_t clusterTs_ = 0;
    std::vector<MkvPacket> pending_; size_t pendingAt_ = 0;                 // laced frames
};

// ---------- H.264 bitstream helpers (ISO/IEC 14496-15 avcC, Annex B) ----------
struct AvcConfig {
    int profile = 0, compat = 0, level = 0, nalLenSize = 4;
    std::vector<std::vector<uint8_t>> sps, pps;
    // from the first SPS
    int codedW = 0, codedH = 0, cropW = 0, cropH = 0;
    int bitDepth = 8, chromaFormat = 1;
    bool frameMbsOnly = true, fullRange = false, hasVui = false, bitstreamRestriction = false;
    int maxNumReorder = -1, maxDecBuffering = -1, numRefFrames = 0;
};
bool AvcParseConfig(const uint8_t* p, size_t n, AvcConfig& c, std::string& err);
// SPS/PPS as Annex B (00 00 00 01 start codes).
void AvcParamSetsAnnexB(const AvcConfig& c, std::vector<uint8_t>& out);
// Length-prefixed NAL units -> Annex B, appended to out. false on a malformed access unit.
bool AvcToAnnexB(const uint8_t* p, size_t n, int nalLenSize, std::vector<uint8_t>& out, bool* hasIdr = nullptr);
