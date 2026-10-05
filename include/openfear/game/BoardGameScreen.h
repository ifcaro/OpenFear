#pragma once

#include "openfear/game/GameScreen.h"
#include "openfear/game/BoardGameState.h"
#include "openfear/game/DuelState.h"
#include "openfear/game/MapFile.h"
#include "openfear/game/PlayerRenderState.h"
#include "openfear/game/FocusIndicator.h"
#include "openfear/game/subscreens/PauseScreen.h"
#include "openfear/game/subscreens/MinimapScreen.h"
#include "openfear/game/subscreens/BribeGame.h"
#include "openfear/game/subscreens/CureCurseGame.h"
#include "openfear/game/subscreens/FortuneDoorGame.h"
#include "openfear/game/subscreens/FlightMap.h"
#include "openfear/game/subscreens/CoffinChoiceGame.h"
#include "openfear/game/subscreens/SlotMachineGame.h"
#include "openfear/game/subscreens/DoorsGame.h"
#include "openfear/game/subscreens/DiceKeysGame.h"
#include "openfear/game/subscreens/OneDieGame.h"
#include "openfear/game/subscreens/KeyAwardGame.h"
#include "openfear/game/subscreens/DuelAttackGauge.h"
#include "openfear/game/subscreens/DuelChooseOpponent.h"
#include "openfear/game/subscreens/DuelStealOrCurse.h"
#include "openfear/game/subscreens/BlackjackGame.h"
#include "openfear/assets/AssetManager.h"
#include "openfear/platform/IRenderer.h"
#include "openfear/platform/IAudio.h"
#include "openfear/platform/IInput.h"
#include "openfear/platform/IVideoPlayer.h"
#include <array>
#include <cstdint>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace openfear::game {

struct GameState;

// Main board gameplay loop: board rendering, turn management, cursor
// control, dice rolls and movement, encounter dispatch, and the embedded
// minigames (each its own class under subscreens/, held here by instance -
// see below for the state this class keeps for itself/shares with them).
class BoardGameScreen : public GameScreen {
public:
    BoardGameScreen(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio,
                    IVideoPlayer& video);
    ~BoardGameScreen() = default;

    // Configure difficulty-dependent initial state (call before load()).
    // initial_keys: province keys the human player starts with (0,2,4,6).
    // ai_power: starting power for AI opponents (10, 20, or 25 per difficulty level).
    // player_fears: per-player fear index (1-6, 1-indexed; 0 = unset), used
    // by gatekeeper elimination.
    void Configure(int initial_keys, uint16_t ai_power, const int* player_fears = nullptr,
                   int num_fears = 0);

    // Force a specific encounter type on every trigger (0 = disabled, use
    // normal RNG). Bypasses the stone-type check and bucket probability.
    void SetForceEncounterType(int enc_type);

    // Manual-QA-only: make SetForceEncounterType's override self-clear after
    // firing once — lets the first landing force the type, every landing
    // after rolls naturally. Used by --test-encounter-once.
    void SetForceEncounterTypeOnce(bool once);

    // Manual-QA-only, not ASM-derived: suppresses the AI-initiated duel
    // check, so a 2-player quickstart isn't interrupted while testing
    // something else.
    void SetDisableAiDuel(bool disable) { debug_disable_ai_duel_ = disable; }

    // Manual-QA-only: speeds up a full playthrough to GameOver for
    // testing. Not ASM-derived. (1) "stop at any stone" always available
    // regardless of real key ownership; (2) a province's key-stone pool
    // never exhausts.
    void SetCheatEasyWin(bool enable) { debug_cheat_easy_win_ = enable; }

    // Manual-QA-only: teleport the current player onto the first stone of
    // the given MapFile type (0-7) found by scanning every province, then
    // run the same arrival dispatch a real landing would.
    // Call after load(). No-op if no stone of that type exists.
    // repeat: for stone types 3/4 only, resolves ResolveFateCard()/ResolveFortuneCard() this
    // many times total on the same player/stone (to sample every RNG branch), one at a time
    // as each card's own audio finishes.
    void DebugForceStoneArrival(int stone_type, int repeat = 1);

    // Manual-QA-only: jump straight into ShowingPortrait for the current
    // player, to inspect a character's turn-portrait key-icon strip.
    // Pairs with DebugSetPlayerKeys(0, 0x3F).
    void DebugShowPortrait();

    // Manual-QA-only: teleport to province 5, stone 63, facing West, with
    // 1 step queued and already in the Moving state, to exercise the
    // province-5 stone 63->64 slide (HSLIDE.AVI) with a single advance click.
    void DebugSetupTobogganTest();

    // Manual-QA-only: teleport to a portal-table gate (index 0-23, same
    // indexing as kPortalTable) at its source province/stone, auto-
    // detecting the trigger direction and granting all 6 keys, so any of
    // the 24 real portals can be spot-checked with one click.
    void DebugSetupGateArrivalTest(int door_index);

    // Manual-QA-only: teleport to province `province`, stone 0, for
    // spot-checking tunnel entry (auto-detects the wall direction).
    void DebugSetupTunnelEntryTest(int province);

    // Manual-QA-only: teleport into province 7 at the tunnel-entry stone
    // for origin_province, for spot-checking tunnel exit.
    void DebugSetupTunnelExitTest(int origin_province);

    // Manual-QA-only: teleport to province 6, stone 15, facing East, with
    // 1 step queued and all 6 keys granted, to exercise the real
    // Gatekeeper win-challenge with a single advance click.
    void DebugForceGatekeeperChallenge();

    // Manual-QA-only: force EndTurn()'s Gatekeeper roll to a deterministic
    // loss instead of real RNG (the real roll is a rare 1/6, impractical
    // to hit by chance during manual testing). No effect if the winning
    // player's fear_index is 0 (always survives in that case).
    void SetForceGatekeeperLose(bool force) { force_gatekeeper_lose_ = force; }

    // Manual-QA-only: teleport to the first type-6 (major junction) stone
    // found, with steps_remaining>0 and already mid-roll — the precondition
    // for the Flight-key power (F1-F24 mid-roll on a type-6 stone). Also
    // grants all 6 keys.
    void DebugSetupFlightTest();

