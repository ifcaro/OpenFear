#pragma once
#include "openfear/platform/IAudio.h"
#include <SDL3/SDL.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace openfear::sdl3 {

// Generic, platform-agnostic audio backend built directly on SDL3 core (no
// SDL3_mixer): SDL_LoadWAV_IO parses WAV headers, SDL_AudioStream converts to
// the device's fixed format, and a small owned SDL_OpenAudioDeviceStream
// pull-callback does the mixing. Backend-specific quirks belong in a
// subclass (see Ps2Audio) - this class stays a plain, portable binding.
class SDL3Audio : public IAudio {
public:
    explicit SDL3Audio(int sample_rate = 44100) : sample_rate_(sample_rate) {}
    ~SDL3Audio() override;

    bool init() override;
    void shutdown() override;
    int sampleRate() const override { return sample_rate_; }

    void setAssetProvider(IAssetProvider* provider) override { asset_provider_ = provider; }

    SoundHandle preloadWave(std::string_view path) override;
    SoundHandle playLoaded(SoundHandle handle, AudioFlags flags) override;
    SoundHandle playWave(std::string_view path, AudioFlags flags) override;
    void stop(SoundHandle handle) override;
    void unload(SoundHandle handle) override;
    bool isPlaying(SoundHandle handle) override;
    bool isReady(SoundHandle handle) override;
    void setVolume(SoundHandle handle, float volume) override;

protected:
    // Source of the raw bytes DecodeWavFile() decodes. Default: read through
    // asset_provider_. A subclass can override this to serve from its own
    // cache instead - the only extension point needed to add a preload cache
    // without touching this class's own logic. Runs on whichever thread ends
    // up processing the decode request, decode_thread_ by default.
    virtual std::vector<uint8_t> ReadWavBytes(std::string_view path);

    // Called right after DecodeWavFile() finishes decoding+resampling a
    // chunk, on whichever thread processes the decode request
    // (decode_thread_ by default). Default: no-op. A subclass whose
    // backend needs a moment after a CPU-heavy conversion for something
    // else (e.g. an audio thread) to catch up can use this.
    virtual void OnAfterDecode() {}

    struct DecodeRequest {
        SoundHandle handle;
        std::string path;
    };

    // Starts whatever's needed to process queued DispatchDecode() calls.
    // Default: decode_thread_. A subclass with no real std::thread support
    // (see EmscriptenAudio) overrides this as a no-op, pairing it with a
    // DispatchDecode() override that processes synchronously instead.
    virtual void StartDecodeThreadIfNeeded();

    // Queues `req` for background decode - auto_play means start playing
    // it the moment it's decoded, instead of just caching it for later.
    // Default: push onto decode_queue_ for decode_thread_ to pick up.
    virtual void DispatchDecode(DecodeRequest req, bool auto_play);

    // The decode + sounds_ update for one request - shared by
    // DecodeThreadMain()'s loop and any subclass's synchronous
    // DispatchDecode() override.
    void ProcessDecodeRequest(DecodeRequest req);

    // Mixes `bytes` bytes (device sample format, S16 stereo) of active
    // sounds_ entries plus the postmix hook directly into dst. Thread-safe
    // (locks mutex_ itself), kept protected so a push-model backend's
    // subclass can drive a mix pass directly instead of going through
    // Mix()/MixCallback's pull model.
    void MixInto(Uint8* dst, int bytes);

    // Clears sounds_ and the postmix hook. Called from shutdown().
    void ClearAllSounds();

    IAssetProvider* asset_provider_ = nullptr;
    int sample_rate_;

private:
    struct SoundEntry {
        std::vector<int16_t> pcm;  // mono, device sample rate - Mix() dupes to stereo output
        size_t play_pos = 0;       // index into pcm, in samples (not frames)
        bool active = false;
        bool loop = false;
        // True only for playWave()-created chunks (fire-and-forget SFX/voice
        // lines - caller never unload()s them). Freed automatically once
        // they finish, instead of leaking.
        bool transient = false;
        float volume = 1.0f;
        // Set (under mutex_) by the mix callback when a non-looping entry
        // runs out, by stop(), or by a failed decode - all mean this entry
        // is done, to be swept on the next reap.
        bool finished_since_reap = false;

