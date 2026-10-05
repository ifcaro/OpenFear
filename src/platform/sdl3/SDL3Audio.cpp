#include "SDL3Audio.h"
#include "openfear/assets/IAssetProvider.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace openfear::sdl3 {

namespace {
// SetAudioPostMix() takes one global installer with no instance parameter -
// route it to whichever SDL3Audio instance is live (in practice, one).
SDL3Audio* g_active_instance = nullptr;
}  // namespace

void SetAudioPostMix(void (*callback)(void*, Uint8*, int), void* userdata) {
    if (!g_active_instance) return;
    std::lock_guard<std::mutex> lock(g_active_instance->mutex_);
    g_active_instance->postmix_callback_ = callback;
    g_active_instance->postmix_userdata_ = userdata;
}

SDL3Audio::~SDL3Audio() {
    shutdown();
}

void SDL3Audio::RegisterAsPostMixTarget() {
    g_active_instance = this;
}

void SDL3Audio::UnregisterAsPostMixTarget() {
    if (g_active_instance == this) g_active_instance = nullptr;
}

void SDL3Audio::ClearAllSounds() {
    std::lock_guard<std::mutex> lock(mutex_);
    sounds_.clear();
    postmix_callback_ = nullptr;
    postmix_userdata_ = nullptr;
}

bool SDL3Audio::init() {
    if (!SDL_Init(SDL_INIT_AUDIO)) return false;

    SDL_AudioSpec spec{};
    spec.freq = sample_rate_;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;

    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                        &SDL3Audio::MixCallback, this);
    if (!stream_) {
        std::fprintf(stderr, "[audio] SDL_OpenAudioDeviceStream failed: %s\n", SDL_GetError());
        return false;
    }
    RegisterAsPostMixTarget();
    // SDL3 audio devices/streams open paused (see SDL_OpenAudioDeviceStream's
    // own doc) - unlike SDL2's SDL_PauseAudioDevice(device, 0).
    SDL_ResumeAudioStreamDevice(stream_);

    StartDecodeThreadIfNeeded();
    return true;
}

void SDL3Audio::StartDecodeThreadIfNeeded() {
    decode_thread_running_ = true;
    decode_thread_ = std::thread(&SDL3Audio::DecodeThreadMain, this);
}

void SDL3Audio::DispatchDecode(DecodeRequest req, bool auto_play) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        // auto_play entries start playing the moment they finish decoding -
        // jumps the queue ahead of plain preloadWave() background requests,
        // which by definition aren't needed yet.
        if (auto_play) {
            decode_queue_.push_front(std::move(req));
        } else {
            decode_queue_.push_back(std::move(req));
        }
    }
    queue_cv_.notify_one();
}