    // Manual-QA-only: set a player's held-key bitmask directly (bit i =
    // province i key held). Must be called after load().
    void DebugSetPlayerKeys(int player_idx, uint8_t key_mask);

    // Manual-QA-only: force a player's fortune_card_targeted[attacker_char_id]
    // flag, simulating "already named attacker_char_id in a FortuneCard"
    // without landing on a type-4 stone. Used by --test-duel-counter to
    // reach the "Protection" branch on demand.
    void DebugSetFortuneCardTargeted(int target_player_idx, int attacker_char_id);

    // Manual-QA-only: force a player's curse_type (curse mode 1-6)
    // directly, so CureCurseGame's automatic trigger can be exercised by
    // just playing that player's turns. Used by --test-curse-mode.
    // curse_mode==0 clears the curse.
    void DebugSetPlayerCurse(int player_idx, int curse_mode);

    // Manual-QA-only: force a player's power value directly, so
    // power-tiered formulas can be exercised at a specific band without
    // grinding turns. Used by --test-power.
    void DebugSetPlayerPower(int player_idx, uint16_t power);

    // Manual-QA-only: force a player's dice_restricted/dice_restriction_turns
    // fields directly, without forcing every subsequent encounter to type
    // 4 — so later natural rolls can reach the real Bucket3 reset path.
    // Used by --test-dice-restricted.
    void DebugSetDiceRestricted(int player_idx, bool restricted);

    // Manual-QA-only: force a province's shared key-remaining pool
    // directly, to exercise the "no keys left" branch without draining it
    // via 6 separate players. Used by --test-no-keys-left.
    void DebugSetProvinceKeysRemaining(int province, uint16_t remaining);

    // Manual-QA-only: force the next 2-dice roll to come up double-six,
    // triggering "The Chosen One" deterministically. Self-disarms after one use.
    void DebugForceDoubleSixRoll();

    // Pure formula: is KeyAward (enc type 15)'s target roll range
    // restricted to the player's remaining unheld keys? True when under
    // 10 minutes remain in the match AND the player holds fewer than 3 keys.
    static bool ComputeKeyAwardRestricted(long long remaining_seconds, int held);

    // Pure formula: KeyAward (enc type 15) target key count to roll
    // against. restricted: result of ComputeKeyAwardRestricted(). held:
    // keys already held (0-5). rng_raw: raw RNG output in [0, 0x7FFF].
    // Returns fixed 1 when not restricted; otherwise
    // RngNext()*(6-held)/0x8000+1, range 1..(6-held).
    static int ComputeKeyAwardTarget(bool restricted, int held, uint32_t rng_raw);

    // Pure formula: black-hole retry roll (1-6).
    static int ComputeBlackHoleRoll(uint32_t rng_raw);

    // Pure formula: does a black-hole retry attempt resolve (escape) this
    // turn? Resolves if retry_flag==4 (guaranteed cap, matches the
    // manual's worst-case "lose 3 turns") or retry_flag > roll.
    static bool ComputeBlackHoleResolves(uint16_t retry_flag, int roll);

    // Pure formula behind FindUnheldProvince(): first unheld province in
    // kKeyTable rotation order starting from char_id, or -1 if all 6 are held.
    static int ComputeFirstUnheldInRotation(int char_id, const uint16_t keys[6]);

    // Pure formula behind FindHeldProvinceByRotation(): LAST held province
    // in kKeyTable rotation order (full loop, keeps overwriting — does NOT
    // stop early), or -1 if none held.
    static int ComputeLastHeldInRotation(int char_id, const uint16_t keys[6]);

    // Pure formula: total key-pool tokens to distribute for a given player
    // count (1-6).
    static int ComputeKeyPoolTokenCount(int total_players);

    // Pure formula: seeds a 6-province pool array by distributing
    // ComputeKeyPoolTokenCount(total_players) tokens round-robin starting
    // from start_province (0-5). Player counts that don't divide 6 evenly
    // leave some provinces with more available copies than others.
    static void SeedKeyPool(uint16_t pool[6], int total_players, int start_province);

    bool load(const AssetPaths& paths);
    bool update(const IInput& input);
    void render();
    void unload();

    // True if the winning player survived the Gatekeeper roll; false too
    // if the match ended by timer_over (no valid winner_char_id_). The
    // roll runs once inside EndTurn() - RunPostGameSequence reads the
    // result here instead of rolling again.
    bool GatekeeperMiss() const {
        return (winner_char_id_ >= 0 && winner_char_id_ < 6) ? winner_gatekeeper_survived_ : false;
    }

    // True if the player quit via the Pause menu's Exit button — no
    // ENDVID/credits, just app exit. main.cpp must check this BEFORE
    // running the normal post-game sequence when update() returns true.
    bool pause_quit_requested() const { return pause_screen_.quitRequested(); }

    // Advances and returns the next 15-bit LCG value (same RNG as
    // in-game). Public so PostGameSequence.cpp uses the same seed stream.
    uint16_t RngNext();

private:
    // Every minigame/subscreen class holds a BoardGameScreen& board_ and
    // reads/writes the members below directly - friended here instead of
    // making them public, so access stays limited to this known set of
    // classes.
    friend class PauseScreen;
    friend class MinimapScreen;
    friend class BribeGame;
    friend class CureCurseGame;
    friend class FortuneDoorGame;
    friend class FlightMap;
    friend class CoffinChoiceGame;
    friend class SlotMachineGame;
    friend class DoorsGame;
    friend class DiceKeysGame;
    friend class OneDieGame;
    friend class KeyAwardGame;
    friend class DuelAttackGauge;
    friend class DuelChooseOpponent;
    friend class DuelStealOrCurse;
    friend class BlackjackGame;

    // ---- Everything below this point is the internal API consumed by the
    // minigame classes above (via the friend declarations).
    // Fields exclusive to one minigame live as private members of that
    // class instead; what's declared here is either genuinely shared
    // between 2+ minigames, or shared between a minigame and
    // BoardGameScreen's own board/movement logic. ----

