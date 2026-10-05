// AVI playback using libcinepak / libindeo3 - pure C99 decoders + a
// hand-written RIFF/AVI parser.
//
// Video codecs : Cinepak (cvid), Indeo 3.x (IV32, IV31)
// Audio        : PCM (WAVEFORMATEX format_tag == 1)
// Container    : AVI 1.0 (RIFF)

#include "SDL3VideoPlayer.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace openfear::sdl3 {

// FourCC helpers - little-endian, matching AVI on-disk byte order.
static constexpr uint32_t mkfcc(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24);
}

static constexpr uint32_t K_RIFF = mkfcc('R', 'I', 'F', 'F');
static constexpr uint32_t K_LIST = mkfcc('L', 'I', 'S', 'T');
static constexpr uint32_t K_AVI = mkfcc('A', 'V', 'I', ' ');
static constexpr uint32_t K_hdrl = mkfcc('h', 'd', 'r', 'l');
static constexpr uint32_t K_avih = mkfcc('a', 'v', 'i', 'h');
static constexpr uint32_t K_strl = mkfcc('s', 't', 'r', 'l');
static constexpr uint32_t K_strh = mkfcc('s', 't', 'r', 'h');
static constexpr uint32_t K_strf = mkfcc('s', 't', 'r', 'f');
static constexpr uint32_t K_movi = mkfcc('m', 'o', 'v', 'i');
static constexpr uint32_t K_rec = mkfcc('r', 'e', 'c', ' ');
static constexpr uint32_t K_vids = mkfcc('v', 'i', 'd', 's');
static constexpr uint32_t K_auds = mkfcc('a', 'u', 'd', 's');
static constexpr uint32_t K_cvid = mkfcc('c', 'v', 'i', 'd');
static constexpr uint32_t K_IV32 = mkfcc('I', 'V', '3', '2');
static constexpr uint32_t K_IV31 = mkfcc('I', 'V', '3', '1');

// AVI on-disk structures (packed little-endian).
#pragma pack(push, 1)
struct AvihData {  // AVIMAINHEADER, 56 bytes
    uint32_t us_per_frame;
    uint32_t max_bps;
    uint32_t padding;
    uint32_t flags;
    uint32_t total_frames;
    uint32_t init_frames;
    uint32_t streams;
    uint32_t buf_size;
    uint32_t width;
    uint32_t height;
    uint32_t reserved[4];
};
struct StrhData {  // AVISTREAMHEADER, 56 bytes
    uint32_t fcc_type;
    uint32_t fcc_handler;
    uint32_t flags;
    uint16_t priority;
    uint16_t language;
    uint32_t init_frames;
    uint32_t scale;
    uint32_t rate;  // fps = rate / scale
    uint32_t start;
    uint32_t length;
    uint32_t buf_size;
    uint32_t quality;
    uint32_t sample_size;
    int16_t rc_frame[4];
};
struct BmpiData {  // BITMAPINFOHEADER, 40 bytes
    uint32_t size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bit_count;
    uint32_t compression;  // FourCC of the codec
    uint32_t size_image;
    int32_t x_pels;
    int32_t y_pels;
    uint32_t clr_used;
    uint32_t clr_important;
};
struct WaveData {         // WAVEFORMATEX (first 16 bytes, cbSize excluded)
    uint16_t format_tag;  // 1 = PCM
    uint16_t channels;
    uint32_t samples_per_sec;
    uint32_t avg_bytes_per_sec;
    uint16_t block_align;
    uint16_t bits_per_sample;
};
#pragma pack(pop)

// Stream chunk IDs: '00dc' (stream 0 compressed video), '01wb' (stream 1
// audio), etc. - first two bytes are ASCII stream-index digits, last two
// are the type ('dc'/'db'/'wb').
static int chunk_sid(uint32_t id) {
    int hi = (int)(id & 0xFF) - '0';
    int lo = (int)((id >> 8) & 0xFF) - '0';
    return (hi >= 0 && hi <= 9 && lo >= 0 && lo <= 9) ? hi * 10 + lo : -1;
}
static bool is_aud(uint32_t id) {
    return ((id >> 16) & 0xFF) == 'w' && ((id >> 24) & 0xFF) == 'b';
}
static bool is_vid(uint32_t id) {
    // Standard suffix is 'dc' (compressed) or 'db' (uncompressed); some IV31/
    // IV32 files use 'iv'/'32' instead, so those are whitelisted too. Not a
    // catch-all "anything non-audio" check: that also matches 'rec '
    // interleave-group containers, which breaks keyframe indexing.
    const uint8_t b2 = (id >> 16) & 0xFF;
    const uint8_t b3 = (id >> 24) & 0xFF;
    if (b2 == 'd' && (b3 == 'c' || b3 == 'b')) return true;
    if (b2 == 'i' && b3 == 'v') return true;
    if (b2 == '3' && b3 == '2') return true;
    return false;
}

