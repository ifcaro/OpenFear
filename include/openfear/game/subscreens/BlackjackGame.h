#pragma once

#include "openfear/game/Subscreen.h"
#include "openfear/assets/RectFile.h"
#include <array>
#include <chrono>

namespace openfear::game {

class BoardGameScreen;

// Real dealer-first Blackjack (enc type 13). Stake picked via a weighted
// table before any cards are shown; the dealer plays its ENTIRE hand
// automatically first, then hands off to the player only if it stands
// under 21.
class BlackjackGame : public Subscreen {
    // Test-only access seam — lets headless tests read stake_id_ directly. No
    // effect in the shipped app (struct only exists in tests).
    friend struct BoardGameScreenTestAccess;

public:
    // Sub-phases of the BlackjackGame state. The stake itself is picked
    // via a weighted table (the player never chooses WHICH stake) and
    // shown as a full-screen announce screen with the stake's own icon +
    // audio (StakeAnnounce) before any card is dealt. The player does get
    // a real confirm/decline click on that screen — declining, or
    // letting a 4000ms grace window expire, cancels the whole encounter
    // with no cards dealt at all; only confirming moves on to the card
    // table.
    enum class BjPhase : uint8_t {
        StakeAnnounce = 0,  // announce the pre-picked stake via audio, no input
        DealCards = 1,      // deal 2 cards each to player+dealer; check natural-21 edge cases
        // DealCards transitions straight to DealerTurn, NOT PlayerTurn —
        // the dealer plays its full hand automatically first. DealerTurn
        // only hands off to PlayerTurn if the dealer stands under 21; if
        // the dealer busts or hits 21, the hand resolves immediately.
        PlayerTurn = 2,  // player hits (clicks one of 3 card zones) or stands (clicks the
                         // separate zone above). 5-card charlie cap. Only reached if the
                         // dealer stood under 21.
        DealerTurn = 3,  // dealer AI plays FIRST (before PlayerTurn): hits while player!=21 AND
                         // (dealer<16 OR dealer<player)
        Comparing = 4    // outcome already resolved by the transitioning phase; apply consequence
    };

    // Pure formula: the real game's hand value is NOT standard casino
    // Blackjack scoring. cards[i] rank encoding: 1=Ace, 2-10=face value,
    // 11+=face card (10), 0 (empty slot) contributes nothing. Every ace
    // in the hand counts as EITHER 1 (all of them) OR 11 (all of them) —
    // never a mix (irrelevant in practice since a hand can only ever
    // hold 1 ace — see RollUniqueRank).
    static int ComputeHandValue(const int* cards, int count);

    BlackjackGame(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio,
                  BoardGameScreen& board)
        : Subscreen(assets, renderer, audio), board_(board) {}

    // Loads DATA/BJACK1.BIN (2 stake-announce zones) and DATA/BJACK2.BIN
    // (4 hit/stand zones). Called once from BoardGameScreen::load().
    bool LoadZoneRects();

    // Frees table/announce/stake-icon/suit textures. Called from
    // BoardGameScreen::FreeLazyMinigameAssets().
    void FreeAssets() override;

    void Enter(int player);

    // current_state_ == BlackjackGame.
    void update(const IInput& input, float dt);
    void render();

private:
    // Deal: reroll on a duplicate rank within the SAME hand (dedup is
    // per-hand, not shared between player and dealer).
    int RollUniqueRank(const int* hand, int filled_count);

    // Player-side scoring: slot 0 always counts; slots 1-4 only count if
    // player_confirmed_[slot] is set.
    int PlayerScore() const;

    BoardGameScreen& board_;

    // Hit/Stand click zones — DATA/BJACK2.BIN (4 rects): 3 adjacent
    // card-column zones (any = Hit) + 1 separate button above (Stand).
    std::array<assets::RectEntry, 4> zone_rects_{};
    // Stake-announce confirm/decline zones — DATA/BJACK1.BIN (2 rects).
    // zone 0 (left) = confirm (skip the wait, deal now); zone 1 (right) =
    // decline (no stake effect).
    std::array<assets::RectEntry, 2> stake_zone_rects_{};
    bool stake_clicked_ = false;  // true once StakeAnnounce has consumed a click this phase
    bool stake_focus_confirm_ =
        true;  // gamepad discrete-focus: true=confirm(left), false=decline(right)
    SoundHandle stake_audio_ = 0;  // handle for the currently-playing BJI1-4 announce line
    // Timestamp when the announce audio finished (min() = not yet
    // finished this phase). Starts the real 4000ms grace window.
    std::chrono::steady_clock::time_point stake_audio_done_time_{};
    // Dealer voice line gating the phase transition that set it: dealer
    // goes first (BJI5-7, DealCards's "neither natural" branch, arms
    // DealerTurn's hit timer), dealer bust (BJI8) or dealer stands (BJI9,
    // DealerTurn's resolve), always consumed once its own done event fires.
    SoundHandle dealer_audio_ = 0;
    // Result-announce line (key-province BJI11-22 or the generic
    // win/loss pool). Teardown is gated on this audio's own done event,
    // not a fixed timer — some clips run 6+ seconds.
    SoundHandle result_audio_ = 0;
    // Card-reveal gate: DrawCard for the player's own 2 initial cards
    // doesn't fire at deal time — it fires once BJI9 (the dealer's
    // stand/your-turn line) finishes playing, ~3s after the dealer
    // decides to stand.
    // For instant-resolve paths (natural 21, dealer busts/hits 21) the
    // reveal is part of that same resolution, effectively immediate.
    bool cards_revealed_ = false;

