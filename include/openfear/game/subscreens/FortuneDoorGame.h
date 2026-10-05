#pragma once

#include "openfear/game/Subscreen.h"
#include "openfear/assets/RectFile.h"
#include <array>
#include <chrono>

namespace openfear::game {

class BoardGameScreen;

// FortuneDoorGame (enc type 10): door bitmap + OK/X choice. X -> taunt
// audio -> TurnEnd. OK -> door creak, then 50/50 between
// ResolveFortuneCard and ResolveFateCard (those 2 resolvers stay on
// BoardGameScreen — shared with plain stone-type-3/4 landings).
class FortuneDoorGame : public Subscreen {
public:
    FortuneDoorGame(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio,
                    BoardGameScreen& board)
        : Subscreen(assets, renderer, audio), board_(board) {}

    // Loads the 2 real OK/X click zones from GATEOPEN.BIN. Called from
    // BoardGameScreen::load().
    bool LoadZoneRects();
    void Enter(int player);
    void update(const IInput& input, float dt);
    void render();

    // Frees the lazy-loaded door textures + entry/taunt audio.
    // Called from BoardGameScreen::FreeLazyMinigameAssets().
    void FreeAssets() override;

private:
    BoardGameScreen& board_;

    int player_ = 0;
    // GATEOPEN.BIN order: [0] = X zone (450,256)-(550,332), [1] = OK zone (100,256)-(200,332).
    std::array<assets::RectEntry, 2> zone_rects_{};
    bool clicked_ = false;         // OK or X resolved — one-shot latch, never reset
    bool ok_click_ = false;        // true=OK, false=X
    bool awaiting_creak_ = false;  // OK path only: door shown open, door-creak WAV playing; card
                                   // dispatch waits for this to finish
    // The 4s auto-resolve window starts AFTER both entry audios finish, never before.
    std::chrono::steady_clock::time_point entry_audio_done_time_{};
    SoundHandle taunt_wav_ = 0;   // X path only: which of the 8 taunt clips is playing —
                                  // teardown is audio-done-gated, not a flat timer
    TextureHandle tex_ = 0;       // G1/GATEOPN2.BMP — "open" frame, shown on OK resolve only
    TextureHandle bg_tex_ = 0;    // G1/GATEOPEN.BMP — "closed" frame, shown while waiting
    SpriteInfo bg_sprite_{};      // bg_tex_ wrapped for DrawFocusTint
    bool focus_ok_ = true;        // gamepad OK/X focus, true = OK zone focused (default)
    SoundHandle result_wav_ = 0;  // W10/DR_OPEN.WAV — plays once, at trigger time
    // Only the RNG-picked variant is ever loaded (playWav(), not preloaded) - see Enter()/
    // update(). taunt_wav_ above already tracks the picked taunt clip; this one
    // tracks the picked result variant the same way.
    SoundHandle result_var_wav_ = 0;
};

}  // namespace openfear::game