SDL3VideoPlayer::SDL3VideoPlayer(IRenderer* renderer, IAssetProvider* asset_provider,
                                 int audio_sample_rate)
    : renderer_(renderer), asset_provider_(asset_provider), audio_sample_rate_(audio_sample_rate) {}

SDL3VideoPlayer::~SDL3VideoPlayer() {
    CloseAll();
}

bool SDL3VideoPlayer::init() {
    return true;
}
void SDL3VideoPlayer::shutdown() {
    if (audio_active_) {
        SetAudioPostMix(nullptr, nullptr);
        audio_active_ = false;
    }
    std::lock_guard<std::recursive_mutex> lock(audio_stream_mutex_);
    if (audio_stream_) {
        SDL_DestroyAudioStream(audio_stream_);
        audio_stream_ = nullptr;
    }
}

size_t SDL3VideoPlayer::Pfread(void* ptr, size_t size, size_t count) {
    if (!asset_provider_ || !file_) return 0;
    size_t got = asset_provider_->Read(file_, ptr, size * count);
    return size ? (got / size) : 0;  // mimic fread()'s item-count return
}

bool SDL3VideoPlayer::Pfseek(long offset, int whence) {
    if (!asset_provider_ || !file_) return false;
    AssetSeekOrigin origin = (whence == SEEK_SET)   ? AssetSeekOrigin::Set
                             : (whence == SEEK_CUR) ? AssetSeekOrigin::Cur
                                                    : AssetSeekOrigin::End;
    return asset_provider_->Seek(file_, offset, origin);
}

long SDL3VideoPlayer::Pftell() {
    if (!asset_provider_ || !file_) return -1;
    return static_cast<long>(asset_provider_->Tell(file_));
}

// Recursive movi/rec list scanner; fills chunks_.
void SDL3VideoPlayer::ScanMovi(long pos, long end, int vid_sid, int aud_sid) {
    while (pos + 8 <= end) {
        Pfseek(pos, SEEK_SET);
        uint32_t id = 0, sz = 0;
        if (Pfread(&id, 4, 1) != 1 || Pfread(&sz, 4, 1) != 1) break;

        long data = pos + 8;
        long next = data + (long)((sz + 1) & ~1u);  // word-aligned next chunk

        if (id == K_LIST) {
            uint32_t lt = 0;
            Pfread(&lt, 4, 1);
            if (lt == K_rec) ScanMovi(data + 4, data + (long)sz, vid_sid, aud_sid);
        } else {
            int sid = chunk_sid(id);
            if (sid >= 0) {
                bool vid = (sid == vid_sid) && is_vid(id);
                bool aud = (sid == aud_sid) && is_aud(id);
                // Zero-size video chunks mean "repeat previous frame" -
                // keep those; zero-size audio has nothing to queue.
                if (vid || (aud && sz > 0)) chunks_.push_back({data, sz, vid, false});
            }
        }

        pos = next;
    }
}

