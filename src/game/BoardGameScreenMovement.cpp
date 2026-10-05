#include "openfear/game/BoardGameScreen.h"
#include "openfear/game/GameState.h"
#include "openfear/game/BoardInitializer.h"
#include "openfear/game/PlayerInitializer.h"
#include "openfear/game/PlayerObject.h"
#include "openfear/assets/AssetManager.h"
#include "openfear/assets/IAssetProvider.h"
#include "openfear/platform/BitmapFont.h"
#include "openfear/platform/Platform.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <random>
#include <vector>

namespace openfear::game {

// Inter-province portal table's entry type. kPortalTable's real definition lives in
// BoardGameScreen.cpp with external linkage, so this file can share it without duplicating
// the 24-entry table. Kept out of BoardGameScreen.h — not part of the class's public surface,
// just a .cpp-level implementation detail.
struct PortalEntry {
    uint16_t src_stone, src_prov, dst_stone, dst_prov;
};
extern const PortalEntry kPortalTable[24];

// Shared insult bank for a refused rotate/advance (dead-end/straight stone, step already
// taken this roll, or a wall in the travel direction). The real handler gates on 3
// preallocated sound-channel slots all being idle at once; this port approximates that with
// a single isPlaying()-gated handle shared across every refusal site.
static const char* const kMovementBlockedSounds[5] = {
    "WAV\\AT_0426.WAV",  "WAV\\AT_0514A.WAV", "WAV\\AT_0514B.WAV",
    "WAV\\AT_0514C.WAV", "WAV\\AT_0514E.WAV",
};

// Standard ray-casting point-in-polygon test (even-odd rule). The original
// uses GDI CreatePolygonRgn+PtInRegion (WINDING fill); for these specific
// simple, non-self-intersecting quadrilaterals the two fill rules agree
// everywhere except exactly on an edge.
static bool PointInPolygon(int px, int py, const std::vector<std::pair<int16_t, int16_t>>& poly) {
    bool inside = false;
    const size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const int xi = poly[i].first, yi = poly[i].second;
        const int xj = poly[j].first, yj = poly[j].second;
        const bool straddles = (yi > py) != (yj > py);
        if (straddles) {
            const double x_at_py = xi + (double)(xj - xi) * (double)(py - yi) / (double)(yj - yi);
            if (px < x_at_py) inside = !inside;
        }
    }
    return inside;
}

uint8_t BoardGameScreen::GetMouseZone(int mouse_x, int mouse_y) const {
    // Point-in-polygon test against move_zone_polys_ (loaded from DATA/MAPMOVE.BIN in
    // load(); the original used CreatePolygonRgn+PtInRegion per hotspot).
    for (uint8_t z = 0; z < 3; ++z) {
        if (PointInPolygon(mouse_x, mouse_y, move_zone_polys_[z])) {
            return z;  // 0=LEFT, 1=RIGHT, 2=CENTER
        }
    }
    // The 3 real polygons leave a real gap uncovered (below CENTER's bottom edge, y>409
    // between x=289 and x=356) — the original's click dispatcher does nothing on a miss, no
    // fallback zone, so this returns a sentinel instead of defaulting to CENTER. Also covers
    // MAPMOVE.BIN failing to load (empty polygons).
    return kNoZone;
}

bool BoardGameScreen::MovementCooldownElapsed() const {
    using namespace std::chrono;
    const long long elapsed_ms =
        duration_cast<milliseconds>(steady_clock::now() - last_move_time_).count();
    // Samedi's curse (curse_type==4) uses a confirmed real 1000ms window; the 200ms default
    // is empirically-tuned, not derived from a single real timeout value.
    const long long window_ms = (render_state_[current_player_].curse_type == 4) ? 1000 : 200;
    return elapsed_ms >= window_ms;
}