    // State management
    BoardGameState current_state_ = BoardGameState::ShowingPortrait;
    uint8_t current_player_ = 0;  // index of player whose turn it is (0-5)
    int total_players_ = 0;       // total number of players (human + AI)

    // Board topology + FULLMAP.AVI frame lookup table.
    // province = GameState::player_idx, stone_id = GameState::sub_idx
    MapFile mapfile_;

    IVideoPlayer& video_;
    bool video_loaded_ = false;

    // Per-player render state — mirrors the original's render_state struct.
    // See PlayerRenderState.h for field documentation.
    PlayerRenderState render_state_[6] = {};

    SoundHandle portrait_wav_ =
        0;  // current player's theme sound (points into portrait_.theme_wav or .curse_hellin_wav)

    TextureHandle dice_tex_ = 0;  // MISC/MVDICE.BMP — real single-die overlay, also drawn
                                  // directly by KeyAwardGame::render()

    // State timing
    std::chrono::steady_clock::time_point state_start_time_;  // when current state began
    bool state_action_executed_ = false;  // guard to execute action once per state
    // Game-level clock — started on the FIRST ShowingPortrait entry (not
    // at load time), so turn 1 correctly shows the full game duration.
    std::chrono::steady_clock::time_point game_start_time_;

    SoundHandle rolling_wav_ = 0;  // precached ROLLING
    SoundHandle rolled_wav_ = 0;   // precached ROLLED

    // GkLoopAVI state — EYELOOP animated eye system (enc types 1,2,3,4,5,6,15,16).
    // Sub-type: RNG*3/0x8000 + 1 -> GKLOOP1/2/3.
    // GKLOOP{n}.AVI loops while insult audio plays; on audio done -> transition to
    // gkloop_post_state_.
    std::string gkloop_avi_path_;       // GKLOOP{1,2,3}.AVI path
    SoundHandle gkloop_enc_audio_ = 0;  // insult audio handle (track isPlaying)
    BoardGameState gkloop_post_state_ = BoardGameState::TurnEnd;  // state after GkLoop finishes
    SoundHandle bathory_key_redirect_wav_ = 0;
    bool bathory_key_redirect_audio_pending_ = false;

    // Shared across every discrete-focus picker state in this file
    // (FlightMap, PauseScreen, DiceKeys, BlackjackGame PlayerTurn,
    // DuelChooseOpponent, DuelStealOrCurse) — safe since only one such
    // state is ever active at a time.
    StickRepeater stick_repeater_;

    // Duel subsystem's shared state — see DuelState.h.
    DuelState duel_;

    // Reopen FULLMAP.AVI after a TeleportAVI sequence closes it
    void ReopenFullmapAvi();
    void StartBathoryKeyRedirectAudio();
    // Starts (if pending) or checks (if already started) the Bathory key-redirect
    // chime. Returns true if the caller should keep waiting before advancing past
    // its own result display — false once there's nothing left to play.
    bool BathoryKeyRedirectAudioBusy();

    // Frees every lazy-loaded minigame's assets, plus the Duel
    // subsystem's own textures. Called every TurnEnd.
    void FreeLazyMinigameAssets();

    // Manual-QA-only: common tail of every Debug*Test() teleport helper — enters Moving with
    // `steps` queued. Caller must set province/stone_id/direction (and any keys) first.
    void DebugEnterMoving(int steps);

    // Duel's curse branch: sets loser's curse_type to
    // kCurseTable[winner_char_id] and resets their curse_step_counter.
    void ApplyCurse(int loser, int winner_char_id);

    // Add `amount` to player's power. No cap (uint16 wraps at 65535 — never reached in practice).
    void AddPower(int player, uint16_t amount);

    // Subtract `amount` from player's power. Floored at 0 (never negative).
    void SubtractPower(int player, uint16_t amount);

    // Grant the key for `province` to `player`.
    void GainKey(int player, int province);

    // Remove the key for `province` from `player`.
    void LoseKey(int player, int province);

    // MapFile stone type 3 (FateCard) landing effect. Unconditional,
    // always negative: SubtractPower / skip-turn / GateOpen-teleport.
    // Shows the FF1 Fate province+amount plaque (or a fixed plaque for the
    // non-power variants) via ShowCard(). Replaces the enc_type 1-16 roll
    // entirely for this landing.
    void ResolveFateCard(int player);

    // MapFile stone type 4 (FortuneCard) landing effect.
    // Unconditional, always beneficial or neutral: AddPower / extra-turn /
    // gain-key / name-opponent(no-op). Shows the FF2 Fortune
    // province+amount plaque (or a fixed/char-indexed plaque for the
    // other 3 results). Replaces the enc_type 1-16 roll entirely for this
    // landing.
    void ResolveFortuneCard(int player);

    // Shared tail of ResolveFateCard/ResolveFortuneCard: swap
    // card_tex_ for the plaque at folder/name. TurnEnd clears it once
    // card_wav_ finishes playing, not on a fixed duration.
    void ShowCard(const char* folder, const char* name);

    // Pick a random entry from a WAV table (uniform, RngNext()) and play it.
    SoundHandle RollAudio(const char* const* table, int count);

    // Picks a random entry from table (uniform, RngNext()) and plays it,
    // unless the player has "The Chosen One" set, in which case it plays
    // fixed_wav instead and consumes no RNG for the pick.
    SoundHandle PlayEncounterAudio(int player, const char* fixed_wav, const char* const* table,
                                   int count);

    // Number of provinces currently held (0-6).
    int CountHeldKeys(int player) const {
        int n = 0;
        for (int k = 0; k < 6; ++k)
            if (render_state_[player].keys[k]) ++n;
        return n;
    }

    // Number of players not yet eliminated.
    int CountActivePlayers() const {
        int n = 0;
        for (int i = 0; i < total_players_; ++i)
            if (!render_state_[i].eliminated) ++n;
        return n;
    }

    // True if any player this match is AI-controlled (1P+AI mode). False in
    // pure multiplayer (2-6 human players, no AI).
    bool HasAiPlayer() const {
        for (int i = 0; i < total_players_; ++i)
            if (render_state_[i].is_ai) return true;
        return false;
    }