// Reads headers and builds the chunk index.
bool SDL3VideoPlayer::ParseAvi() {
    Pfseek(0, SEEK_SET);

    uint32_t id = 0, sz = 0, lt = 0;
    auto r4 = [&](uint32_t& v) { return Pfread(&v, 4, 1) == 1; };

    if (!r4(id) || id != K_RIFF) return false;
    r4(sz);
    if (!r4(lt) || lt != K_AVI) return false;

    long file_end = 12L + (long)sz;

    int vid_sid = -1;
    int aud_sid = -1;
    int n_streams = 0;
    long movi_pos = 0;
    long movi_sz = 0;
    bool got_movi = false;
    bool got_main_fps = false;
    uint32_t main_us_per_frame = 0;
    uint32_t main_total_frames = 0;

    // Level 1: top-level chunks inside the RIFF.
    for (long p1 = 12; p1 + 8 <= file_end;) {
        Pfseek(p1, SEEK_SET);
        r4(id);
        r4(sz);
        long d1 = p1 + 8;
        long n1 = d1 + (long)((sz + 1) & ~1u);

        if (id == K_LIST) {
            r4(lt);

            if (lt == K_hdrl) {
                // Level 2: header list.
                long p2 = d1 + 4;
                long e2 = d1 + (long)sz;
                while (p2 + 8 <= e2) {
                    Pfseek(p2, SEEK_SET);
                    uint32_t id2 = 0, sz2 = 0;
                    r4(id2);
                    r4(sz2);
                    long d2 = p2 + 8;
                    long n2 = d2 + (long)((sz2 + 1) & ~1u);

                    if (id2 == K_avih && sz2 >= sizeof(AvihData)) {
                        AvihData ah{};
                        Pfread(&ah, sizeof(ah), 1);
                        if (ah.us_per_frame > 0) {
                            vid_fps_ = 1e6 / (double)ah.us_per_frame;
                            got_main_fps = true;
                            main_us_per_frame = ah.us_per_frame;
                            main_total_frames = ah.total_frames;
                            avi_total_frames_ = (int)ah.total_frames;
                        }

                    } else if (id2 == K_LIST) {
                        uint32_t lt2 = 0;
                        r4(lt2);
                        if (lt2 == K_strl) {
                            // Level 3: stream list.
                            int stream_idx = n_streams++;
                            long p3 = d2 + 4;
                            long e3 = d2 + (long)sz2;
                            StrhData sh{};

                            while (p3 + 8 <= e3) {
                                Pfseek(p3, SEEK_SET);
                                uint32_t id3 = 0, sz3 = 0;
                                r4(id3);
                                r4(sz3);
                                long d3 = p3 + 8;
                                long n3 = d3 + (long)((sz3 + 1) & ~1u);

                                if (id3 == K_strh && sz3 >= sizeof(StrhData)) {
                                    Pfread(&sh, sizeof(sh), 1);
                                } else if (id3 == K_strf) {
                                    if (sh.fcc_type == K_vids && vid_sid < 0) {
                                        if (sz3 >= sizeof(BmpiData)) {
                                            BmpiData bm{};
                                            Pfread(&bm, sizeof(bm), 1);
                                            vid_w_ = (int)bm.width;
                                            vid_h_ =
                                                bm.height < 0 ? -(int)bm.height : (int)bm.height;
                                            vid_fcc_ =
                                                bm.compression ? bm.compression : sh.fcc_handler;
                                        }
                                        vid_sid = stream_idx;
                                        if (!got_main_fps && sh.scale > 0 && sh.rate > 0)
                                            vid_fps_ = (double)sh.rate / sh.scale;

                                    } else if (sh.fcc_type == K_auds && aud_sid < 0) {
                                        if (sz3 >= sizeof(WaveData)) {
                                            WaveData wv{};
                                            Pfread(&wv, sizeof(wv), 1);
                                            aud_tag_ = wv.format_tag;
                                            aud_ch_ = wv.channels;
                                            aud_rate_ = wv.samples_per_sec;
                                            aud_bits_ = wv.bits_per_sample;
                                            has_audio_ = (wv.format_tag == 1);
                                        }
                                        aud_sid = stream_idx;
                                    }
                                }
                                p3 = n3;
                            }
                        }
                    }
                    p2 = n2;
                }

            } else if (lt == K_movi) {
                movi_pos = d1 + 4;  // right after 'movi' fourcc
                movi_sz = (long)sz - 4;
                got_movi = true;
            }
        }
        p1 = n1;
    }

    if (!got_movi || vid_sid < 0 || vid_w_ <= 0 || vid_h_ <= 0) return false;

    if (!TryBuildChunksFromIdx1(movi_pos, file_end, vid_sid, aud_sid)) {
        // No usable idx1 - walk the movi data chunk by chunk (slow for
        // large files, but the only option without a real index), then
        // the Cinepak-byte-sniffing keyframe fallback inside ParseIdx1.
        ScanMovi(movi_pos, movi_pos + movi_sz, vid_sid, aud_sid);
        ParseIdx1(file_end);
    }

    if (main_us_per_frame > 0 && main_total_frames > 0) {
        int indexed_video_frames = 0;
        for (const AviChunk& ch : chunks_)
            if (ch.is_video) ++indexed_video_frames;

        if (indexed_video_frames > 0) {
            const double timeline_s = (double)main_total_frames * (double)main_us_per_frame / 1e6;
            if (timeline_s > 0.0) vid_fps_ = (double)indexed_video_frames / timeline_s;
        }
    }

    return !chunks_.empty();
}