        // Still decoding - pcm is empty, nothing can play yet (Mix() only
        // looks at active, which stays false this whole time). The flag
        // itself is for playLoaded(), so it knows not to touch play_pos/pcm
        // yet, and for stop()/unload(), so they can still act on the
        // handle without waiting for decode to finish.
        bool pending = false;
        // playLoaded() called while pending: remember to start once decode
        // finishes, instead of starting now on an empty pcm.
        bool auto_play_pending = false;
        bool pending_loop = false;
    };

    // The actual decode+resample work - no lock held, safe to run on
    // decode_thread_. Returns an empty vector on failure (bad path, empty
    // file, conversion failure).
    std::vector<int16_t> DecodeWavFile(std::string_view path);

    // Runs for the lifetime of the audio device (started by
    // StartDecodeThreadIfNeeded(), joined in shutdown()). Pulls one
    // DecodeRequest at a time and does the heavy
    // SDL_LoadWAV_IO/SDL_AudioStream work off the caller's thread, so a
    // slow decode can't block the real audio-feeder thread and cause an
    // audible stutter.
    void DecodeThreadMain();

    SoundHandle EnqueueDecode(std::string_view path, bool transient, bool auto_play, bool loop);

    // Erases transient entries flagged finished_since_reap, clears the flag
    // on non-transient ones. Called at the top of preloadWave()/
    // playLoaded()/playWave()/stop()/unload(), so cleanup never lags more
    // than one call behind a real finish - isPlaying()/isReady()/
    // setVolume() skip it on purpose.
    void ReapFinishedTransients();

    // Registers/unregisters this instance as SetAudioPostMix()'s target
    // (g_active_instance in SDL3Audio.cpp), independent of stream_, so
    // SDL3VideoPlayer's AVI audio postmix hook always reaches whichever
    // instance is live.
    void RegisterAsPostMixTarget();
    void UnregisterAsPostMixTarget();

    // SDL3's audio device callback is a pull model ("give me
    // additional_amount more bytes") instead of SDL2's fixed-size push
    // buffer - Mix() writes into mix_scratch_ and pushes it via
    // SDL_PutAudioStreamData() itself rather than filling a buffer SDL handed it.
    static void SDLCALL MixCallback(void* userdata, SDL_AudioStream* stream, int additional_amount,
                                    int total_amount);
    void Mix(SDL_AudioStream* stream, int additional_amount);

    SoundHandle next_handle_ = 1;
    std::unordered_map<SoundHandle, SoundEntry> sounds_;
    // Guards sounds_ against the audio callback thread (which
    // reads/mutates play_pos/active/finished_since_reap every callback)
    // and decode_thread_ (which mutates pcm/pending/auto_play_pending
    // when a decode finishes).
    std::mutex mutex_;
    // SDL_OpenAudioDeviceStream() returns a stream, not a device id -
    // destroying it closes the device it opened too (see shutdown()).
    SDL_AudioStream* stream_ = nullptr;
    std::vector<uint8_t> mix_scratch_;  // reused across Mix() calls

    std::thread decode_thread_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<DecodeRequest> decode_queue_;
    bool decode_thread_running_ = false;

    friend void SetAudioPostMix(void (*callback)(void*, Uint8*, int), void* userdata);
    void (*postmix_callback_)(void*, Uint8*, int) = nullptr;
    void* postmix_userdata_ = nullptr;
};

// Installs a hook invoked after SDL3Audio's own mix pass, on whichever
// SDL3Audio instance is currently active (this project only ever has one
// open audio device at a time - mirrors SDL_mixer's single global
// Mix_SetPostMix). Pass callback=nullptr to uninstall.
void SetAudioPostMix(void (*callback)(void* userdata, Uint8* stream, int len), void* userdata);

}  // namespace openfear::sdl3