void BoardGameScreen::BeginStepArrival() {
    // The single arrival dispatcher — reached by a normal step hitting steps_remaining==0,
    // or by Space's "stop at any stone" (key_power_flags bit1) early-stop. Same function
    // either way, no distinct feedback for the early-stop case.
    cursor_state_.is_animating = false;

    // Disarms the turn-timeout check for the rest of this turn, matching the original's own
    // timing. Without this the clock keeps running under whatever encounter/minigame comes
    // next and can fire mid-minigame, stacking its timeout audio on top of the minigame's.
    turn_timeout_triggered_ = true;

    // dice_restriction_turns only increments while already nonzero (restriction active) —
    // once per completed roll, not per die pip, since this dispatcher runs once per arrival.
    // That's what lets TriggerEncounter's dice_restriction_turns>6 expiry gate mean "~7 more
    // rolls", not "~7 pips".
    if (render_state_[current_player_].dice_restriction_turns != 0) {
        render_state_[current_player_].dice_restriction_turns++;
    }

    // The current_state_==Moving ambient-nag gate only stops NEW ambient clips from starting
    // once Moving ends — a clip already playing when arrival resolves would otherwise keep
    // running into whatever encounter/minigame comes next. Real hardware's single/limited
    // audio stream would have silently cut the ambient clip the instant the encounter's own
    // audio started playing; the port's multi-channel audio model has no such implicit
    // cutoff, so it needs an explicit stop here, at the same real step-arrival teardown
    // point as EndTurn()'s stop.
    if (ambient_nag_wav_ != 0) {
        audio_.stop(ambient_nag_wav_);
        ambient_nag_wav_ = 0;
    }

    // curse_step_counter: while any curse is active (curse_type!=0), incremented once per
    // completed movement action. Only curse_type==1 (Gevaudan) actually reads it (capped
    // at 3); harmless for other modes.
    if (render_state_[current_player_].curse_type != 0) {
        render_state_[current_player_].curse_step_counter++;
    }
}

void BoardGameScreen::ResolveKeyStoneArrival(uint16_t province) {
    // KEY STONE: gated on the shared per-province key pool (province_keys_remaining_,
    // decremented globally as ANY player collects it) — matches the manual's rule that a key
    // stone stops yielding once every copy is collected. Nonzero: decrement + GainKey + KEY
    // avi. Exhausted: NOKEY avi, no GainKey. Either way, suppresses the subsequent
    // enc_type 1-16 roll (same as types 2/3/4).
    const bool got_key = debug_cheat_easy_win_ || province_keys_remaining_[province] > 0;
    if (got_key) {
        if (!debug_cheat_easy_win_) --province_keys_remaining_[province];
        GainKey(current_player_, province);
        stone_arrival_avi_path_ =
            AssetPaths::avi("AV3", std::string("KEY") + std::to_string(province)).string();
    } else {
        stone_arrival_avi_path_ =
            AssetPaths::avi("AV3", std::string("NOKEY") + std::to_string(province)).string();
    }
    current_state_ = BoardGameState::StoneArrivalAVI;
    // Opened right here (not left to StoneArrivalAVI's own 1-frame-later init block) so the
    // in-between frame doesn't show a stale video; state_action_executed_=true on success
    // skips that init block next frame instead of letting it redundantly reopen the same clip.
    video_.close();
    video_loaded_ = false;
    video_.setRenderRect(kBoardAreaX, kBoardAreaY, kBoardAreaW, kBoardAreaH);
    if (video_.open(stone_arrival_avi_path_)) {
        video_.play(true);
        state_action_executed_ = true;
    }
}

void BoardGameScreen::ResolveBholeStoneArrival(uint16_t province) {
    // BHOLE STONE. key_power_flags bit4 SET -> immune: ResolveStepArrival never calls this
    // function at all, falling straight to its own generic roll branch instead (no AVI, no
    // black_hole_retry_counter write). bit4 CLEAR -> this runs: plays bhole AVI + arms
    // black_hole_retry_counter.
    stone_arrival_avi_path_ =
        AssetPaths::avi("AV1", std::string("BHOLE") + std::to_string(province)).string();
    render_state_[current_player_].black_hole_retry_counter = 1;
    current_state_ = BoardGameState::StoneArrivalAVI;
    video_.close();
    video_loaded_ = false;
    video_.setRenderRect(kBoardAreaX, kBoardAreaY, kBoardAreaW, kBoardAreaH);
    if (video_.open(stone_arrival_avi_path_)) {
        video_.play(true);
        state_action_executed_ = true;
    }
}