// Fast path: builds chunks_+keyframes_ straight from the AVI's own idx1
// index instead of ScanMovi()'s chunk-by-chunk walk. idx1 entries already
// carry (ckid, flags, offset, size) for every real chunk in movi order -
// the same fields ScanMovi derives one seek+read at a time. Same
// inclusion rules as ScanMovi, so the resulting list matches exactly.
bool SDL3VideoPlayer::TryBuildChunksFromIdx1(long movi_data_start, long file_end, int vid_sid,
                                             int aud_sid) {
    static constexpr uint32_t K_idx1 = mkfcc('i', 'd', 'x', '1');
    static constexpr uint32_t AVIIF_KEYFRAME = 0x10;

    Pfseek(0, SEEK_END);
    long fsize = Pftell();
    if (file_end > fsize) file_end = fsize;

    // Locate idx1 (same top-level walk as ParseIdx1's own search).
    long p = 12;
    long idx1_pos = -1;
    long idx1_sz = 0;
    while (p + 8 <= file_end) {
        Pfseek(p, SEEK_SET);
        uint32_t id = 0, sz = 0;
        if (Pfread(&id, 4, 1) != 1 || Pfread(&sz, 4, 1) != 1) break;
        long data = p + 8;
        long next = data + (long)((sz + 1) & ~1u);
        if (id == K_idx1) {
            idx1_pos = data;
            idx1_sz = (long)sz;
            break;
        }
        p = next;
    }
    if (idx1_pos < 0 || idx1_sz < 16) return false;

    // idx1's dwChunkOffset base isn't consistently defined across writers:
    // absolute from file start (base=0), relative to movi's data start, or
    // relative to the 'movi' LIST fourcc itself (base=movi_data_start-4,
    // the classic AVI1.0 'rec '-interleaved convention this project's own
    // assets use). Rather than guess, self-verify: take the first real
    // data-chunk entry (skipping 'rec ' group entries, whose ckid lies
    // about what's at that position) and check which base actually has
    // that ckid's fourcc sitting at (base+offset) in the file.
    //
    // Per spec, idx1's offset points at the chunk's own 8-byte header, not
    // its data - chunks_ stores data-start like ScanMovi, so every offset
    // built here needs +8 added.
    Pfseek(idx1_pos, SEEK_SET);
    long idx_end = idx1_pos + idx1_sz;
    uint32_t sniff_ckid = 0, sniff_off = 0;
    bool have_sniff = false;
    while (Pftell() + 16 <= idx_end) {
        uint32_t ckid = 0, flags = 0, off = 0, sz = 0;
        if (Pfread(&ckid, 4, 1) != 1) break;
        if (Pfread(&flags, 4, 1) != 1) break;
        if (Pfread(&off, 4, 1) != 1) break;
        if (Pfread(&sz, 4, 1) != 1) break;
        if (chunk_sid(ckid) >= 0 && (is_vid(ckid) || is_aud(ckid))) {
            sniff_ckid = ckid;
            sniff_off = off;
            have_sniff = true;
            break;
        }
    }
    if (!have_sniff) return false;

    auto ckid_matches_at = [&](long candidate_base) -> bool {
        long pos = candidate_base + (long)sniff_off;
        if (pos < 0 || pos + 4 > file_end) return false;
        Pfseek(pos, SEEK_SET);
        uint32_t id = 0;
        return Pfread(&id, 4, 1) == 1 && id == sniff_ckid;
    };

    long base;
    if (ckid_matches_at(movi_data_start - 4)) {
        base = movi_data_start - 4;
    } else if (ckid_matches_at(movi_data_start)) {
        base = movi_data_start;
    } else if (ckid_matches_at(0)) {
        base = 0;
    } else {
        // No candidate base's fourcc matched - guess from which side of
        // movi_data_start the raw offset falls on rather than refuse outright.
        base = ((long)sniff_off >= movi_data_start) ? 0L : movi_data_start;
    }

    Pfseek(idx1_pos, SEEK_SET);
    int vf = 0;

    std::vector<AviChunk> new_chunks;
    std::vector<KeyframeEntry> new_keyframes;
    new_chunks.reserve((size_t)(idx1_sz / 16));

    while (Pftell() + 16 <= idx_end) {
        uint32_t ckid = 0, flags = 0, off = 0, sz = 0;
        if (Pfread(&ckid, 4, 1) != 1) break;
        if (Pfread(&flags, 4, 1) != 1) break;
        if (Pfread(&off, 4, 1) != 1) break;
        if (Pfread(&sz, 4, 1) != 1) break;

        int sid = chunk_sid(ckid);
        if (sid < 0) continue;  // e.g. 'rec ' interleave-group entries

        bool vid = (sid == vid_sid) && is_vid(ckid);
        bool aud = (sid == aud_sid) && is_aud(ckid);
        if (!vid && !(aud && sz > 0)) continue;

        new_chunks.push_back({base + (long)off + 8, sz, vid, false});
        if (vid) {
            if (flags & AVIIF_KEYFRAME) {
                new_chunks.back().is_key = true;
                new_keyframes.push_back({vf, (int)new_chunks.size() - 1});
            }
            ++vf;
        }
    }

    if (new_chunks.empty()) return false;
    if (new_keyframes.empty()) new_keyframes.push_back({0, 0});

    chunks_ = std::move(new_chunks);
    keyframes_ = std::move(new_keyframes);
    return true;
}

