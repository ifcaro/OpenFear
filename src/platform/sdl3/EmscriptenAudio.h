#pragma once
#include "SDL3Audio.h"

namespace openfear::sdl3 {

// Browser backend; skips SDL3Audio's background decode_thread_ entirely.
// std::thread there needs pthreads plus COOP/COEP headers this project
// doesn't set up. Decoding a WAV inline is fast enough on any browser —
// the PS2/native stutter decode_thread_ exists for was never a web issue.
class EmscriptenAudio final : public SDL3Audio {
protected:
    void StartDecodeThreadIfNeeded() override {}
    void DispatchDecode(DecodeRequest req, bool /*auto_play*/) override {
        ProcessDecodeRequest(std::move(req));
    }
};

}  // namespace openfear::sdl3