void BoardGameScreen::ResolveStepArrival(bool skip_stone_type_effects) {
    BeginStepArrival();

    // Determine encounter entry based on landing stone type.
    // Stone types 1 (key) and 2 (bhole) have landing AVIs (StoneArrivalAVI),
    // which always replace the normal enc type 1-16 roll. The no-op cases
    // (key already held / bhole immune) skip StoneArrivalAVI entirely instead
    // and fall through to the normal roll below.
    const uint16_t enc_province = render_state_[current_player_].province;
    const uint16_t enc_stone = render_state_[current_player_].stone_id;
    const MapStone* enc_ms = mapfile_.GetStone(enc_province, enc_stone);
    const uint8_t enc_type = enc_ms ? enc_ms->type : 0;

    if (!skip_stone_type_effects && enc_province < 6 && enc_type == 1 &&
        !render_state_[current_player_].keys[enc_province]) {
        ResolveKeyStoneArrival(enc_province);
        return;
    } else if (!skip_stone_type_effects && enc_province < 6 && enc_type == 2 &&
               !(render_state_[current_player_].key_power_flags & 0x10)) {
        ResolveBholeStoneArrival(enc_province);
        return;
    } else if (!skip_stone_type_effects && enc_province < 6 && enc_type == 3) {
        // FateCard (stone type 3). Unconditional, always negative. Fires directly here and
        // NEVER reaches TriggerEncounter/DispatchEncounter (the enc_type 1-16 roll) for this
        // landing.
        ResolveFateCard(current_player_);
        current_state_ = BoardGameState::TurnEnd;
        state_start_time_ = std::chrono::steady_clock::now();
        state_action_executed_ = false;
    } else if (!skip_stone_type_effects && enc_province < 6 && enc_type == 4) {
        // FortuneCard (stone type 4). Unconditional, always beneficial/neutral. Fires
        // directly here and NEVER reaches the enc_type 1-16 roll for this landing.
        ResolveFortuneCard(current_player_);
        current_state_ = BoardGameState::TurnEnd;
        state_start_time_ = std::chrono::steady_clock::now();
        state_action_executed_ = false;
    } else {
        // The real step-arrival -> stone-type-outcome -> encounter-roll chain is one
        // synchronous function-call cascade with no message-loop turn in between for the
        // "nothing happened" case — so it must resolve fully on the SAME input event, not
        // need a second keypress. Call TriggerEncounter()
        // directly here instead of deferring it to update()'s Encounter case a whole frame
        // later; if it doesn't launch anything (still Encounter afterward — no
        // minigame/AVI/TurnEnd), end the turn immediately ourselves.
        current_state_ = BoardGameState::Encounter;
        state_action_executed_ = true;  // prevent update()'s Encounter case from re-triggering
        TriggerEncounter();
        if (current_state_ == BoardGameState::Encounter) {
            current_state_ = BoardGameState::TurnEnd;
            state_start_time_ = std::chrono::steady_clock::now();
            state_action_executed_ = false;
            return;
        }
    }

    state_start_time_ = std::chrono::steady_clock::now();
    state_action_executed_ = false;
}

void BoardGameScreen::HandleMovementInput(uint8_t zone) {
    // LEFT/RIGHT rotate the travel direction at a junction (delegated to
    // RotateOrReverseDirection); CENTER advances one step. mapfile_direction_ is both the
    // travel direction and the frame-lookup index used after moving.

    GameState* gs = GameStateManager::Instance().Get();
    if (!gs) return;

    // Input-blocking while portal_blocked_wav_ (WAV/STOP.WAV) plays is handled at the
    // Moving-state level (the movement_available/break gate, same pattern as
    // portrait_.curse_blindness_wav) — this function isn't even called while it's active.

    switch (zone) {
        case 0:  // LEFT zone — CCW direction rotate at junctions
            RotateOrReverseDirection(/*clockwise=*/false);
            break;

        case 1:  // RIGHT zone — CW direction rotate at junctions
            RotateOrReverseDirection(/*clockwise=*/true);
            break;

        case 2: {  // CENTER advance
            if (render_state_[current_player_].steps_remaining > 0) {
                const uint16_t province = gs->player_idx;
                const uint16_t stone_id = gs->sub_idx;
                const MapStone* stone = mapfile_.GetStone(province, stone_id);
                if (!stone) break;

                // The real step-validation chain runs gate-entry / tunnel-entry / tunnel-exit /
                // toboggan-slide, then the win-all-keys check (province-6 central portal) —
                // all FIRST, all reading dest[dir] on the raw/untouched travel direction. The
                // "scan for another direction" fallback only runs once every one of those says
                // "not applicable", matching the real call order. Each check now lives in its
                // own method (Handle*/Advance*); the gating condition stays here.
                const uint8_t dir_raw = mapfile_direction_;
                const int16_t dest_raw = mapfile_.GetDest(province, stone_id, dir_raw);

                if (dest_raw == kDestGate) {
                    HandlePortalCrossing(province, stone_id);
                    break;
                }
                if (dest_raw == kDestWall) {
                    HandleWallDestination(province, stone_id);
                    break;
                }
                if (province == 5 && stone_id == 63 && dest_raw == 64) {
                    HandleTobogganSlide();
                    break;
                }
                if (HandleCentralPortalWin(province, stone_id, dest_raw)) break;

                AdvanceNormalStep(province, stone_id, dir_raw);
            }
            break;
        }
    }
}