// Reads the AVI legacy index (AVIOLDINDEX: ckid, flags, offset, size per
// entry) and marks keyframes in chunks_. After this, keyframes_ holds
// (video_frame_num, chunk_idx) sorted ascending.
void SDL3VideoPlayer::ParseIdx1(long file_end) {
    static constexpr uint32_t K_idx1 = mkfcc('i', 'd', 'x', '1');
    static constexpr uint32_t AVIIF_KEYFRAME = 0x10;

    Pfseek(0, SEEK_END);
    long fsize = Pftell();
    if (file_end > fsize) file_end = fsize;

    // idx1 is typically the last top-level chunk after LIST movi.
    long p = 12;
    long idx1_pos = -1;
    long idx1_sz = 0;
    while (p + 8 <= file_end) {
        Pfseek(p, SEEK_SET);
        uint32_t id = 0, sz = 0;
        if (Pfread(&id, 4, 1) != 1 || Pfread(&sz, 4, 1) != 1) break;
        long data = p + 8;
        long next = data + (long)((sz + 1) & ~1u);
        if (id == K_idx1) {
            idx1_pos = data;
            idx1_sz = (long)sz;
            break;
        }
        p = next;
    }

    if (idx1_pos < 0 || idx1_sz < 16) {
        // No idx1: fall back to detecting keyframes from Cinepak's own
        // frame data (first byte 0x00 = keyframe) - only valid for that
        // codec, so other codecs just assume frame 0 is the sole keyframe.
        int vf = 0;
        for (int ci = 0; ci < (int)chunks_.size(); ++ci) {
            AviChunk& ch = chunks_[ci];
            if (!ch.is_video) continue;
            if (vid_fcc_ == K_cvid && ch.size > 0) {
                Pfseek(ch.offset, SEEK_SET);
                uint8_t flag = 0;
                if (Pfread(&flag, 1, 1) == 1) ch.is_key = (flag == 0x00);
            }
            if (ch.is_key) keyframes_.push_back({vf, ci});
            ++vf;
        }
        if (keyframes_.empty()) keyframes_.push_back({0, 0});  // assume frame 0 is key
        return;
    }

    Pfseek(idx1_pos, SEEK_SET);
    long idx_end = idx1_pos + idx1_sz;
    int vf = 0;
    int ci_hint = 0;  // sequential match hint (idx1 and movi share order)

    while (Pftell() + 16 <= idx_end) {
        uint32_t ckid = 0, flags = 0, off = 0, sz = 0;
        if (Pfread(&ckid, 4, 1) != 1) break;
        if (Pfread(&flags, 4, 1) != 1) break;
        if (Pfread(&off, 4, 1) != 1) break;
        if (Pfread(&sz, 4, 1) != 1) break;

        // Skips non-stream ckids (chunk_sid < 0, e.g. 'rec ' interleave-
        // group entries) and anything not video-shaped. Doesn't check
        // against vid_sid specifically - ci_hint below pairs idx1's video
        // entries, in order, against chunks_'s own already-filtered list.
        if (chunk_sid(ckid) < 0 || !is_vid(ckid)) continue;

        while (ci_hint < (int)chunks_.size() && !chunks_[ci_hint].is_video) ++ci_hint;
        if (ci_hint >= (int)chunks_.size()) break;

        bool key = (flags & AVIIF_KEYFRAME) != 0;
        chunks_[ci_hint].is_key = key;
        if (key) keyframes_.push_back({vf, ci_hint});

        ++ci_hint;
        ++vf;
    }

    if (keyframes_.empty()) keyframes_.push_back({0, 0});
}

bool SDL3VideoPlayer::AllocDec() {
    if (vid_fcc_ == K_cvid) {
        cpk_ = cinepak_alloc(vid_w_, vid_h_);
        return cpk_ != nullptr;
    }
    if (vid_fcc_ == K_IV32 || vid_fcc_ == K_IV31) {
        iv3_ = indeo3_alloc(vid_w_, vid_h_);
        return iv3_ != nullptr;
    }
    char buf[5]{};
    std::memcpy(buf, &vid_fcc_, 4);
    std::fprintf(stderr, "[video] unsupported codec: %.4s\n", buf);
    return false;
}

bool SDL3VideoPlayer::OpenAudio() {
    if (!has_audio_ || aud_rate_ == 0 || aud_ch_ == 0) return false;

    const SDL_AudioFormat src_format = (aud_bits_ <= 8) ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE;
    // Destination must match the audio backend's real output rate exactly -
    // the format SetAudioPostMix()'s callback receives its buffer in. Uses
    // audio_sample_rate_ (IAudio::sampleRate(), passed in at construction)
    // instead of a hardcoded constant, which would go stale silently the
    // moment a backend's rate changed, with no compiler error to catch it.
    SDL_AudioSpec src_spec{};
    src_spec.format = src_format;
    src_spec.channels = static_cast<int>(aud_ch_);
    src_spec.freq = static_cast<int>(aud_rate_);
    SDL_AudioSpec dst_spec{};
    dst_spec.format = SDL_AUDIO_S16;
    dst_spec.channels = 2;
    dst_spec.freq = audio_sample_rate_;
    audio_stream_ = SDL_CreateAudioStream(&src_spec, &dst_spec);
    if (!audio_stream_) std::fprintf(stderr, "[video] audio stream: %s\n", SDL_GetError());
    return audio_stream_ != nullptr;
}