void SDL3Audio::shutdown() {
    if (decode_thread_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            decode_thread_running_ = false;
        }
        queue_cv_.notify_all();
        decode_thread_.join();
    }

    if (stream_) {
        // Also closes the device this stream was opened against - see
        // SDL_OpenAudioDeviceStream's doc. Blocks until the callback thread
        // is stopped, same as SDL2's SDL_CloseAudioDevice() did.
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
    UnregisterAsPostMixTarget();
    ClearAllSounds();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SDLCALL SDL3Audio::MixCallback(void* userdata, SDL_AudioStream* stream, int additional_amount,
                                    int /*total_amount*/) {
    static_cast<SDL3Audio*>(userdata)->Mix(stream, additional_amount);
}

void SDL3Audio::Mix(SDL_AudioStream* stream, int additional_amount) {
    if (additional_amount <= 0) return;
    if (mix_scratch_.size() < static_cast<size_t>(additional_amount)) {
        mix_scratch_.resize(static_cast<size_t>(additional_amount));
    }
    MixInto(mix_scratch_.data(), additional_amount);
    SDL_PutAudioStreamData(stream, mix_scratch_.data(), additional_amount);
}

void SDL3Audio::MixInto(Uint8* dst_bytes, int additional_amount) {
    std::memset(dst_bytes, 0, static_cast<size_t>(additional_amount));
    auto* dst = reinterpret_cast<int16_t*>(dst_bytes);
    // dst is interleaved stereo (L,R,L,R,...) - every source WAV in this game
    // is mono, so e.pcm holds one sample per output frame, duplicated into
    // both channels here instead of at decode time - halves decode work and
    // pcm memory for every sound.
    const int n_frames = additional_amount / static_cast<int>(sizeof(int16_t)) / 2;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [handle, e] : sounds_) {
            if (!e.active) continue;
            for (int frame = 0; frame < n_frames; ++frame) {
                if (e.play_pos >= e.pcm.size()) {
                    if (e.pcm.empty()) {
                        e.active = false;
                        e.finished_since_reap = true;
                        break;
                    }
                    if (e.loop) {
                        e.play_pos = 0;
                    } else {
                        e.active = false;
                        e.finished_since_reap = true;
                        break;
                    }
                }
                const int scaled =
                    static_cast<int>(static_cast<float>(e.pcm[e.play_pos]) * e.volume);
                const int l = static_cast<int>(dst[frame * 2]) + scaled;
                const int r = static_cast<int>(dst[frame * 2 + 1]) + scaled;
                dst[frame * 2] = static_cast<int16_t>(std::clamp(l, -32768, 32767));
                dst[frame * 2 + 1] = static_cast<int16_t>(std::clamp(r, -32768, 32767));
                ++e.play_pos;
            }
        }

        if (postmix_callback_) {
            postmix_callback_(postmix_userdata_, dst_bytes, additional_amount);
        }
    }
}

std::vector<uint8_t> SDL3Audio::ReadWavBytes(std::string_view path) {
    if (!asset_provider_) return {};
    return asset_provider_->ReadFile(std::string(path));
}

std::vector<int16_t> SDL3Audio::DecodeWavFile(std::string_view path) {
    std::vector<uint8_t> raw = ReadWavBytes(path);
    if (raw.empty()) {
        std::fprintf(stderr, "[audio] ReadWavBytes failed: %s\n", std::string(path).c_str());
        return {};
    }

    SDL_IOStream* io = SDL_IOFromConstMem(raw.data(), raw.size());
    if (!io) return {};

    SDL_AudioSpec src_spec{};
    Uint8* src_buf = nullptr;
    Uint32 src_len = 0;
    // closeio=true - SDL closes `io` for us once this returns (success or
    // failure), no manual SDL_CloseIO(io) needed here.
    if (!SDL_LoadWAV_IO(io, true, &src_spec, &src_buf, &src_len)) {
        std::fprintf(stderr, "[audio] SDL_LoadWAV_IO failed: %s: %s\n", std::string(path).c_str(),
                     SDL_GetError());
        return {};
    }

    // Step 1: normalize whatever sample format the file itself declares
    // (8-bit, 16-bit, float, ...) to mono int16 AT THE SOURCE RATE -
    // SDL_AudioStream does no resampling here (dst freq == src freq), just
    // format/channel conversion, which is cheap regardless of platform.
    std::vector<int16_t> src_mono;
    SDL_AudioSpec mono_spec{};
    mono_spec.format = SDL_AUDIO_S16;
    // Mono, not stereo - Mix() duplicates each sample into both output
    // channels at mix time, not stored twice here.
    mono_spec.channels = 1;
    mono_spec.freq = src_spec.freq;
    SDL_AudioStream* cvt = SDL_CreateAudioStream(&src_spec, &mono_spec);
    if (cvt) {
        if (SDL_PutAudioStreamData(cvt, src_buf, static_cast<int>(src_len)) &&
            SDL_FlushAudioStream(cvt)) {
            const int avail = SDL_GetAudioStreamAvailable(cvt);
            if (avail > 0) {
                src_mono.resize(static_cast<size_t>(avail) / sizeof(int16_t));
                const int got = SDL_GetAudioStreamData(cvt, src_mono.data(), avail);
                if (got > 0) {
                    src_mono.resize(static_cast<size_t>(got) / sizeof(int16_t));
                } else {
                    src_mono.clear();
                }
            }
        }
        SDL_DestroyAudioStream(cvt);
    }
    SDL_free(src_buf);

    if (src_mono.empty()) {
        std::fprintf(stderr, "[audio] audio conversion failed: %s\n", std::string(path).c_str());
        return {};
    }

    // Step 2: resample to the device rate ourselves with plain linear
    // interpolation instead of SDL_AudioStream's windowed-sinc resampler -
    // the source material (1998-era mono voice/SFX, upsampled 2-4x) makes
    // linear interpolation's softening inaudible, and the CPU this saves
    // matters on the lower-end backends this project targets.
    std::vector<int16_t> pcm;
    if (mono_spec.freq == sample_rate_) {
        pcm = std::move(src_mono);
    } else {
        const float ratio = static_cast<float>(mono_spec.freq) / static_cast<float>(sample_rate_);
        const int32_t out_count = static_cast<int32_t>(static_cast<float>(src_mono.size()) / ratio);
        pcm.resize(static_cast<size_t>(out_count));
        const int32_t last = static_cast<int32_t>(src_mono.size()) - 1;
        float src_pos = 0.0f;
        for (int32_t i = 0; i < out_count; ++i, src_pos += ratio) {
            // int32_t, not uint32_t - src_pos is always >= 0 here, so the
            // result is identical, and a plain signed float->int cast avoids
            // falling back to a slower conversion path for the unsigned case.
            int32_t idx = static_cast<int32_t>(src_pos);
            if (idx > last) idx = last;
            const float frac = src_pos - static_cast<float>(idx);
            const float a = static_cast<float>(src_mono[static_cast<size_t>(idx)]);
            const float b = static_cast<float>((idx < last) ? src_mono[static_cast<size_t>(idx) + 1]
                                                            : src_mono[static_cast<size_t>(idx)]);
            pcm[static_cast<size_t>(i)] = static_cast<int16_t>(a + (b - a) * frac);
        }
    }
    OnAfterDecode();

    return pcm;
}