    // Find first unheld province via the real key-rotation table.
    // Returns -1 if player already has all 6 keys.
    // kKeyTable = {0,5,1,4,2,3} — iterates (kKeyTable[i] + char_id) % 6, stops at first unheld
    // slot.
    int FindUnheldProvince(int player) const;

    // Find the LAST held province in kKeyTable rotation order (full loop,
    // keeps overwriting). Returns -1 if player holds no keys. Picks which
    // key is at stake/lost for Bribe (enc_type 12), Blackjack's key stake,
    // and CoffinChoice's id==6 outcome.
    int FindHeldProvinceByRotation(int player) const;

    // Rotate current_player_ backward by one, wrapping at 0. Used for the
    // "extra turn" bonus (enc_type 6, FortuneCard result==1, OneDie's
    // enc9 prize case 3, Blackjack's BJI1 win) and for DuelChooseOpponent's
    // self-targeted duel cancel.
    void DecrementCurrentPlayerCyclic();

    // Finds a direction with a valid FULLMAP.AVI frame at (province, stone_id), called after
    // arriving at a new stone. No-op if the current mapfile_direction_ already has one.
    // Otherwise scans dirs 0-3 for the first valid one and writes it to both
    // mapfile_direction_ (the real 4-value board direction) and cursor_state_.direction
    // (the 3-value LEFT/RIGHT/HORIZONTAL cursor sprite, kept in sync with it).
    void AutoOrientDirection(uint16_t province, uint16_t stone_id);

    // Moves player to their province's nearest black-hole stone, faces them
    // toward the first valid direction out of it, and arms
    // black_hole_retry_counter unconditionally — even if no black-hole
    // stone was found — so the next movement plays the BHOLE AVI.
    void TeleportToNearestBlackHole(int p);

    // Test-only access seam (apps/tests/BoardGameScreenTestAccess.h) — lets
    // headless tests reach private minigame members / DispatchEncounter()
    // without widening the real public API. Inert in the shipped app: the
    // friended struct is only ever defined inside apps/tests/, so this
    // declaration has zero effect unless that test file is compiled in.
    friend struct BoardGameScreenTestAccess;

    // Board movement's OWN stick repeater — not shared with stick_repeater_
    // above, to avoid a stale armed_ from another picker firing a spurious
    // step on Moving's first frame. Reset on every Moving-state entry.
    StickRepeater move_stick_repeater_;

    // Fate/Fortune card overlay (ShowCard()). Also used by ResolveFortuneCard's
    // Protection result at DRAW time only - the later reveal when Protection
    // actually triggers uses DuelChooseOpponent's own protection_card_tex_.
    TextureHandle card_tex_ = 0;  // loaded Fate/Fortune card BMP (0 = none)
    bool card_active_ = false;
    SoundHandle card_wav_ = 0;  // hides on its own audio-done, not a fixed duration

    // Cursor system: 9 total resources (3 SETs x 3 frames), animation
    // frame cycles 0-6 (0->1->0->1->0->1->2).
    struct CursorAnimationState {
        uint8_t animation_frame = 0;  // 0-6 (7-frame cycle)
        uint8_t direction = 2;        // 0=LEFT, 1=RIGHT, 2=HORIZONTAL (initial) — managed
                                      // exclusively by this default and AutoOrientDirection()
                                      // (called after any arrival/teleport/flight pick)
        bool is_animating = false;    // true during player's turn
        std::chrono::steady_clock::time_point last_frame_time;
        int animation_interval_ms = 200;  // 200ms per frame, real SETTIMER(id=2,0xC8) value
        float frame_accumulator = 0.0f;   // dt accrued toward the next frame tick
    };

    BoardGameState black_flash_next_state_ =
        BoardGameState::ShowingPortrait;  // destination after BlackFlash

    // Current dice roll/UI state — one roll's worth of results plus the
    // animation/lock bookkeeping around it.
    struct DiceRollState {
        int result = 0;              // die 1 individual roll (1-6)
        int result2 = 0;             // die 2 individual roll (1-6)
        int result3 = 0;             // die 3 individual roll (1-6)
        int move_distance = 0;       // chosen move: result (1-die) OR result2+result3 (2-dice)
        int selected_option = 0;     // 0=spinning, 1=die1 selected, 2=die2+die3 selected
        uint16_t anim_counter = 0;   // 0..9999; frame = (counter / 4) % 6
        bool result_locked = false;  // true: die clicked, waiting on ROLLED.WAV
        // Gamepad discrete-focus, reusing GetMouseZone()'s LEFT/RIGHT/CENTER zone
        // numbering (0/1/2) for consistency — 2 (CENTER) is never set since the
        // dice choice only has 2 real options.
        int focus = 0;  // 0=1-die, 1=2-dice
    } dice_;

    uint32_t rng_seed_ = 0;  // random number generator seed (LCG)

    CursorAnimationState cursor_state_;

    struct BoardRenderContext {
        int tile_width = 64;
        int tile_height = 64;
        int board_x = 0;
        int board_y = 77;
    } render_ctx_;

    int last_video_frame_ = -1;  // cache last seeked frame to avoid redundant seeks