void BoardGameScreen::RotateOrReverseDirection(bool clockwise) {
    // Junction stones (connections>2) always rotate. Non-junction stones gate on steps
    // already taken this roll, not a boolean: 0 (right after rolling, before any step)
    // scans in the rotate direction for the stone's one other connection — effectively a
    // 180-degree reversal, matching the manual's rule that a reversal is only allowed
    // right after rolling; any other value refuses.
    GameState* gs = GameStateManager::Instance().Get();
    if (!gs) return;

    const uint16_t province = gs->player_idx;
    const uint16_t stone_id = gs->sub_idx;
    const MapStone* stone = mapfile_.GetStone(province, stone_id);
    const uint16_t steps_taken = render_state_[current_player_].steps_total_this_roll -
                                 render_state_[current_player_].steps_remaining;

    const auto step_dir = [clockwise](uint8_t dir) -> uint8_t {
        return clockwise ? (dir + 1) % 4 : (dir == 0 ? 3 : dir - 1);
    };

    if (stone && stone->connections > 2) {
        mapfile_direction_ = step_dir(mapfile_direction_);
        if (portrait_.turn_wav) audio_.playLoaded(portrait_.turn_wav, AudioFlags::Async);
        // A rotate is a real resolved action just like a step, so it must re-arm
        // MovementCooldownElapsed()'s gate too (the same re-enable-after-delay call fires
        // after every real completion, not just forward steps).
        last_move_time_ = std::chrono::steady_clock::now();
    } else if (stone && steps_taken == 0) {
        // Dead-end/straight stone, 0 steps taken yet this roll — search for the one other
        // valid (non -1) direction, same loop as the real ASM.
        uint8_t dir = mapfile_direction_;
        int guard = 0;
        do {
            dir = step_dir(dir);
        } while (stone->dest[dir] == -1 && ++guard < 4);
        mapfile_direction_ = dir;
        if (portrait_.turn_wav) audio_.playLoaded(portrait_.turn_wav, AudioFlags::Async);
        last_move_time_ = std::chrono::steady_clock::now();
    } else {
        // Straight stone, >=1 step already taken — real ASM refuses (returns 0). Skip
        // instead of stacking while the last insult is still playing.
        if (!audio_.isPlaying(movement_blocked_wav_)) {
            movement_blocked_wav_ =
                playWav(kMovementBlockedSounds[RngNext() % 5], AudioFlags::Async);
        }
    }
}