SoundHandle SDL3Audio::EnqueueDecode(std::string_view path, bool transient, bool auto_play,
                                     bool loop) {
    SoundHandle h = next_handle_++;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        SoundEntry& e = sounds_[h];
        e.transient = transient;
        e.pending = true;
        e.auto_play_pending = auto_play;
        e.pending_loop = loop;
    }
    DispatchDecode(DecodeRequest{h, std::string(path)}, auto_play);
    return h;
}

void SDL3Audio::ProcessDecodeRequest(DecodeRequest req) {
    std::vector<int16_t> pcm = DecodeWavFile(req.path);

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sounds_.find(req.handle);
    if (it == sounds_.end()) return;  // unload()/reaped before decode finished

    if (pcm.empty()) {
        // Leave insert/erase of sounds_ to the main thread only
        // (isPlaying() relies on that invariant to skip locking) - just
        // mark it finished so the next ReapFinishedTransients() call
        // (always main-thread) sweeps it if transient.
        it->second.pending = false;
        it->second.auto_play_pending = false;
        it->second.finished_since_reap = true;
        return;
    }

    it->second.pcm = std::move(pcm);
    it->second.pending = false;
    if (it->second.auto_play_pending) {
        it->second.play_pos = 0;
        it->second.loop = it->second.pending_loop;
        it->second.active = true;
        it->second.auto_play_pending = false;
    }
}

void SDL3Audio::DecodeThreadMain() {
    for (;;) {
        DecodeRequest req;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock,
                           [this] { return !decode_queue_.empty() || !decode_thread_running_; });
            if (!decode_thread_running_ && decode_queue_.empty()) return;
            req = std::move(decode_queue_.front());
            decode_queue_.pop_front();
        }
        ProcessDecodeRequest(std::move(req));
    }
}

