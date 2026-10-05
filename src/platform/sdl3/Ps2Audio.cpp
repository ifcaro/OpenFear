#include "Ps2Audio.h"
#include "openfear/assets/IAssetProvider.h"
#include <SDL3/SDL.h>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace openfear::sdl3 {

namespace {
// Setup-screen WAVs cached resident for the whole setup phase, to hide
// disk-read latency: the one real click-reaction SFX (AT_GLASS.WAV) plus
// each screen's entry voice line, since those play instantly, before a
// disk read would otherwise be done. ~3.71MB total. Every other
// setup-screen WAV is reactive (narrator responding to a choice already
// made, with natural cover time from the preceding animation) and loads
// on demand instead - a bigger resident cache eats into the heap
// headroom that audio's decode/free churn (the confirmed real driver of
// EE-heap fragmentation) needs to still find a contiguous block for
// texture loads.
constexpr const char* kSetupWavPaths[] = {
    "W9/AT_GLASS.WAV",  // PickChar/FearSelect glass-break SFX
    "W1/AT_0007.WAV",   // PickPlayersScreen entry voice
    "W1/AT_0014.WAV",   // PickCharScreen entry voice, 1st player
    "W3/AT_0394.WAV",   // PickCharScreen entry voice, 2nd+ player
    "W4/AT_0490.WAV",   // FearSelectScreen entry voice, variant 1/5
    "W4/AT_0491.WAV",   // FearSelectScreen entry voice, variant 2/5
    "W4/AT_0492.WAV",   // FearSelectScreen entry voice, variant 3/5
    "W4/AT_0493.WAV",   // FearSelectScreen entry voice, variant 4/5
    "W4/AT_0494.WAV",   // FearSelectScreen entry voice, variant 5/5
    "W8/AT_0701.WAV",   // ChooseLevelScreen entry voice
    "W8/AT_0692.WAV",   // MasterControlScreen entry voice, variant A
    "W8/AT_0693.WAV",   // MasterControlScreen entry voice, variant B
    "W9/AT_0759.WAV",   // MasterControlScreen GameDuration feedback: unchanged from default
};

// Returns the matching kSetupWavPaths entry if `path` ends with one,
// uppercase/slash-agnostic - sidesteps replicating Ps2AssetProvider's own
// root-prefix/remap logic here.
const char* MatchSetupWavSuffix(std::string_view path) {
    std::string norm(path);
    for (char& c : norm) {
        if (c == '\\') c = '/';
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    for (const char* suffix : kSetupWavPaths) {
        const size_t slen = std::strlen(suffix);
        if (norm.size() >= slen && norm.compare(norm.size() - slen, slen, suffix) == 0) {
            return suffix;
        }
    }
    return nullptr;
}
}  // namespace

Ps2Audio::Ps2Audio()
    // 48000 (SPU2 native), not 44100/22050 - feed_size=(512*freq)/48000
    // only lands exactly on our 2048-byte buffer_size at 48000; anything
    // else misaligns into a chronic partial-accept, audible as stutter.
    : SDL3Audio(48000) {}

void Ps2Audio::preloadSetupPhase(std::function<void(int, int)> on_progress) {
    if (!asset_provider_) return;
    const int total = static_cast<int>(sizeof(kSetupWavPaths) / sizeof(kSetupWavPaths[0]));
    int cached = 0;
    size_t total_bytes = 0;
    int done = 0;
    for (const char* path : kSetupWavPaths) {
        std::vector<uint8_t> raw = asset_provider_->ReadFile(path);
        if (!raw.empty()) {
            total_bytes += raw.size();
            raw_cache_[path] = std::move(raw);
            ++cached;
        }
        ++done;
        if (on_progress) on_progress(done, total);
    }
    std::fprintf(stderr, "[audio] preloadSetupPhase done (%d/%d cached, %zu bytes)\n", cached,
                 total, total_bytes);
}

void Ps2Audio::releaseSetupPhaseCache() {
    size_t freed_bytes = 0;
    for (auto& [path, bytes] : raw_cache_) freed_bytes += bytes.size();
    raw_cache_.clear();
    std::fprintf(stderr, "[audio] releaseSetupPhaseCache: freed %zu bytes\n", freed_bytes);
}

std::vector<uint8_t> Ps2Audio::ReadWavBytes(std::string_view path) {
    if (const char* key = MatchSetupWavSuffix(path)) {
        auto it = raw_cache_.find(key);
        if (it != raw_cache_.end()) return it->second;
    }
    return SDL3Audio::ReadWavBytes(path);
}

void Ps2Audio::OnAfterDecode() {
    // A long decode (~1.5s for the biggest WAVs) can starve PS2's
    // audio-feeder thread, stuttering the NEXT playback for a couple
    // seconds.
    SDL_Delay(10);  // 10ms
}

}  // namespace openfear::sdl3