    // Stake ids (does NOT match the announcement WAV numbering):
    //   kStakeKey (BJI4): win->GainKey(FindUnheldProvince), loss->LoseKey(last held by rotation)
    //   kStakePower30 (BJI3): win->AddPower(30), loss->SubtractPower(10)
    //   kStakePower20 (BJI2): win->AddPower(20), loss->SubtractPower(10)
    //   kStakeTurn (BJI1, always available): win->DecrementCurrentPlayerCyclic (extra
    //     turn), loss->skip_turn_counter=1 (skip a turn)
    static constexpr int kStakeKey = 0;
    static constexpr int kStakePower30 = 1;
    static constexpr int kStakePower20 = 2;
    static constexpr int kStakeTurn = 3;

    int player_ = 0;             // player index in the Blackjack game
    int stake_id_ = kStakeTurn;  // one of kStakeKey/Power30/Power20/Turn, picked in Enter()
    // The real game pre-rolls all 5 card slots for EACH side at deal time
    // (not one roll per hit), with per-hand rank dedup — reroll on a
    // duplicate WITHIN THE SAME hand (cross-hand duplicates are fine).
    // dealer_count_ means "how many of the 5 pre-rolled dealer slots are
    // revealed so far" — the dealer's own hits are always sequential.
    //
    // The PLAYER side is NOT a simple sequential counter — each of the 3
    // real hit zones (zone_rects_[0..2]) maps to a FIXED card slot
    // (slot = zone index+2 = 2/3/4) and can only be used once (guarded
    // by a per-slot "already revealed" flag). Each zone always maps to
    // its own fixed slot, independent of click order — clicking zone 2
    // reveals slot 4 even if slots 2/3 are still empty. The 4th zone
    // (zone_rects_[3]) is STAND: no card, ends the turn.
    // Scoring gate: slot 0 always counts unconditionally (the first
    // initial card); slots 1-4 only count if player_confirmed_[slot] is
    // set. The real game sets confirmed[0]/[1]=1 for BOTH initial cards
    // together at deal time — independent of when those 2 cards are
    // actually DRAWN on screen (that's cards_revealed_, a separate
    // ~3s-later visual gate).
    int player_cards_[5] = {};       // player hand: card values 1-13, 0=empty
    int dealer_cards_[5] = {};       // dealer hand: card values 1-13, 0=empty
    bool player_confirmed_[5] = {};  // per-slot "counts for scoring/drawn" flag
    int player_count_ = 0;  // number of confirmed player slots (0-5) — drives the 5-card charlie
                            // win check
    int dealer_count_ = 0;  // dealer cards revealed so far (0-5)
    bool dealer_hidden_ = true;  // second dealer card is face-down during PlayerTurn
    BjPhase phase_ = BjPhase::StakeAnnounce;
    bool player_won_ = false;  // set in Comparing phase. No separate tie/push
                               // outcome exists in the real game (a tie is a loss).
    bool clicked_ = false;     // debounce: true while mouse held, cleared on release
    int focus_index_ = 0;      // gamepad discrete-focus over zone_rects_ (0-2 hit, 3 stand)
    std::chrono::steady_clock::time_point
        focus_change_time_;  // reset whenever focus_index_ changes, anchors the render-side
                             // pulse to peak brightness right on switch
    // 2 distinct random suit icons — the real game draws these at deal
    // time via 2 different random rolls and shows both. Reloaded on
    // every deal (released then re-picked in DealCards), not loaded
    // once like table_tex_/announce_tex_/stake_icon_tex_.
    TextureHandle suit_tex_[2] = {};
    // Static dealing-phase background — drawn full-screen, same crop every
    // frame. The 10 card slots it shows are empty placeholders; each
    // revealed card's actual rank is a separate crop from suit_tex_,
    // drawn on top (see DrawBjCard in render()).
    TextureHandle table_tex_ = 0;       // G3/CARDTBLE.BMP — dealing-phase background,
                                        // 10 pre-drawn card-back slots + curse-style checkmark
    TextureHandle announce_tex_ = 0;    // G3/BJTABLEH.BMP — stake-announce background
    SpriteInfo announce_sprite_{};      // announce_tex_ wrapped for DrawFocusTint
    TextureHandle stake_icon_tex_ = 0;  // G3/BJBLIT.BMP — stake icon sheet (key/dice/
                                        // +20/+30/check/cross), cropped per announced stake
    std::chrono::steady_clock::time_point phase_time_;  // time when current sub-phase started
};

}  // namespace openfear::game