void SDL3Audio::ReapFinishedTransients() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = sounds_.begin(); it != sounds_.end();) {
        if (it->second.finished_since_reap) {
            it->second.finished_since_reap = false;
            if (it->second.transient) {
                it = sounds_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

SoundHandle SDL3Audio::preloadWave(std::string_view path) {
    ReapFinishedTransients();
    return EnqueueDecode(path, /*transient=*/false, /*auto_play=*/false, /*loop=*/false);
}

SoundHandle SDL3Audio::playLoaded(SoundHandle handle, AudioFlags flags) {
    ReapFinishedTransients();
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sounds_.find(handle);
    if (it == sounds_.end()) return 0;

    if (it->second.pending) {
        // Decode thread hasn't filled pcm yet - remember to start once it
        // does (ProcessDecodeRequest checks auto_play_pending itself).
        it->second.auto_play_pending = true;
        it->second.pending_loop = hasFlag(flags, AudioFlags::Loop);

        // This handle was queued as a plain background preload - now that
        // it's actually wanted, move it to the front of decode_queue_ so
        // it doesn't wait behind an earlier batch of preloadWave() calls
        // nobody needs yet (lock ordering: mutex_ then queue_mutex_,
        // matches every other caller - DecodeThreadMain never holds both).
        {
            std::lock_guard<std::mutex> qlock(queue_mutex_);
            auto qit =
                std::find_if(decode_queue_.begin(), decode_queue_.end(),
                             [handle](const DecodeRequest& r) { return r.handle == handle; });
            if (qit != decode_queue_.end() && qit != decode_queue_.begin()) {
                DecodeRequest req = std::move(*qit);
                decode_queue_.erase(qit);
                decode_queue_.push_front(std::move(req));
            }
        }
        return handle;
    }

    it->second.play_pos = 0;
    it->second.loop = hasFlag(flags, AudioFlags::Loop);
    it->second.active = true;
    return handle;
}

SoundHandle SDL3Audio::playWave(std::string_view path, AudioFlags flags) {
    ReapFinishedTransients();
    return EnqueueDecode(path, /*transient=*/true, /*auto_play=*/true,
                         hasFlag(flags, AudioFlags::Loop));
}

void SDL3Audio::stop(SoundHandle handle) {
    ReapFinishedTransients();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (handle == 0) {
            for (auto& [h, e] : sounds_) {
                e.auto_play_pending = false;  // cancel any not-yet-decoded auto-start
                if (!e.active) continue;
                e.active = false;
                e.finished_since_reap = true;
            }
        } else {
            auto it = sounds_.find(handle);
            if (it != sounds_.end()) {
                it->second.auto_play_pending = false;
                if (it->second.active) {
                    it->second.active = false;
                    it->second.finished_since_reap = true;
                }
            }
        }
    }
    // Sweep right away, same as the natural-finish path - a stopped
    // transient sound is done, not just paused.
    ReapFinishedTransients();
}

void SDL3Audio::unload(SoundHandle handle) {
    ReapFinishedTransients();
    if (handle == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    sounds_.erase(handle);
}

bool SDL3Audio::isPlaying(SoundHandle handle) {
    // No ReapFinishedTransients() or lock here, on purpose - called every
    // frame from tight polling loops. `active` stays accurate via Mix()
    // regardless of reap timing, and sounds_'s structure (insert/erase)
    // only ever changes on the main thread, so find() is safe unlocked.
    // Reading `active` unlocked is a data race against Mix()'s writes,
    // accepted to avoid this thread contending mutex_ against the audio
    // thread every iteration - that contention risks starving it.
    auto it = sounds_.find(handle);
    if (it == sounds_.end()) return false;
    // pending counts as still playing too - the decode thread hasn't had a
    // chance to flip active to true yet, but a caller polling "wait for
    // this to finish" must not read that gap as "already done".
    return it->second.active || it->second.pending;
}

bool SDL3Audio::isReady(SoundHandle handle) {
    // No lock: sounds_'s structure only changes on the main thread, so
    // find() is safe unlocked; reading `pending` unlocked is a data race
    // against decode_thread_'s write to it, accepted to avoid locking on
    // a path polled every frame.
    auto it = sounds_.find(handle);
    if (it == sounds_.end()) return false;
    return !it->second.pending;
}

void SDL3Audio::setVolume(SoundHandle handle, float volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sounds_.find(handle);
    if (it == sounds_.end()) return;
    it->second.volume = volume;
}

}  // namespace openfear::sdl3