void BoardGameScreen::HandlePortalCrossing(uint16_t province, uint16_t stone_id) {
    // Eligibility: province==6 (hub) OR player has province key.
    const bool eligible = (province == 6) || render_state_[current_player_].keys[province];
    if (!eligible) {
        // stop(0) halts every currently playing sound (matches the real
        // single/limited waveOut stream model). Input-blocking for the WAV's
        // duration is handled at the Moving-state level (movement_available/
        // break gate, same pattern as portrait_.curse_blindness_wav).
        audio_.stop(0);
        audio_.playLoaded(portal_blocked_wav_, AudioFlags::Async);
        return;
    }

    // Find portal table entry matching current (stone, province).
    int door_index = -1;
    for (int i = 0; i < 24; ++i) {
        if (kPortalTable[i].src_stone == stone_id && kPortalTable[i].src_prov == province) {
            door_index = i;
            break;
        }
    }
    if (door_index < 0) {
        return;
    }

    // door_index%3==1 entries require ALL 6 province keys.
    if (door_index % 3 == 1) {
        int key_count = 0;
        for (int p = 0; p < 6; ++p) key_count += render_state_[current_player_].keys[p] ? 1 : 0;
        if (key_count < 6) {
            audio_.stop(0);
            audio_.playLoaded(portal_blocked_wav_, AudioFlags::Async);
            return;
        }
    }

    // Teleport player to destination province/stone via DOOR{door_index}.AVI — same
    // "queue TeleportAVI, caller applies dest after" convention already used
    // for Flight/GateOpen/gatekeeper FEAR. steps_remaining decrements
    // unconditionally right after this, regardless of outcome.
    const uint16_t dst_prov = kPortalTable[door_index].dst_prov;
    const uint16_t dst_stone = kPortalTable[door_index].dst_stone;

    // Post-teleport direction override table: of the 16 portal-table entries
    // (door_index 1-16), only door_index in {1,4,7,10,13,16} (the door_index%3==1
    // "need all 6 keys" group) carry an explicit re-orientation; the rest carry
    // direction over unchanged from before the crossing. A SEPARATE unconditional
    // override follows: if the SOURCE province == 6, direction is forced to South
    // regardless of the table, always facing South leaving the hub.
    {
        static constexpr uint8_t kGateDirTable[16] = {
            /*door_index=1*/ 0,  /*door_index=2*/ 4,
            /*door_index=3*/ 4,  /*door_index=4*/ 1,
            /*door_index=5*/ 4,  /*door_index=6*/ 4,
            /*door_index=7*/ 3,  /*door_index=8*/ 4,
            /*door_index=9*/ 4,  /*door_index=10*/ 3,
            /*door_index=11*/ 4, /*door_index=12*/ 4,
            /*door_index=13*/ 2, /*door_index=14*/ 4,
            /*door_index=15*/ 4, /*door_index=16*/ 1,
        };  // 4 = sentinel "no override"
        // Staged into teleport_.dest_direction, not applied to mapfile_direction_ yet —
        // doing that now would leak the post-arrival direction into the TeleportVideoPreload
        // hold frame, which still shows the OLD province/stone.
        int dest_direction = mapfile_direction_;
        if (door_index >= 1 && door_index <= 16 && kGateDirTable[door_index - 1] != 4) {
            dest_direction = kGateDirTable[door_index - 1];
        }
        if (province == 6) {
            dest_direction = 2;  // South — forced when leaving the hub
        }
        teleport_.dest_direction = dest_direction;
    }

    render_state_[current_player_].steps_remaining--;
    last_move_time_ = std::chrono::steady_clock::now();
    timeout_warn_played_ = false;

    const char* door_folder = (door_index >= 7 && door_index <= 9) ? "AV2" : "AV1";
    teleport_.path =
        AssetPaths::avi(door_folder, std::string("DOOR") + std::to_string(door_index)).string();
    teleport_.dest_stone = dst_stone;
    teleport_.dest_prov = dst_prov;
    teleport_.kind = TeleportAviState::Kind::MidRollContinue;
    // Route through TeleportVideoPreload (port-only, no ASM equivalent) instead of opening
    // the door AVI directly here — it holds the already-rendered board on screen through
    // the video decoder's blocking open call, so its blank first frame never flashes on top
    // of it.
    current_state_ = BoardGameState::TeleportVideoPreload;
    state_start_time_ = std::chrono::steady_clock::now();
    state_action_executed_ = false;
    return;
}

