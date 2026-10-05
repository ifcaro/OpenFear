#pragma once

#include "openfear/game/Subscreen.h"
#include "openfear/assets/RectFile.h"
#include <array>

namespace openfear::game {

class BoardGameScreen;

// Duel winner's post-gauge choice: steal a stealable key (zones 0-5) or
// curse the loser (zone 6, always valid).
class DuelStealOrCurse : public Subscreen {
public:
    DuelStealOrCurse(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio,
                     BoardGameScreen& board)
        : Subscreen(assets, renderer, audio), board_(board) {}

    // Preloads the 9-zone GETKEYS.BIN click grid. Called from BoardGameScreen::load().
    bool LoadZoneRects();

    // current_state_ == DuelStealOrCurse.
    void update(const IInput& input, float dt);
    void render();

private:
    // update()'s sub-phases, split out for readability — same order/early-return
    // semantics as when this was inline.
    void EnterStealOrCurse();
    // Resolves the winner's choice (steal a key or curse) — was a local lambda,
    // moved to a method since it only ever touched class state, no update() locals.
    void ResolveZone(int zone);
    // Returns true if this phase handled the frame (caller must return).
    bool UpdatePendingTurnEnd();
    // AI auto-decide or human mouse/gamepad interaction — calls ResolveZone().
    void UpdateWinnerChoice(const IInput& input, float dt);

    // Test-only access seam — lets headless tests call ResolveZone() directly instead of
    // simulating a real click. No effect in the shipped app (struct only exists in tests).
    friend struct BoardGameScreenTestAccess;

    BoardGameScreen& board_;

    // DATA/GETKEYS.BIN — 6 key zones + curse zone (0-6 real clickable) +
    // 2 portrait display-only frames (7-8, never registered).
    std::array<assets::RectEntry, 9> getkeys_zone_rects_{};

    int focus_ =
        6;  // gamepad discrete-focus over the 7 getkeys_zone_rects_ zones (0-5 key, 6 curse)
    bool timer_armed_ = false;  // true once the announce audio finishes — gates the AI's 1000ms
                                // decide delay (human has no timeout at all) and starts the
                                // clock the post-choice 2000ms hold-pause measures from
    bool pending_turnend_ = false;
    int hover_mouse_x_ = -1,
        hover_mouse_y_ = -1;  // screen-pixel mouse pos cached once the interactive phase is
                              // reached in update() — render() has no IInput access, needed
                              // for the hover highlight
    // Announce (W6/AT_0630.WAV or kNoStealWav 1-of-2) — the real
    // interactive timer only arms once this finishes.
    SoundHandle announce_wav_ = 0;
    // Curse-result taunt (curse_type 1/2/4/6) — must finish before the
    // state transitions to TurnEnd.
    SoundHandle curse_taunt_wav_ = 0;
};

}  // namespace openfear::game