// Adds our converted AVI PCM on top of whatever SDL3Audio already mixed
// into `stream` (sound effects, etc.) instead of overwriting it. stream/len
// are always S16SYS/2ch at audio_sample_rate_ Hz here regardless of the
// AVI's own format (see OpenAudio()).
void SDL3VideoPlayer::MixCallback(void* udata, Uint8* stream, int len) {
    auto* self = static_cast<SDL3VideoPlayer*>(udata);
    std::lock_guard<std::recursive_mutex> lock(self->audio_stream_mutex_);
    if (!self->audio_stream_) return;

    // Own buffer, not tmp_: this runs on SDL3Audio's audio thread,
    // concurrently with the main thread's decode loop, which reuses tmp_.
    std::vector<uint8_t>& buf = self->mix_buf_;
    if ((int)buf.size() < len) buf.resize(len);
    int got = SDL_GetAudioStreamData(self->audio_stream_, buf.data(), len);
    if (got <= 0) return;

    auto* dst = reinterpret_cast<int16_t*>(stream);
    auto* src = reinterpret_cast<int16_t*>(buf.data());
    const int n_samples = got / (int)sizeof(int16_t);
    for (int i = 0; i < n_samples; ++i) {
        int mixed = (int)dst[i] + (int)src[i];
        dst[i] = (int16_t)std::clamp(mixed, -32768, 32767);
    }
}

bool SDL3VideoPlayer::open(std::string_view path) {
    CloseAll();

    if (!asset_provider_) return false;

    std::string p(path);
    current_path_ = p;
    file_ = asset_provider_->Open(p);
    if (!file_) {
        std::fprintf(stderr, "[video] cannot open: %s\n", p.c_str());
        return false;
    }

    if (!ParseAvi()) {
        std::fprintf(stderr, "[video] bad AVI: %s\n", p.c_str());
        CloseAll();
        return false;
    }
    if (!AllocDec()) {
        CloseAll();
        return false;
    }

    stride_ = vid_w_ * 4;
    // +4 extra rows so paint_v1/paint_v4 never write past the buffer when
    // vid_h_ isn't a multiple of 4 (e.g. 480x98).
    rgba_.assign((size_t)((vid_h_ + 4) * stride_), 0);

    // createStreamingTexture() picks whatever pixel format the backend
    // actually supports and updateTexture() converts into it - no per-
    // platform format branching needed here (see IRenderer.h's contract:
    // rgba_ is always fed in as RGBA32 byte order, matching what
    // DecodeFrame() produces natively).
    texture_ = renderer_->createStreamingTexture(vid_w_, vid_h_);
    if (!texture_) {
        std::fprintf(stderr, "[video] createStreamingTexture failed: %s\n", p.c_str());
        CloseAll();
        return false;
    }
    // Backends don't promise zeroed initial texture content - some leave
    // whatever a previous video (on this same shared instance) last
    // rendered. Push our zero-filled rgba_ in immediately so there's no
    // window where stale content briefly shows.
    renderer_->updateTexture(texture_, rgba_.data(), stride_);

    OpenAudio();  // failure is non-fatal - video plays silently

    status_.store(VideoStatus::Stopped);
    return true;
}

void SDL3VideoPlayer::play(bool fullscreen_overlay) {
    if (status_.load() == VideoStatus::Idle) return;
    fullscreen_ = fullscreen_overlay;
    next_chunk_ = 0;
    vid_frame_ = 0;
    lag_reported_ = false;
    if (audio_stream_) {
        {
            std::lock_guard<std::recursive_mutex> lock(audio_stream_mutex_);
            SDL_ClearAudioStream(audio_stream_);
        }
        if (!audio_active_) {
            SetAudioPostMix(&SDL3VideoPlayer::MixCallback, this);
            audio_active_ = true;
        }
    }
    start_ms_ = SDL_GetTicks();
    status_.store(VideoStatus::Playing);
}

void SDL3VideoPlayer::resume() {
    if (status_.load() != VideoStatus::Paused) return;
    // Recompute start_ms_ so the wall-clock timer stays consistent with
    // how far into the stream vid_frame_ already is.
    const uint64_t elapsed_ms = (uint64_t)(vid_frame_ / vid_fps_ * 1000.0);
    start_ms_ = SDL_GetTicks() - elapsed_ms;
    if (audio_stream_ && !audio_active_) {
        SetAudioPostMix(&SDL3VideoPlayer::MixCallback, this);
        audio_active_ = true;
    }
    status_.store(VideoStatus::Playing);
}

void SDL3VideoPlayer::pause() {
    if (status_.load() != VideoStatus::Playing) return;
    if (audio_active_) {
        SetAudioPostMix(nullptr, nullptr);
        audio_active_ = false;
    }
    status_.store(VideoStatus::Paused);
}

void SDL3VideoPlayer::stop() {
    if (audio_active_) {
        SetAudioPostMix(nullptr, nullptr);
        audio_active_ = false;
    }
    if (audio_stream_) {
        std::lock_guard<std::recursive_mutex> lock(audio_stream_mutex_);
        SDL_ClearAudioStream(audio_stream_);
    }
    next_chunk_ = 0;
    vid_frame_ = 0;
    status_.store(VideoStatus::Stopped);
}