void BoardGameScreen::HandleWallDestination(uint16_t province, uint16_t stone_id) {
    // Province-indexed: the tunnel (province 7) stone where each province's entrance sits.
    // Indexed directly on entry; searched by stone_id on exit.
    static constexpr uint16_t kTunnelEntryStoneTable[6] = {25, 49, 0, 13, 61, 37};

    // AVI name tables (province-indexed): "dn" = entering the province-7 tunnels from
    // province X, "up" = exiting the tunnels back to province X. Both directions' clips live
    // in the same AV folder per province, hence one shared folder table.
    static const char* kTunnelAviFolders[6] = {"AV2", "AV3", "AV1", "AV1", "AV3", "AV2"};
    static const char* kTunnelDnAviNames[6] = {"GAVBNDN", "KHUFUDN", "BARONDN",
                                               "CNTSSDN", "WITCHDN", "HELLINDN"};
    static const char* kTunnelUpAviNames[6] = {"GAVBNUP", "KHUFUUP", "BARONUP",
                                               "CNTSSUP", "WITCHUP", "HELLINUP"};

    const MapStone* cur_ms = mapfile_.GetStone(province, stone_id);
    const uint8_t cur_type = cur_ms ? cur_ms->type : 0;

    // Province 7 (tunnels), type-5, wall -> EXIT tunnels, "up" AVI. Dest:
    // dest_province, stone 0. Direction: dest_province==1 -> South(2), else North(0).
    if (province == 7 && cur_type == 5) {
        int dest_province = -1;
        for (int i = 0; i < 6; ++i) {
            if (kTunnelEntryStoneTable[i] == stone_id) {
                dest_province = i;
                break;
            }
        }
        if (dest_province >= 0) {
            // steps_remaining decrements by 1 and the transition fires immediately on this
            // same click (unlike a normal step, which waits for Space to confirm arrival) —
            // no move-sound, just the video load/draw plus an optional black-flash. Must use
            // MidRollContinue here, not RollEnding — that resumes Moving keeping
            // steps_remaining as decremented, while RollEnding would silently reset it back
            // to the full roll.
            render_state_[current_player_].steps_remaining--;
            last_move_time_ = std::chrono::steady_clock::now();
            timeout_warn_played_ = false;

            cursor_state_.is_animating = false;
            teleport_.kind = TeleportAviState::Kind::MidRollContinue;
            teleport_.path =
                AssetPaths::avi(kTunnelAviFolders[dest_province], kTunnelUpAviNames[dest_province])
                    .string();
            teleport_.dest_prov = dest_province;
            teleport_.dest_stone = 0;
            // Staged, not applied to mapfile_direction_ yet — avoids leaking the new
            // direction into the TeleportVideoPreload hold frame that still shows the old
            // stone.
            teleport_.dest_direction = (dest_province == 1) ? 2 : 0;
            current_state_ = BoardGameState::TeleportVideoPreload;
            state_start_time_ = std::chrono::steady_clock::now();
            state_action_executed_ = false;
            return;
        }
    }

    // prov 0-6, type-6, wall -> ENTER tunnels (prov 7), "dn" AVI. Eligibility:
    // key_power_flags bit0 — always the player's OWN home province's key
    // (char0->prov0, char1->prov1, ... char5->prov5): you need your own home
    // province's key to enter the tunnels at all.
    if (province < 7 && cur_type == 6) {
        if (!(render_state_[current_player_].key_power_flags & 0x01)) {
            audio_.stop(0);
            audio_.playLoaded(portal_blocked_wav_, AudioFlags::Async);
            return;
        }
        // Fires immediately on this same click, no move-sound — just the video load/draw
        // plus an optional black-flash.
        render_state_[current_player_].steps_remaining--;
        last_move_time_ = std::chrono::steady_clock::now();
        timeout_warn_played_ = false;

        cursor_state_.is_animating = false;
        teleport_.kind = TeleportAviState::Kind::MidRollContinue;
        teleport_.path =
            AssetPaths::avi(kTunnelAviFolders[province], kTunnelDnAviNames[province]).string();
        teleport_.dest_prov = 7;
        teleport_.dest_stone = kTunnelEntryStoneTable[province];

        // Explicit scan for the destination stone's first dest>=0 direction (no self-loop
        // exclusion, unlike the corner-rotate scan). Can't rely on AutoOrientDirection()'s
        // frame-validity check here — every tunnel stone's wall direction has real wall art,
        // so that check alone would leave the player facing the wall they just walked
        // through.
        {
            const MapStone* tdst = mapfile_.GetStone(7, kTunnelEntryStoneTable[province]);
            if (tdst) {
                for (int d = 0; d < 4; ++d) {
                    if (tdst->dest[d] >= 0) {
                        teleport_.dest_direction = d;
                        break;
                    }
                }
            }
        }

        current_state_ = BoardGameState::TeleportVideoPreload;
        state_start_time_ = std::chrono::steady_clock::now();
        state_action_executed_ = false;
        return;
    }

    // Not a handled wall case -> blocked
    return;
}

