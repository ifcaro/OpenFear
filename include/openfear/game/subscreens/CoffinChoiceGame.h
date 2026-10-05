#pragma once

#include "openfear/game/Subscreen.h"
#include <chrono>
#include <vector>

namespace openfear::game {

class BoardGameScreen;

// CoffinChoiceGame (enc type 11): binary coffin choice — known (+10 power,
// guaranteed) vs closed coffin (weighted table roll, 8 possible outcomes).
class CoffinChoiceGame : public Subscreen {
public:
    CoffinChoiceGame(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio,
                     BoardGameScreen& board)
        : Subscreen(assets, renderer, audio), board_(board) {}

    // Sets poly_base_ to the coffin-silhouette hexagon used for this
    // minigame's gamepad focus overlay. Port-only UX data (zero ASM basis).
    // Called once from BoardGameScreen::load().
    void LoadPolygon();
    // Loads DATA/POWCOFF.BIN's first 2 rects (known/coffin click zones).
    // Called once from BoardGameScreen::load().
    bool LoadZoneRects();
    // Destroys the currently-loaded coffin bitmap. Called from
    // BoardGameScreen::FreeLazyMinigameAssets() (every TurnEnd).
    void FreeAssets() override;

    void Enter(int player);
    void update(const IInput& input, float dt);
    void render();

private:
    void DrawCoffinFocus(const std::vector<Point>& verts);

    BoardGameScreen& board_;

    // Outcome ids rolled by the weighted stake table:
    //   kCoffinPower30: AddPower(30)        kCoffinKeyLoss: LoseKey(rotation)
    //   kCoffinPower50: AddPower(50)        kCoffinPowerLoss10: SubtractPower(10)
    //   kCoffinKeyGain: GainKey(rotation)   kCoffinGateOpen: TeleportToNearestBlackHole
    //   kCoffinPower1: AddPower(1)          kCoffinSkipTurn: skip_turn_counter=1
    static constexpr int kCoffinPower30 = 1;
    static constexpr int kCoffinPower50 = 2;
    static constexpr int kCoffinKeyGain = 3;
    static constexpr int kCoffinPower1 = 4;
    static constexpr int kCoffinSkipTurn = 5;
    static constexpr int kCoffinKeyLoss = 6;
    static constexpr int kCoffinPowerLoss10 = 7;
    static constexpr int kCoffinGateOpen = 8;

    // Gate tags for the skip-turn/GateOpen slot category — a different
    // numbering space than the ids above, dropped when total_players==1.
    static constexpr int kSlotSpecialSkipTurn = 100;
    static constexpr int kSlotSpecialGateOpen = 101;

    int player_ = 0;
    int picked_id_ = 0;          // one of kCoffin* above, rolled this resolution
    bool clicked_ = false;       // player has clicked (debounce)
    bool reveal_phase_ = false;  // brief display of coffin contents after choice
    bool chose_known_ = false;   // true=player picked known, false=picked coffin/timeout
    bool focus_known_ = true;    // gamepad discrete-focus: true=known(left), false=coffin(right)
    std::vector<Point> poly_base_;
    std::vector<Point> poly_translated_;  // poly_base_ shifted +218px in x, the 2nd coffin
    assets::RectEntry known_zone_{};      // DATA/POWCOFF.BIN rect 0
    assets::RectEntry coffin_zone_{};     // DATA/POWCOFF.BIN rect 1
    // G4/COF_01..19.BMP. kCoffinKeyGain/kCoffinKeyLoss use PROVINCE-indexed art
    // (6 files each); the rest use one fixed image each. Loaded on demand
    // rather than all 19 preloaded up front.
    TextureHandle tex_ = 0;
    SoundHandle entry_wav_ = 0;  // entry pool
    // When the entry audio finishes playing on its own (no click yet), a
    // genuine 3000ms grace timer starts before treating it as a timeout.
    // time_point::min() = entry audio still playing / grace period not
    // yet started.
    std::chrono::steady_clock::time_point entry_audio_done_time_{};
    SoundHandle result_wav_ = 0;  // outcome insult
    // Timeout announcement — plays with the coffin still closed, resolution
    // withheld until it finishes.
    SoundHandle timeout_wav_ = 0;
    bool awaiting_timeout_wav_ = false;
};

}  // namespace openfear::game