void SDL3VideoPlayer::close() {
    CloseAll();
}

void SDL3VideoPlayer::CloseAll() {
    status_.store(VideoStatus::Idle);
    if (audio_active_) {
        SetAudioPostMix(nullptr, nullptr);
        audio_active_ = false;
    }
    {
        std::lock_guard<std::recursive_mutex> lock(audio_stream_mutex_);
        if (audio_stream_) {
            SDL_DestroyAudioStream(audio_stream_);
            audio_stream_ = nullptr;
        }
    }
    if (texture_) {
        renderer_->destroyTexture(texture_);
        texture_ = 0;
    }
    cinepak_free(cpk_);
    cpk_ = nullptr;
    indeo3_free(iv3_);
    iv3_ = nullptr;
    if (file_ && asset_provider_) {
        asset_provider_->Close(file_);
        file_ = 0;
    }
    chunks_.clear();
    keyframes_.clear();
    rgba_.clear();
    tmp_.clear();
    vid_w_ = vid_h_ = 0;
    vid_fcc_ = 0;
    next_chunk_ = vid_frame_ = 0;
    has_audio_ = false;
}

double SDL3VideoPlayer::ElapsedS() const {
    return static_cast<double>(SDL_GetTicks() - start_ms_) * 1e-3;
}

void SDL3VideoPlayer::DecodeFrame(const uint8_t* data, uint32_t sz) {
    if (!data || sz == 0) return;
    int rc = 0;
    if (cpk_)
        rc = cinepak_decode(cpk_, data, (int)sz, rgba_.data(), stride_);
    else if (iv3_) {
        rc = indeo3_decode(iv3_, data, (int)sz, rgba_.data(), stride_);
        if (rc != 0) fprintf(stderr, "[iv3player] frame %d decode error rc=%d\n", vid_frame_, rc);
    }
}

void SDL3VideoPlayer::renderFrame() {
    if (status_.load() == VideoStatus::Playing) {
        const double now = ElapsedS();
        const int target = (int)(now * vid_fps_);  // frame due now

        // Diagnostic: a soft stutter (disk I/O falling behind the video's
        // own framerate) never fails a read or decode call, so it leaves no
        // trace in any existing log line - just late frames. Flag it
        // directly: log once per stall episode if decode falls more than
        // half a second behind wall-clock time.
        if (next_chunk_ < (int)chunks_.size() && target - vid_frame_ > (int)(vid_fps_ * 0.5) &&
            !lag_reported_) {
            lag_reported_ = true;
            std::fprintf(
                stderr, "[video] %s stalled: frame %d due, only %d decoded (%.2fs behind)\n",
                current_path_.c_str(), target, vid_frame_, (target - vid_frame_) / vid_fps_);
        } else if (target - vid_frame_ <= 1) {
            lag_reported_ = false;  // caught up - re-arm for the next stall
        }

        // Consume chunks until caught up to wall-clock time. Audio chunks
        // queue immediately, capped at kMaxQueuedAheadBytes ahead of
        // playback - nothing else bounds SDL_AudioStream's buffer, so an
        // uncapped catchup loop (e.g. after a stall) could queue unbounded
        // audio. Excess chunks just wait for the next tick, not lost.
        // Seconds, not a byte constant, so it scales with the real backend
        // rate; kept generous so disk jitter doesn't starve the resampler
        // into audible underrun.
        constexpr double kMaxQueuedAheadSeconds = 3.0;
        const int kMaxQueuedAheadBytes = (int)(audio_sample_rate_ * 2 /*channels*/ *
                                               (int)sizeof(int16_t) * kMaxQueuedAheadSeconds);
        while (next_chunk_ < (int)chunks_.size()) {
            const AviChunk& ch = chunks_[next_chunk_];

            if (ch.is_video && vid_frame_ > target) break;
            if (audio_stream_) {
                std::lock_guard<std::recursive_mutex> lock(audio_stream_mutex_);
                if (SDL_GetAudioStreamAvailable(audio_stream_) >= kMaxQueuedAheadBytes) break;
            }

            ++next_chunk_;
            if (ch.size == 0) {
                // Empty video chunk = duplicate previous frame. For IV3,
                // decode with size=0 so it copies cur->ref, keeping the
                // next INTER frame's reference buffer correct.
                if (ch.is_video) {
                    if (iv3_) {
                        indeo3_decode(iv3_, nullptr, 0, rgba_.data(), stride_);
                    }
                    ++vid_frame_;
                }
                continue;
            }

            if (ch.size > (uint32_t)tmp_.size()) tmp_.resize(ch.size);

            Pfseek(ch.offset, SEEK_SET);
            if (Pfread(tmp_.data(), 1, ch.size) != ch.size) break;

            if (ch.is_video) {
                DecodeFrame(tmp_.data(), ch.size);
                renderer_->updateTexture(texture_, rgba_.data(), stride_);
                ++vid_frame_;
            } else if (audio_stream_) {
                std::lock_guard<std::recursive_mutex> lock(audio_stream_mutex_);
                SDL_PutAudioStreamData(audio_stream_, tmp_.data(), (int)ch.size);
            }
        }

        if (next_chunk_ >= (int)chunks_.size()) {
            // Don't cut audio off the instant the file's chunks run out -
            // SDL_PutAudioStreamData() queues ahead of what's actually
            // played, so unregistering MixCallback here truncates the
            // waveform mid-sample - audible as a click right at the end.
            // Wait for the queue to actually drain first.
            long remaining = 0;
            if (audio_stream_) {
                std::lock_guard<std::recursive_mutex> lock(audio_stream_mutex_);
                remaining = SDL_GetAudioStreamAvailable(audio_stream_);
            }
            if (remaining <= 0) {
                status_.store(VideoStatus::Stopped);
                if (audio_active_) {
                    SetAudioPostMix(nullptr, nullptr);
                    audio_active_ = false;
                }
            }
        }
    }

    if (!texture_) return;

    // IRenderer's coordinate space is always a fixed 640x480 (see its own
    // doc comment) - no need to query an actual renderer/window size here.
    constexpr int kLogicalW = 640;
    constexpr int kLogicalH = 480;

    SDL_Rect dst;
    if (use_custom_rect_) {
        dst = custom_rect_;
    } else if (fullscreen_) {
        // Centered, aspect-preserving, letterboxed to the logical space.
        float scale = std::min((float)kLogicalW / (float)vid_w_, (float)kLogicalH / (float)vid_h_);
        int fw = (int)(vid_w_ * scale);
        int fh = (int)(vid_h_ * scale);
        dst = {(kLogicalW - fw) / 2, (kLogicalH - fh) / 2, fw, fh};
    } else {
        // Native size, centered.
        dst = {(kLogicalW - vid_w_) / 2, (kLogicalH - vid_h_) / 2, vid_w_, vid_h_};
    }

    // drawBitmapScaled() (not drawBitmap()) since dst can differ from the
    // video's native size (fullscreen letterboxing, or an explicit
    // setRenderRect() the game called with its own dimensions). Routing
    // through IRenderer means Ps2Renderer's Y-crop offset (already applied
    // to every other texture in the game) applies here for free.
    renderer_->drawBitmapScaled(texture_, dst.x, dst.y, dst.w, dst.h, 0, 0, vid_w_, vid_h_);
}

