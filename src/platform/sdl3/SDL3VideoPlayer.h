#pragma once
#include "openfear/platform/IVideoPlayer.h"
#include "openfear/platform/IRenderer.h"
#include "openfear/assets/IAssetProvider.h"
#include "SDL3Audio.h"
#include <SDL3/SDL.h>
extern "C" {
#include "cinepak.h"
#include "indeo3.h"
}
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

namespace openfear::sdl3 {

// One entry in the sequential chunk index built from the AVI movi list.
struct AviChunk {
    long offset;  // absolute file offset of chunk data (after the 8-byte header)
    uint32_t size;
    bool is_video;  // false = PCM audio chunk
    bool is_key;    // keyframe (safe to decode without prior state)
};

// Keyframe index entry: which video frame number, and which index in chunks_.
struct KeyframeEntry {
    int video_frame;
    int chunk_idx;
};

// AVI player using libcinepak/libindeo3. Video: cvid (Cinepak), IV31/IV32
// (Indeo 3.x). Audio: PCM only (WAVEFORMATEX format_tag == 1).
class SDL3VideoPlayer final : public IVideoPlayer {
public:
    // audio_sample_rate: the audio backend's actual mix/output rate (see
    // IAudio::sampleRate()) - AVI audio gets converted to this rate before
    // mixing in via SetAudioPostMix(), so it must match exactly regardless
    // of platform (see OpenAudio()).
    SDL3VideoPlayer(IRenderer* renderer, IAssetProvider* asset_provider, int audio_sample_rate);
    ~SDL3VideoPlayer() override;

    bool init() override;
    void shutdown() override;

    bool open(std::string_view path) override;
    void play(bool fullscreen_overlay = true) override;
    void resume() override;
    void pause() override;
    void stop() override;
    void close() override;

    VideoStatus status() const override { return status_.load(); }

    // Decodes/displays the next due video frame; queues any audio chunks
    // encountered along the way. Call once per game tick while Playing.
    void renderFrame() override;

    // Decodes from the nearest keyframe up to and including the target,
    // to keep decoder state correct (0-indexed).
    void seekFrame(int frame_number) override;

    // Overrides the default centered/fullscreen destination rect.
    void setRenderRect(int x, int y, int width, int height) override;

private:
    IRenderer* renderer_ = nullptr;
    TextureHandle texture_ = 0;
    bool fullscreen_ = true;

    // AVI audio mixes into SDL3Audio's own output via SetAudioPostMix()
    // instead of a separate SDL_OpenAudioDeviceStream - this project only
    // ever has one open audio device (already claimed by SDL3Audio::init()
    // for WAV sound effects), on every backend. The stream itself converts
    // the AVI's own rate/channels/bit-depth into the audio backend's real
    // output format (see OpenAudio()).
    SDL_AudioStream* audio_stream_ = nullptr;
    bool audio_active_ = false;     // this instance is the registered postmix source
    std::vector<uint8_t> mix_buf_;  // MixCallback's own scratch buffer (audio thread)
    // recursive_mutex: under a non-pthread Emscripten build there's no real
    // audio thread, so MixCallback can fire synchronously on the same
    // thread as the caller - a plain mutex would deadlock there.
    std::recursive_mutex audio_stream_mutex_;
    static void SDLCALL MixCallback(void* udata, Uint8* stream, int len);

    SDL_Rect custom_rect_{0, 0, 0, 0};
    bool use_custom_rect_ = false;

    IAssetProvider* asset_provider_ = nullptr;
    AssetHandle file_ = 0;
    std::string current_path_;
    int audio_sample_rate_ = 44100;  // see constructor doc / OpenAudio()

    int vid_w_ = 0;
    int vid_h_ = 0;
    uint32_t vid_fcc_ = 0;
    double vid_fps_ = 15.0;
    int avi_total_frames_ = 0;  // dwTotalFrames from AVIH header

    uint16_t aud_tag_ = 0;  // 1 = PCM
    uint16_t aud_ch_ = 0;
    uint32_t aud_rate_ = 0;
    uint16_t aud_bits_ = 0;
    bool has_audio_ = false;

    // Sequential chunk list (populated by ScanMovi or the idx1 fast path).
    std::vector<AviChunk> chunks_;
    int next_chunk_ = 0;
    int vid_frame_ = 0;
    bool lag_reported_ = false;  // stall diagnostic latch, see renderFrame()

    // Keyframe index, sorted ascending by video_frame - used by seekFrame().
    std::vector<KeyframeEntry> keyframes_;

    CinepakContext* cpk_ = nullptr;
    Indeo3Context* iv3_ = nullptr;

    std::vector<uint8_t> rgba_;
    int stride_ = 0;
    std::vector<uint8_t> tmp_;  // reusable chunk read buffer

    std::atomic<VideoStatus> status_{VideoStatus::Idle};
    uint64_t start_ms_ = 0;

    // fread/fseek/ftell equivalents routed through asset_provider_/file_
    // instead of libc, so the parsing logic below doesn't change shape.
    size_t Pfread(void* ptr, size_t size, size_t count);
    bool Pfseek(long offset, int whence);
    long Pftell();

    bool ParseAvi();
    void ScanMovi(long pos, long end, int vid_sid, int aud_sid);
    void ParseIdx1(long file_end);
    // Fast path: builds chunks_+keyframes_ directly from the AVI's own idx1
    // index instead of ScanMovi()'s chunk-by-chunk walk of the whole movi
    // data. Returns false (chunks_/keyframes_ untouched) if idx1 is
    // missing/malformed, so callers fall back to ScanMovi()+ParseIdx1().
    bool TryBuildChunksFromIdx1(long movi_data_start, long file_end, int vid_sid, int aud_sid);
    bool AllocDec();
    bool OpenAudio();
    void DecodeFrame(const uint8_t* data, uint32_t size);
    void CloseAll();
    double ElapsedS() const;
};

}  // namespace openfear::sdl3