    // Resources reloaded every turn for whoever's portrait is currently showing —
    // same lifecycle for all 9 (loaded in EnterShowingPortrait, released at the same
    // handful of exit points), matching the original's own per-turn reload pattern
    struct PortraitResources {
        // Normal INFO{char_id}_0.BMP, or for a cursed player (curse_type 1 or 5)
        // the one curse bitmap that applies this turn instead (MG{char_id}_{stage}.BMP
        // or BROKEN_{char_id}.BMP).
        TextureHandle tex = 0;
        // CHARS/INFO{char_id}_1.BMP — the 6 province key icons are 6 crops of this one
        // texture, offset by src_x = province*44 each.
        TextureHandle key_tex = 0;
        // Mode 3 (Khufu): C1/K_CURSE.BMP (640x480), drawn every frame instead of
        // fullmap once ms_in_moving>=kDiceGraceMs while curse_type==3, see
        // render()'s Layer 1.
        TextureHandle curse_indicator_tex = 0;
        SoundHandle curse_blindness_wav = 0;  // mode 3: W6/AT_0597.WAV (Khufu's blindness taunt)
        SoundHandle curse_hellin_wav = 0;     // mode 5: W1/AT_0082.WAV, one-time on first display
        // Mode 3 (Khufu) — has TriggerCurseIndicatorIfKhufu() already fired for the
        // CURRENT roll? Reset false at Moving-state entry, set true the moment
        // ms_in_moving first crosses kDiceGraceMs (edge-triggered). Prevents
        // re-playing the one-shot blindness-curse audio every frame.
        bool curse_move_audio_played = false;
        SoundHandle theme_wav = 0;  // WAV/{HOWL,BJUJU2,EGYPT,BATS,WITCH,HELEN}.WAV per char_id
        SoundHandle turn_wav = 0;   // WAV/TURN{n}.WAV — plays at END of movement
        // WAV/MOVE{n}.WAV, or WAV\RIBBIT1.WAV instead when curse_type==2
        // (Anne/toad curse), chosen at load time in EnterShowingPortrait.
        SoundHandle move_wav = 0;
        // Set once LoadDeferredPortraitResources() has run for this turn.
        bool deferred_loaded = false;
    } portrait_;

    // Key icon slot rects — loaded once in load() from DATA/PLAYER.BIN.
    std::array<assets::RectEntry, 6> key_slot_rects_{};
    // Movement/dice-choice click zones — DATA/MAPMOVE.BIN: 3 polygons
    // (count-prefixed vertex lists). Index matches GetMouseZone()'s
    // return value: 0=LEFT, 1=RIGHT, 2=CENTER.
    std::array<std::vector<std::pair<int16_t, int16_t>>, 3> move_zone_polys_{};
    // Step counter box click zone — DATA/MAPMOVE.BIN's trailing 8 bytes
    // (x1,y1,x2,y2), read in the same file session as the 3 polygons
    // above. Defaults to the known-good real values if the file is
    // missing/short.
    int counter_box_x_ = 290, counter_box_y_ = 410, counter_box_w_ = 65, counter_box_h_ = 64;
    SoundHandle timeout_wav_ = 0;           // W1/AT_0063.WAV (precached)
    SoundHandle portal_blocked_wav_ = 0;    // WAV/STOP.WAV — plays when a portal/passage
                                            // entry is blocked for lacking the required key(s)
    SoundHandle movement_blocked_wav_ = 0;  // WAV/AT_0426,0514{A,B,C,E}.WAV — shared by
                                            // HandleMovementInput's 3 "can't do that" insult
                                            // sites; skip (not stack) while one is already playing.

    // Idle ambient nag pool. While idling with movement input unlocked
    // and no other movement audio busy, rolls RNG*15/0x8000 each ~10fps
    // tick; on a 1/15 hit, plays a fresh uniform-random pick from this
    // 12-clip pool — unlike the per-char_id/curse-gated audio elsewhere in
    // this class, the same 12-clip pool plays for every player.
    SoundHandle ambient_nag_wavs_[12] = {};  // WAV/AT_0471E,0472C,0474{A-G},0475{A-C}.WAV
    SoundHandle ambient_nag_wav_ = 0;        // currently-loaded/playing pick

    SpriteInfo dice_sprite_ = {};  // dice_tex_ wrapped for drawSpriteTinted (gamepad focus pulse)

    bool game_timer_started_ = false;  // true once game_start_time_ has been set
    bool timer_expired_ = false;       // set when game-timer expiry is handled; prevents re-trigger
    // Set the moment <15s of real game time remain (checked every frame,
    // any state). EndTurn() only acts on it at the next turn boundary, not
    // instantly — so the match can end anywhere from ~14s to negative
    // remaining, depending on how long the in-progress turn takes.
    bool near_end_flag_ = false;

    // Difficulty config — set by Configure() before load() for 1-player mode.
    int initial_keys_count_ = 0;  // keys [0..N-1] granted to human player at game start
    uint16_t ai_initial_power_ =
        0;  // starting power for AI opponents (10, 20, or 25; 0 = not 1P mode)

    // Test hook — set by SetForceEncounterType() before load().
    int force_encounter_type_ = 0;  // 0 = disabled (normal RNG), 1-16 = always dispatch this type
    bool force_encounter_type_once_ = false;
    bool force_double_six_roll_ = false;  // --test-chosen-one: next 2-dice roll = 6+6
    bool debug_disable_ai_duel_ = false;  // --test-no-ai-duel: manual-QA-only, not ASM-derived
    bool debug_cheat_easy_win_ = false;   // --cheat-easy-win: manual-QA-only, not ASM-derived
    bool force_gatekeeper_lose_ = false;  // manual-QA-only, see SetForceGatekeeperLose()
    // --test-stone-repeat: remaining ResolveFateCard()/ResolveFortuneCard() calls, paced one
    // per card instead of all at once — see DebugForceStoneArrival().
    int debug_stone_repeat_remaining_ = 0;
    int debug_stone_repeat_type_ = 0;  // 3 or 4, matches debug_stone_repeat_remaining_

    // Portrait stat strings — computed ONCE at ShowingPortrait entry (not animated).
    char portrait_time_str_[16] = {};  // time remaining, e.g. "02:34" or "120"
    char portrait_power_str_[8] = {};  // player power, e.g. "0000"

    // Step counter display
    TextureHandle counter_tex_ = 0;   // MISC/COUNTER.BMP — 65x64 step count box background
    SpriteInfo digit_font_sprite_{};  // data/fonts/digitfont.bmp — digit strip (16x20 per cell),
                                      // steps/time/power displays

    // Last zone GetMouseZone() returned, for cursor visual feedback during Moving.
    uint8_t current_mouse_zone_ =
        2;  // 0=left (rotate left), 1=right (rotate right), 2=center (advance)

