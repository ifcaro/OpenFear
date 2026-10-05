#pragma once

#include "openfear/game/Subscreen.h"
#include "openfear/game/subscreens/DuelAttackGauge.h"
#include "openfear/assets/RectFile.h"
#include <array>

namespace openfear::game {

class BoardGameScreen;

// Duel target picker: draws all players' portraits. Human is a real
// interactive click loop over choose_zone_rects_; AI resolves
// instantly/fixed (choose_ai_target drives the auto-resolve branch
// instead of waiting for a click). Also owns the
// Protection bypass (a previously-drawn Fortune-card can force an
// auto-resolved reversed-roles duel here instead of the normal
// gauge-picker flow). board_.duel_.choose_num_candidates/choose_ai_target
// are built directly by BoardGameScreen's own portrait-corner-click/
// AI-eligibility trigger sites before this screen ever mounts.
class DuelChooseOpponent : public Subscreen {
public:
    DuelChooseOpponent(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio,
                       BoardGameScreen& board, DuelAttackGauge& duel_gauge)
        : Subscreen(assets, renderer, audio), board_(board), duel_attack_gauge_(duel_gauge) {}

    // Preloads the 6-zone CHOOSE.BIN click grid. Called from BoardGameScreen::load().
    bool LoadZoneRects();

    // current_state_ == DuelChooseOpponent. Returns false always (never
    // requests exiting the board screen) — matches BoardGameScreen::update()'s
    // own return convention for this case.
    bool update(const IInput& input, float dt);
    void render();

    // Destroys protection_card_tex_, if loaded. Called from
    // BoardGameScreen::FreeLazyMinigameAssets() (every TurnEnd), alongside
    // the other Duel textures — not registered in the generic lazy_minigames
    // list, since Duel's trigger isn't an enc_type roll.
    void FreeProtectionCardTexture();

private:
    // update()'s sub-phases, split out for readability — same order/early-return
    // semantics as when this was inline.
    void EnterChooseOpponent();
    // Returns true if this phase handled the frame (caller must return false).
    bool UpdateProtectionCardWait();
    bool UpdateAiAutoResolve(float dt);
    bool UpdatePostPickWait();
    // Last phase — no further phases follow, so this just does the work directly.
    void UpdateClickResolution(const IInput& input, float dt);

    BoardGameScreen& board_;
    DuelAttackGauge& duel_attack_gauge_;

    // DATA/CHOOSE.BIN — 3x2 grid of candidate-portrait slots, loaded once.
    std::array<assets::RectEntry, 6> choose_zone_rects_{};

    // Post-pick wait sub-phase — the valid-target branch draws broken
    // glass over the CHOSEN cell + starts glass audio, but doesn't
    // proceed to the Protection check/the duel until that audio finishes.
    // -1 = no pick pending; 0-5 = char_id of the cell currently shattered.
    int picked_char_ = -1;
    int picked_target_slot_ = -1;  // resolved player slot for the pending pick above
    int focus_ = 0;                // gamepad discrete-focus over the 6 choose_zone_rects_ cells
    // Protection pre-resolve overlay — shows a
    // fortune-card overlay (same table as the grant-side reveal) and
    // plays 1-of-4 taunt audio BEFORE the gauge gets shown broken/empty
    // and the steal screen opens.
    bool protection_card_pending_ = false;
    int protection_attacker_ = -1;
    int protection_target_ = -1;
    TextureHandle protection_card_tex_ = 0;
    bool protection_card_active_ = false;
    SoundHandle protection_card_wav_ = 0;
};

}  // namespace openfear::game