void BoardGameScreen::HandleTobogganSlide() {
    // Consumes 1 step immediately, on this same click — the step-counter box stays visible
    // through the slide AVI (teleport_.shows_counter), same resume-without-re-init pattern
    // as the tunnel teleports above (MidRollContinue).
    render_state_[current_player_].steps_remaining--;
    last_move_time_ = std::chrono::steady_clock::now();
    timeout_warn_played_ = false;
    cursor_state_.is_animating = false;
    teleport_.kind = TeleportAviState::Kind::MidRollContinue;
    teleport_.shows_counter = true;
    teleport_.path = AssetPaths::avi("AV3", "HSLIDE").string();
    teleport_.dest_stone = 25;
    teleport_.dest_prov = -1;
    current_state_ = BoardGameState::TeleportVideoPreload;
    state_start_time_ = std::chrono::steady_clock::now();
    state_action_executed_ = false;
    return;
}

bool BoardGameScreen::HandleCentralPortalWin(uint16_t province, uint16_t stone_id,
                                             int16_t dest_raw) {
    if (province != 6) return false;

    const char* central_avi = nullptr;
    if (stone_id == 15 && dest_raw == 16)
        central_avi = "CENTRAL1";
    else if (stone_id == 29 && dest_raw == 30)
        central_avi = "CENTRAL2";
    else if (stone_id == 5 && dest_raw == 4)
        central_avi = "CENTRAL3";

    if (central_avi) {
        const int key_count = CountHeldKeys(current_player_);

        if (key_count >= 6) {
            render_state_[current_player_].win_flag = 1;
            teleport_.path = AssetPaths::avi("AV1", central_avi).string();
            // The real win-all-keys check doesn't set a destination at all — it only sets
            // win_flag and returns. Province 7 stone 0 here is an arbitrary placeholder: no
            // screen ever reads this position again once win_flag fires.
            teleport_.dest_prov = 7;
            teleport_.dest_stone = 0;
            teleport_.kind = TeleportAviState::Kind::RollEnding;
            teleport_.shows_counter = false;
            teleport_.post_state = BoardGameState::TurnEnd;
            current_state_ = BoardGameState::TeleportVideoPreload;
            state_start_time_ = std::chrono::steady_clock::now();
            state_action_executed_ = false;
            return true;
        }
        // keys < 6 -> step refused, no move.
        return true;
    }
    return false;
}

void BoardGameScreen::AdvanceNormalStep(uint16_t province, uint16_t stone_id, uint8_t dir_raw) {
    const MapStone* stone = mapfile_.GetStone(province, stone_id);
    GameState* gs = GameStateManager::Instance().Get();
    if (!stone || !gs) return;

    uint8_t dir = dir_raw;
    if (stone->connections == 2) {
        if (mapfile_.GetDest(province, stone_id, dir) < 0) {
            // Current direction hit a wall: find the turn direction
            dir = 4;  // sentinel = not found
            for (int d = 0; d < 4; ++d) {
                int16_t candidate = mapfile_.GetDest(province, stone_id, (uint8_t)d);
                if (candidate < 0) continue;                    // wall/none
                if ((uint16_t)candidate == stone_id) continue;  // self-loop
                dir = (uint8_t)d;
                break;
            }
            if (dir >= 4) {
                if (!audio_.isPlaying(movement_blocked_wav_)) {
                    movement_blocked_wav_ =
                        playWav(kMovementBlockedSounds[RngNext() % 5], AudioFlags::Async);
                }
                return;
            }
        }
        // else: dest[dir] >= 0 -> keep dir unchanged (straight ahead)
    }
    // else: conn != 2 -> use dir_raw as set by junction steering

    int16_t dest = mapfile_.GetDest(province, stone_id, dir);
    if (dest < 0) {
        return;
    }

    gs->sub_idx = static_cast<uint16_t>(dest);
    render_state_[current_player_].stone_id = static_cast<uint16_t>(dest);
    mapfile_direction_ = dir;  // travel direction (held for next step)

    // Corner auto-rotate: after arriving at new stone, if dest[dir] is invalid in
    // the new stone, scan for the correct exit direction: try_dir = (dir +
    // counter) % 4, skip if no connection, skip if it goes back to the old stone.
    // The found direction becomes the zone_idx for frame lookup.
    {
        const MapStone* new_stone = mapfile_.GetStone(province, static_cast<uint16_t>(dest));
        if (new_stone && new_stone->dest[dir] == kDestNone) {
            for (int counter = 0; counter < 4; ++counter) {
                uint8_t td = static_cast<uint8_t>((dir + counter) % 4);
                int16_t cand = new_stone->dest[td];
                if (cand == kDestNone) continue;           // no connection
                if ((uint16_t)cand == stone_id) continue;  // going back to origin
                mapfile_direction_ = td;
                break;
            }
        }
    }

    // Play MOVE sound for current player's character.
    if (portrait_.move_wav) {
        audio_.playLoaded(portrait_.move_wav, AudioFlags::Async);
    }

    // Decrement remaining steps; reset idle timer on each advance
    render_state_[current_player_].steps_remaining--;
    last_move_time_ = std::chrono::steady_clock::now();
    timeout_warn_played_ = false;
}