    // Turn-level timeout: timer starts when dice begin spinning (RollingDice entry), fires
    // in RollingDice/Moving/MinimapScreen/Encounter/OneDieGame/KeyAwardGame. On expiry: stop
    // rolling audio, play AT_0063.WAV, enter TurnTimeoutWait.
    std::chrono::steady_clock::time_point turn_start_time_;  // set at RollingDice state entry
    bool turn_timeout_triggered_ = false;                    // true once timeout fired this turn
    uint32_t turn_time_limit_ms_ = 0;                        // from GameState.turn_time_limit_ms_

    std::chrono::steady_clock::time_point last_move_time_;  // time of last successful step
    bool timeout_warn_played_ = false;  // true once first idle-warning sound has played

    // MAPFILE direction (0=N,1=E,2=S,3=W), the real board direction for pathfinding —
    // separate from cursor visual direction (3-value LEFT/RIGHT/HORIZONTAL) and from
    // render_state_[p].direction (each player's own saved copy while not their turn;
    // this is the single live copy for whoever's turn it is now).
    uint8_t mapfile_direction_ = 0;

    // Set by DecrementCurrentPlayerCyclic() when it already did EndTurn()'s own
    // position-save itself (at the correct, not-yet-decremented
    // current_player_) — tells EndTurn() not to redo that save.
    bool skip_position_save_ = false;

    // Winner tracking — set in EndTurn() when a win flag is detected.
    int winner_char_id_ =
        -1;  // char_id (0-5) of the winning player, or -1 if ended by timer/elimination
    // The Gatekeeper survive/eliminate roll happens here in EndTurn(), not
    // re-rolled later in the post-game sequence — lets it pick the right
    // audio pool without a second (potentially disagreeing) roll.
    bool winner_gatekeeper_survived_ = false;

    // Dice modifier flags now in render_state_[]:
    // render_state_[p].dice_restricted, render_state_[p].even_dice

    // Special AVI state (TeleportAVI). Used for: toboggan (province 5
    // stone 63->64), province-portal crossings, and the Gatekeeper's
    // FEAR{roll}.AVI challenge.
    struct TeleportAviState {
        std::string path;         // AVI to play (opened on TeleportAVI entry)
        uint16_t dest_stone = 0;  // stone to teleport to after AVI finishes
        int dest_prov = -1;       // province to teleport to after AVI (-1 = no change)
        // Arrival-facing direction, applied at TeleportAVI completion instead of
        // immediately at crossing time - writing mapfile_direction_ early would
        // leak the post-arrival direction into the TeleportVideoPreload hold frame
        // (still showing the OLD province/stone), producing a frame that
        // combines an old position with a new direction that was never meant to
        // render. -1 = no override (AutoOrientDirection's own fallback applies).
        int dest_direction = -1;
        BoardGameState post_state =
            BoardGameState::Moving;  // state after a RollEnding finishes (Moving/TurnEnd/GameOver)

        // What the completion handler does once the currently playing AVI finishes:
        //   RollEnding          — go to post_state (the default; covers the central
        //                         portal win).
        //   MidRollContinue     — resume Moving as-is (steps_remaining untouched)
        //                         instead; the player is still mid-roll (province
        //                         portal, tunnel up/down, or the toboggan).
        //   GatekeeperChallenge — chain into StartGatekeeperResultTaunt() (a GKLOOP1
        //                         taunt) before post_state, instead of going there
        //                         directly.
        enum class Kind { RollEnding, MidRollContinue, GatekeeperChallenge };
        Kind kind = Kind::RollEnding;

        // True while the step-counter box should stay visible during the currently
        // playing AVI — every use hides it except the province-5 stone 63->64 slide
        // (HSLIDE.AVI).
        bool shows_counter = false;
    } teleport_;

    bool gatekeeper_taunt_survived_ = false;  // selects insult pool: survived (6) vs eliminated (3)

    // Path of the video to play when the player lands exactly on a key
    // stone (AV3/KEY{province} or AV3/NOKEY{province} if the pool's
    // exhausted) or a black-hole stone (AV1/BHOLE{province}, first arrival
    // or each retry) — replaces the normal enc_type 1-16 roll for that
    // landing. Set by ResolveKeyStoneArrival()/ResolveBholeStoneArrival(),
    // consumed when the StoneArrivalAVI state opens it.
    std::string stone_arrival_avi_path_;

    // Shared, GLOBAL (not per-player) province key stock — MapFile stone
    // type 1 (key square) draws from this pool as ANY player collects a
    // province's key; once exhausted, further type-1 landings show the
    // "no keys left" AVI instead. Seeded by SeedKeyPool() in Configure():
    // total tokens scale with player count, distributed round-robin from a
    // random starting province, so some provinces can end up with more
    // available copies than others. Zeroed here only as a safe
    // pre-Configure() default.
    uint16_t province_keys_remaining_[6] = {0, 0, 0, 0, 0, 0};

    // Starts the fixed-subtype GKLOOP1 taunt after the Gatekeeper's
    // FEAR{roll}.AVI closes. Resets teleport_.kind, picks the
    // insult pool from gatekeeper_taunt_survived_, and transitions via
    // BlackFlash into GkLoopAVI with gkloop_post_state_ = teleport_.post_state (TurnEnd).
    void StartGatekeeperResultTaunt();

    // Render COUNTER.BMP background + pixel-art step number at (290, 410)
    void DrawStepCounter(int steps);

    // Draw a stats string (digits + ':') left-aligned using digit_font_sprite_.
    // text: null-terminated string of '0'-'9' and ':' (colon at strip index 10).
    // x/y: screen position of the left/top edge of the rendered text.
    void DrawStatText(const char* text, int x, int y);

    // render()'s ShowingPortrait branch — never falls through to the shared
    // board-drawing code below it.
    void RenderShowingPortrait();

    void UpdateCursorAnimation(float dt);

    // update()'s top-of-frame checks, run before the state-machine switch.
    // Sets near_end_flag_ once under 15s remain on the overall game timer.
    void CheckGameTimerNearEnd();
    // Fires once the current turn exceeds turn_time_limit_ms_: either
    // resolves the pending step arrival (if already at steps_remaining==0)
    // or reverts the player to their turn-start position and plays the
    // timeout line.
    void CheckTurnTimeout();

