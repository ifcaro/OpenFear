#pragma once

#include "openfear/game/Subscreen.h"
#include "openfear/assets/RectFile.h"
#include <array>
#include <chrono>

namespace openfear::game {

class BoardGameScreen;

// DoorsGame (enc type 7): click a door within 4s -> 8-entry mixed power
// table (gain or loss, tapered by powerFactor, not a flat 50/50); timeout ->
// 4-entry all-loss table, same taper.
class DoorsGame : public Subscreen {
public:
    DoorsGame(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio,
              BoardGameScreen& board)
        : Subscreen(assets, renderer, audio), board_(board) {}

    // Preloads all 3 door-type click-zone files (DOOR3/6/10.BIN, 24-80
    // bytes combined — negligible cost) once, so Enter()'s RNG-picked
    // variant never risks a load failure mid-encounter. Called from
    // BoardGameScreen::load().
    bool LoadZoneRects();
    void Enter(int player);
    void update(const IInput& input, float dt);
    void render();

    // Destroys door_tex_/plaque_tex_. Called from
    // BoardGameScreen::FreeLazyMinigameAssets() (every TurnEnd).
    void FreeAssets() override;

private:
    BoardGameScreen& board_;

    int player_ = 0;  // player index in this encounter
    int type_ = 3;    // door type: 3, 6, or 10 doors — selects bitmap + zone
                      // count, no effect on the reward outcome
    TextureHandle door_tex_ = 0;
    TextureHandle plaque_tex_ = 0;
    // All 3 variants, preloaded once by LoadZoneRects() — Enter() points
    // zone_rects_/zone_count_ below at whichever one was RNG-picked.
    std::array<assets::RectEntry, 3> rects3_{};
    std::array<assets::RectEntry, 6> rects6_{};
    std::array<assets::RectEntry, 10> rects10_{};
    // Real click zones for the currently active encounter — variant-
    // dependent (door type selects the rect count), points into one of
    // the preloaded arrays above, set in Enter().
    const assets::RectEntry* zone_rects_ = nullptr;
    int zone_count_ = 0;
    // Gamepad discrete-focus over the doors. D-pad/stick navigates between
    // zone_rects_[0..zone_count_), A presses whichever is focused —
    // matches mouse's positional interaction, even though the RNG outcome
    // doesn't depend on which door is chosen.
    int focus_ = 0;
    int phase_ = 0;  // 0=entry/waiting for click-or-timeout, 1=door creak playing (click only),
                     // 2=result audio playing
    SoundHandle entry_wav_ = 0;   // step 1: entry clip, 6 variants (see kDoorsEntryWav)
    SoundHandle creak_wav_ = 0;   // step 2 (click only): W10/DR_OPEN.WAV
    SoundHandle result_wav_ = 0;  // step 3: outcome pool clip
    std::chrono::steady_clock::time_point
        entry_audio_done_time_{};  // when step 1's audio finished — the 4s auto-timeout only starts
                                   // counting from here
};

}  // namespace openfear::game