void SDL3VideoPlayer::seekFrame(int frame_number) {
    if (!file_ || chunks_.empty() || frame_number < 0) return;

    if (vid_frame_ == frame_number + 1) return;  // already there

    // Find the nearest keyframe at or before target.
    int kf_video = 0, kf_chunk = 0;
    if (!keyframes_.empty()) {
        int lo = 0, hi = (int)keyframes_.size() - 1;
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            if (keyframes_[mid].video_frame <= frame_number)
                lo = mid;
            else
                hi = mid - 1;
        }
        kf_video = keyframes_[lo].video_frame;
        kf_chunk = keyframes_[lo].chunk_idx;
    }

    // Jump to the keyframe if it's a better starting point than the
    // current position: always on backward seek, or on forward seek when
    // the keyframe is ahead of where we are (skips redundant decoding).
    if (frame_number < vid_frame_ || kf_video > vid_frame_) {
        vid_frame_ = kf_video;
        next_chunk_ = kf_chunk;
    }

    // Decode from the current position up to and including the target.
    while (next_chunk_ < (int)chunks_.size()) {
        if (chunks_[next_chunk_].is_video && vid_frame_ > frame_number) break;

        const AviChunk& ch = chunks_[next_chunk_++];
        if (ch.size == 0) {
            if (ch.is_video) {
                if (iv3_) {
                    indeo3_decode(iv3_, nullptr, 0, rgba_.data(), stride_);
                    renderer_->updateTexture(texture_, rgba_.data(), stride_);
                }
                ++vid_frame_;
            }
            continue;
        }

        if (ch.size > (uint32_t)tmp_.size()) tmp_.resize(ch.size);

        Pfseek(ch.offset, SEEK_SET);
        if (Pfread(tmp_.data(), 1, ch.size) != ch.size) break;

        if (ch.is_video) {
            DecodeFrame(tmp_.data(), ch.size);
            renderer_->updateTexture(texture_, rgba_.data(), stride_);
            ++vid_frame_;
        }
        // Audio skipped during seek.
    }

    start_ms_ = SDL_GetTicks() - (uint64_t)(frame_number / vid_fps_ * 1000.0);
}

void SDL3VideoPlayer::setRenderRect(int x, int y, int width, int height) {
    custom_rect_ = {x, y, width, height};
    use_custom_rect_ = (width > 0 && height > 0);
}

}  // namespace openfear::sdl3