    // The 5 largest update() state handlers, one per BoardGameState case.
    // Private members since each reaches into a large slice of
    // BoardGameScreen's own state.
    //
    // Shows the current player's portrait + plays their theme WAV (set up
    // by EnterShowingPortrait() on first entry). Checks the duel/pause
    // corner clicks every frame; once the theme audio finishes, resolves
    // the black-hole retry or transitions onward.
    void UpdateShowingPortrait(const IInput& input);

    // UpdateShowingPortrait()'s own sub-steps, in call order.
    // Loads portrait_.tex/key_tex and starts their theme WAV — the minimum needed for
    // the portrait to appear and sound right away. Called on ShowingPortrait's first frame.
    void EnterShowingPortrait();
    // Loads the rest of portrait_'s per-turn resources (video reopen, curse indicator,
    // turn/move WAVs), one frame after EnterShowingPortrait() so their I/O doesn't delay
    // the portrait's first frame.
    void LoadDeferredPortraitResources();
    // True once the portrait screen is ready to advance: at least 300ms
    // elapsed (avoids a same-frame false-done right after playLoaded()),
    // then either the theme WAV has finished playing, or — if none
    // loaded — a flat 2s has passed.
    bool ComputePortraitAudioDone() const;
    // Each Handle*Click() returns true if it fired (caller must return
    // immediately after, matching the original's early `return` inline).
    bool HandleDuelCornerClick(const IInput& input);
    bool HandlePauseCornerClick(const IInput& input);
    // Shared setup for DuelChooseOpponent, used by both the human corner-click and the
    // AI auto-challenge path: counts eligible targets into duel_.choose_num_candidates,
    // lazy-loads the picker's icon textures and glass/prompt WAVs, and plays the prompt
    // (skipped if the count is 0). Returns the candidate count.
    int PrepareDuelChooseOpponent();
    // Returns true if it fired (caller must return immediately after).
    bool ResolveBlackHoleRetry();
    // AI: runs the duel-eligibility check (curse/power/key gates) and
    // either opens DuelChooseOpponent or falls through to TurnEnd — AI
    // never gets a random encounter roll. Human: HomeAVI on their first
    // turn, WaitingForPlayer otherwise.
    void DispatchTurnStartAfterPortrait();

    // On entry, resets the dice-spin animation and arms the turn-level
    // timeout. No roll happens yet — that only fires once the player
    // picks 1-die or 2-dice.
    void UpdateRollingDice(const IInput& input, float dt);
    // On entry, initializes steps_remaining from dice_.move_distance and
    // snapshots steps_total_this_roll.
    void UpdateMoving(const IInput& input, float dt);
    // Opens/plays whichever AVI teleport_.path currently points at,
    // then on completion routes by teleport_.kind to either resume
    // Moving, chain the Gatekeeper taunt, or teleport_.post_state.
    void UpdateTeleportAVI();

    // Rolls only the option (1 or 2 dice) actually chosen, after the
    // click — never rolls all 3 upfront.
    void RollDiceForOption(int option);
    void TriggerEncounter();
    void DispatchEncounter(int player);  // 16-case encounter type dispatch

    // DispatchEncounter()'s decision engine — picks WHICH of the 16
    // encounter types fires this turn (or 0, meaning none).
    uint16_t DetermineEncounterType(int player);
    // Seconds left in the game (game_duration_s_ minus real elapsed time since
    // game_start_time_), shared by every time-gated encounter/minigame check.
    long long RemainingGameSeconds() const;
    // Catch-up signal shared by multiple weighted-pick systems (the main encounter-slot
    // table and OneDie's prize-tier table): positive when this player is short on keys
    // relative to real time remaining, negative when ahead.
    float ComputeKeyFactor(int player, long long remaining_seconds, long long total_seconds) const;
    // Catch-up signal shared the same way: positive when this player's power is above
    // the table average, negative when below.
    float ComputePowerFactor(int player) const;

    struct WeightedPickEntry {
        int id;
        float weight;
    };
    // Builds one category's entries (ids[0..count-1], a single shared `rate` for the
    // whole category) with the real game's index-based linear taper + capacity-based
    // rescale applied, appending into `out` starting at `out[out_count]` (caller-sized
    // buffer). `capacity` is the real table's total slot count, which varies per call
    // site (not always 3) — pass the actual count for that site, not a fixed constant.
    static void AppendWeightedCategory(const int ids[], int count, float rate, int capacity,
                                       WeightedPickEntry* out, int& out_count);
    // Same normalization as AppendWeightedCategory, but for a category whose real weights
    // aren't an index-based taper — takes the literal per-entry weight ratios directly.
    static void AppendLiteralWeightCategory(const int ids[], const float weights[], int count,
                                            int capacity, WeightedPickEntry* out, int& out_count);
    // Final cumulative roll over entries built via AppendWeightedCategory calls,
    // matching the real picker's scaling (RngNext()*100/32768) and saturating
    // fallback to the last entry. Deliberately does NOT reseed the RNG from the system
    // clock like the real picker does — same policy as RollEncounterSlot, no port
    // feature depends on matching the exact roll sequence. Consumes exactly 1
    // RngNext() call.
    int PickWeightedEntry(const WeightedPickEntry entries[], int count);

    // How likely each of the 5 weighted slots is to be picked this turn.
    void ComputeEncounterSlotWeights(int player, float out_weights[5]) const;
    // Rolls once against the cumulative weights, returns the picked slot
    // (0-4). Consumes exactly 1 RngNext() call.
    int RollEncounterSlot(const float weights[5]);
    // Slot 1 (~5%): OneDie / 1-die-restriction / Bribes.
    uint16_t RollDiceEffectEncounter(int player);
    // Slot 2: the 9 minigame types (Doors through KeyAward).
    uint16_t RollMinigameEncounter(int player);
    // Slot 3: extra-turn (type 6) or OneDie.
    uint16_t RollExtraTurnOrDiceEncounter(int player);