void BoardGameScreen::AutoOrientDirection(uint16_t province, uint16_t stone_id) {
    // Only acts when current direction has no valid frame. If current direction already has
    // a valid frame, keep it — no change.
    uint16_t cur_frame = mapfile_.GetFrame(province, stone_id, mapfile_direction_);
    if (cur_frame != kFrameInvalid) {
        return;  // Current direction has a valid frame — nothing to do
    }

    // Current direction has no frame — find first valid direction (0-3)
    for (uint8_t d = 0; d < 4; ++d) {
        uint16_t f = mapfile_.GetFrame(province, stone_id, d);
        if (f != kFrameInvalid) {
            mapfile_direction_ = d;
            cursor_state_.direction = (d == 3) ? 0 : (d == 1) ? 1 : 2;
            return;
        }
    }
}

void BoardGameScreen::RollDiceForOption(int option) {
    // Rolls exactly the die/dice for the clicked option (1 or 2) — the RNG call count and
    // order here matches the real sequence exactly. Eligibility (dice_restricted, even_dice,
    // curse_type) is already checked by the caller before this runs.
    //
    // option==1: single RNG call -> dice_.result (die1).
    // option==2: two RNG calls -> dice_.result2/result3 (die2+die3, summed).
    const bool even_only = render_state_[current_player_].even_dice;

    auto roll_one = [this, even_only]() -> uint16_t {
        uint16_t r = RngNext();
        if (even_only) {
            // Encounter types 5+6: only even results {2,4,6}.
            return static_cast<uint16_t>((static_cast<uint32_t>(r) * 3u / 32768u) * 2u + 2u);
        }
        return static_cast<uint16_t>(((static_cast<uint32_t>(r) * 6) / 32768) + 1);
    };

    if (option == 1) {
        dice_.result = roll_one();
        dice_.move_distance = dice_.result;
        // The even-only restriction only clears once the roll actually lands on 6 — it
        // PERSISTS across turns otherwise, matching the "roll a 6 to play/move again"
        // voice line this restriction is tied to.
        if (dice_.result == 6) {
            render_state_[current_player_].even_dice = 0;
        }
    } else {
        // 2-dice option requires even_dice==0 as an eligibility precondition (checked by the
        // caller before RollDiceForOption is invoked) — nothing to clear.
        if (force_double_six_roll_) {
            dice_.result2 = 6;
            dice_.result3 = 6;
            force_double_six_roll_ = false;
        } else {
            dice_.result2 = roll_one();
            dice_.result3 = roll_one();
        }
        dice_.move_distance = dice_.result2 + dice_.result3;

        // "The Chosen One" (double sixes): when a 2-dice roll sums to 12, sets chosen_one_flag=1
        // PERMANENTLY (never cleared anywhere in the program). Consumed by enc_type 2/4/6 to
        // pick a fixed insult string instead of the random pool.
        if (dice_.move_distance == 12) {
            render_state_[current_player_].chosen_one_flag = 1;
        }
    }

    // Anne/toad curse (curse_type==2) final override: result is forced to 1 regardless of
    // what was actually rolled. The Moving-state dice click handler already blocks picking
    // the 2-dice option while cursed, so option==2 shouldn't reach here under this curse —
    // but the override stays unconditional, matching the real placement.
    if (render_state_[current_player_].curse_type == 2) {
        dice_.move_distance = 1;
    }

    // Audio: Handled in state machine (not here)
    //   - ROLLING: Started in RollingDice state entry
    //   - ROLLED: Started when player clicks dice
}

}  // namespace openfear::game
