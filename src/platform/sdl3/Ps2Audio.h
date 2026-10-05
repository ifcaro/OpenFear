#pragma once
#include "SDL3Audio.h"
#include <functional>
#include <string>
#include <unordered_map>

namespace openfear::sdl3 {

// PS2-specific audio backend. Wraps SDL3Audio, which stays a plain,
// generic SDL3 core binding, and adds, in one place, every
// fix/optimization the PS2 audio path specifically needs:
//   - Requests 48000 (SPU2's native rate). The audio-feeder's fixed-size
//     buffer only divides evenly at that rate - any other rate misaligns
//     into a chronic partial-accept, audible as stutter.
//   - A raw-bytes cache for the setup-phase WAVs, to skip the CD read cost
//     (not the decode cost) on the ones known to be slow.
//   - A yield after every decode, so PS2's audio-feeder thread gets a
//     chance to catch up after a long CPU-bound WAV-parse+resample call.
// Device state, including the format reset when the device closes,
// belongs entirely to SDL3's own PS2AUDIO backend - this class only talks
// to SDL3Audio's base API.
class Ps2Audio final : public SDL3Audio {
public:
    Ps2Audio();
    void preloadSetupPhase(std::function<void(int, int)> on_progress = nullptr) override;
    void releaseSetupPhaseCache() override;

protected:
    std::vector<uint8_t> ReadWavBytes(std::string_view path) override;
    void OnAfterDecode() override;

private:
    std::unordered_map<std::string, std::vector<uint8_t>> raw_cache_;
};

}  // namespace openfear::sdl3