    // Shared tail for the 7 encounter types (1,2,3,4,5,6,0x10): rolls GKLOOP{1-3}.AVI,
    // queues `audio` as the taunt, transitions through BlackFlash -> GkLoopAVI -> TurnEnd.
    void QueueGkLoopTaunt(SoundHandle audio);

    // The 7 "big" encounter-type handlers (the other 9 are already 1-line minigame-Enter()
    // forwards, not worth their own method), each ending in a QueueGkLoopTaunt() call with
    // whichever audio it picked. Most apply a type-specific side effect first;
    // DispatchTaunt/DispatchEyeloopTaunt have none — audio only.
    void DispatchTaunt();
    void DispatchSkipNextTurn(int p);
    void DispatchBlackHoleTeleport(int p);
    void DispatchOneDieRestriction(int p);
    void DispatchEvenDiceOnly(int p);
    void DispatchExtraTurn(int p);
    void DispatchEyeloopTaunt();
    void EndTurn();

    // EndTurn()'s sub-steps, in call order. Each bool-returning one
    // mirrors EndTurn()'s own early-return: caller does `if (X()) return;`.
    bool CheckGameTimerExpiry();
    bool ResolveWinFlagGatekeeperRoll();
    void AdvanceToNextActivePlayer(GameState* gs);

    // Copies current_player_'s live position (province/stone_id) FROM GameState
    // INTO render_state_ — the 2 fields both AdvanceToNextActivePlayer() and
    // DecrementCurrentPlayerCyclic() save identically; each still writes .direction and
    // guards the call itself differently, so only this 2-field core is shared.
    void SaveCurrentPlayerPositionFromGameState(const GameState* gs);
    void TriggerCurseIndicatorIfKhufu();  // mode 3 (Khufu, real char_id 1): shows C1/K_CURSE.BMP +
                                          // W6/AT_0597.WAV at junction crossings

    // Point-in-polygon test against the 3 real MAPMOVE.BIN zones. Returns
    // 0/1/2 (LEFT/RIGHT/CENTER), or kNoZone if the click matches none of them.
    uint8_t GetMouseZone(int mouse_x, int mouse_y) const;

    // Sentinel for "no zone matched" — port-only, no equivalent value in the
    // original (it just does nothing on an unmatched click). 3 is simply the
    // next free value after the 3 real zone ids (0/1/2).
    static constexpr uint8_t kNoZone = 3;

    // Board-area render rect for video_.setRenderRect() — every fullscreen
    // AVI in the board area uses this exact rect.
    static constexpr int kBoardAreaX = 0, kBoardAreaY = 77, kBoardAreaW = 640, kBoardAreaH = 326;

    // Gates the dice->counter sprite swap and the Khufu curse bitmap in
    // Moving. 0 because the actual wait (dice_.result_locked, until
    // ROLLED.WAV finishes) already happens earlier in RollingDice.
    static constexpr long long kDiceGraceMs = 0;

    // Handle movement phase input: cursor rotation or advance one step.
    // zone: 0=left, 1=right, 2=center.
    void HandleMovementInput(uint8_t zone);

    // Shared LEFT/RIGHT zone handler: rotates the travel direction at a junction stone, or
    // reverses onto a dead-end's one other connection if no step has been taken this roll yet;
    // otherwise refuses with an insult. clockwise selects RIGHT (true) vs LEFT (false).
    void RotateOrReverseDirection(bool clockwise);

    // The 5 checks HandleMovementInput's CENTER branch runs, in real call order.
    // HandleCentralPortalWin is the only one that can legitimately not apply (returns
    // false), falling through to AdvanceNormalStep.
    void HandlePortalCrossing(uint16_t province, uint16_t stone_id);
    void HandleWallDestination(uint16_t province, uint16_t stone_id);
    void HandleTobogganSlide();
    bool HandleCentralPortalWin(uint16_t province, uint16_t stone_id, int16_t dest_raw);
    void AdvanceNormalStep(uint16_t province, uint16_t stone_id, uint8_t dir_raw);

    // Dispatches arrival at the current stone (step finished or Space's early
    // stop), deciding encounter entry from the stone's type.
    // skip_stone_type_effects: skip the type-1..4 special stone effects even if
    // the landing stone is one of those types; the generic enc_type 1-16 roll
    // still fires either way.
    void ResolveStepArrival(bool skip_stone_type_effects = false);

    // ResolveStepArrival()'s sub-steps, in call order.
    void BeginStepArrival();
    void ResolveKeyStoneArrival(uint16_t province);
    void ResolveBholeStoneArrival(uint16_t province);

    // True once enough time has passed since the last step (last_move_time_).
    // Gates all 3 input paths (mouse/gamepad/keyboard) identically.
    bool MovementCooldownElapsed() const;

    // Finds the next opponent `player` hasn't yet named for the Protection
    // effect (FortuneCard result 4) — scans player slots for the first
    // OTHER player whose fortune_card_targeted flag is still 0; with
    // commit=true, marks it. Returns that player's char_id, or -1 if
    // everyone else is already named.
    int FindNextProtectionTarget(int player, bool commit);

    // Minigame screens — own their update()/render()/unload()
    // logic; each holds only what's genuinely exclusive to it as a
    // private member, and a BoardGameScreen& board_ reference for the
    // shared state declared above. Constructed with *this, so declared
    // last (after every field they read/write exists).
    PauseScreen pause_screen_;
    MinimapScreen minimap_screen_;
    BribeGame bribe_screen_;
    CureCurseGame curecurse_screen_;
    FortuneDoorGame fortune_door_screen_;
    FlightMap flight_map_screen_;
    CoffinChoiceGame coffin_screen_;
    SlotMachineGame slot_screen_;
    DoorsGame doors_screen_;
    DiceKeysGame dicekeys_screen_;
    OneDieGame onedie_screen_;
    KeyAwardGame keyaward_screen_;
    DuelAttackGauge duel_attack_gauge_;
    DuelChooseOpponent duel_choose_screen_;
    DuelStealOrCurse duel_steal_or_curse_screen_;
    BlackjackGame blackjack_screen_;
};

}  // namespace openfear::game
