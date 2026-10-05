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

void BoardGameScreen::render() {
    // No-op on backends with nothing to crop (see IRenderer::SafeAreaAnchor).
    // Bottom only for states that share the board's own fullmap background
    // (protects the key/step-counter row and, for ShowingPortrait, the
    // portrait's own bottom UI) — everything else (dedicated minigame
    // screens, AVIs, result screens) uses the default Center. Checked
    // every frame instead of at each of the ~50 current_state_ assignment
    // sites: cheap (int write, no hardware touched), and avoids relying on
    // every future state transition remembering to set it.
    switch (current_state_) {
        case BoardGameState::ShowingPortrait:
        case BoardGameState::WaitingForPlayer:
        case BoardGameState::RollingDice:
        case BoardGameState::Moving:
        case BoardGameState::TurnEnd:
        case BoardGameState::BlackFlash:
        case BoardGameState::MinimapScreen:
        case BoardGameState::HomeAVI:
        case BoardGameState::TurnTimeoutWait:
            renderer_.setSafeAreaAnchor(IRenderer::SafeAreaAnchor::Bottom);
            break;
        default:
            renderer_.setSafeAreaAnchor(IRenderer::SafeAreaAnchor::Center);
            break;
    }

    // Cursor set: derived from current mouse zone (visual hint of what clicking will do).
    // current_mouse_zone_ (0=LEFT, 1=RIGHT, 2=CENTER, same numbering GetMouseZone() returns)
    // maps directly to the matching cursor SET index.
    // Animation frame: table maps animation_frame(0-6) -> frame(0,1,0,1,0,1,2).
    // Deliberately runs before every early `return` below (Pause, AVIs,
    // minigames, DuelAttackGauge, TurnEnd, ...) instead of further down - states
    // that hit one of those returns still need the cursor set, otherwise it
    // stays frozen on whatever LEFT/RIGHT movement-zone sprite was showing
    // right before switching state.
    static const uint8_t kAnimFrameTable[7] = {0, 1, 0, 1, 0, 1, 2};

    // Default (SET 0, frame 2) — same baseline every earlier screen already shows,
    // since none of them call setGameCursorState() at all. A frame here that isn't
    // yet in Moving state (the only branch that overrides cursor_set) falls back to
    // this instead of popping to something else at the hand-off.
    int cursor_set = 0;
    if (current_state_ == BoardGameState::Moving) {
        switch (current_mouse_zone_) {
            case 0:
                cursor_set = 0;
                break;  // LEFT zone   -> SET 0
            case 1:
                cursor_set = 1;
                break;  // RIGHT zone  -> SET 1
            case 2:
                cursor_set = 2;
                break;  // CENTER zone -> SET 2
        }
    }
    int cursor_frame =
        cursor_state_.is_animating ? kAnimFrameTable[cursor_state_.animation_frame % 7] : 2;

    renderer_.setGameCursorState(cursor_set, cursor_frame);

    // During ShowingPortrait: render portrait fullscreen, skip board/dice.
    if (current_state_ == BoardGameState::ShowingPortrait) {
        RenderShowingPortrait();
        return;
    }

    // TurnEnd: draw black here too (matching the real BitBlt-BLACKNESS-
    // before-portrait sequence), EXCEPT while a FateCard/FortuneCard
    // overlay is still active (TurnEnd is deliberately held for multiple
    // frames in that case, and the card's own draw block below still
    // needs to run).
    if (current_state_ == BoardGameState::TurnEnd && !card_active_) {
        renderer_.fillRect(0, 0, 640, 480, 0, 0, 0);
        return;
    }

    // GameOver never needs to draw anything — nothing downstream reads
    // this screen's render() output again.
    if (current_state_ == BoardGameState::GameOver) {
        return;
    }

    // During TeleportAVI / StoneArrivalAVI: render AVI in the board area, skip board/dice overlay.
    if (current_state_ == BoardGameState::TeleportAVI) {
        video_.renderFrame();
        // Real teleport AVIs (tunnel enter/exit, central portal) hide the
        // counter like any other fullscreen AVI. The toboggan is the one
        // exception — the counter box stays visible during HSLIDE.AVI.
        if (teleport_.shows_counter) {
            DrawStepCounter(render_state_[current_player_].steps_remaining);
        }
        return;
    }
    if (current_state_ == BoardGameState::StoneArrivalAVI) {
        video_.renderFrame();
        return;
    }
    if (current_state_ == BoardGameState::GkLoopAVI) {
        video_.renderFrame();
        return;
    }

    // OneDieGame: OneDie (enc type 9) full-screen render.
    if (current_state_ == BoardGameState::OneDieGame) {
        onedie_screen_.render();
        return;
    }

    // DiceKeysGame: DiceKeys (enc type 8) full-screen render.
    if (current_state_ == BoardGameState::DiceKeysGame) {
        dicekeys_screen_.render();
        return;
    }

    // DoorsGame: Doors (enc type 7) full-screen render.
    if (current_state_ == BoardGameState::DoorsGame) {
        doors_screen_.render();
        return;
    }

    // BlackjackGame: dealer-first BJ (enc type 13).
    if (current_state_ == BoardGameState::BlackjackGame) {
        blackjack_screen_.render();
        return;
    }

    // BlackFlash: solid black for one frame before ShowingPortrait/
    // GkLoopAVI (normal flow), or before TeleportAVI only for the
    // Gatekeeper win-challenge edge case.
    if (current_state_ == BoardGameState::BlackFlash) {
        renderer_.fillRect(0, 0, 640, 480, 0, 0, 0);
        return;
    }

    // MinimapScreen: province overview map + player position marker.
    // Marker: 22x22px hollow rectangle centered on stone pixel.
    // Border (outside->in): 1px orange, 2px red, 1px orange. Interior
    // (14x14) is transparent. Middle ring blinks red<->orange every 200ms.
    if (current_state_ == BoardGameState::MinimapScreen) {
        minimap_screen_.render();
        return;
    }

    if (current_state_ == BoardGameState::HomeAVI) {
        video_.renderFrame();
        return;
    }

    // TurnTimeoutWait: keeps presenting the video's last decoded frame, no seek —
    // avoids visibly snapping the board back to the reverted position mid-audio.
    if (current_state_ == BoardGameState::TurnTimeoutWait) {
        if (video_loaded_) video_.renderFrame();
        return;
    }

    // CoffinChoiceGame render: closed-coffin bitmap (enc type 11) during the choice
    // phase, swaps to the outcome-specific image once resolved.
    if (current_state_ == BoardGameState::CoffinChoiceGame) {
        coffin_screen_.render();
        return;
    }

    // BribeGame render: bitmap of the key at stake (m1\bribes{province}.bmp) + a single
    // right-half click zone (rect (320,0)-(640,480)).
    if (current_state_ == BoardGameState::BribeGame) {
        bribe_screen_.render();
        return;
    }

    // SlotMachineGame render: 3 reels with suit symbols.
    if (current_state_ == BoardGameState::SlotMachineGame) {
        slot_screen_.render();
        return;
    }

    // FortuneDoorGame render: door bitmap + OK/X button labels.
    if (current_state_ == BoardGameState::FortuneDoorGame) {
        fortune_door_screen_.render();
        return;
    }

    // DuelChooseOpponent: interactive duel target picker — 6-icon grid (symbolic sigils,
    // not per-character face portraits).
    if (current_state_ == BoardGameState::DuelChooseOpponent) {
        duel_choose_screen_.render();
        return;
    }

    // DuelAttackGauge: Duel battle — ATTACKB.BMP background + two power gauges.
    if (current_state_ == BoardGameState::DuelAttackGauge) {
        duel_attack_gauge_.render();
        return;
    }

    // DuelStealOrCurse: winner's post-gauge choice. GETKEYS.BIN's zone 6
    // (the curse-card-shaped region) IS the real always-clickable curse
    // choice, regardless of stealable state. Stealable-key highlight reuses
    // the same ATTACKC.BMP key-highlight overlay DuelAttackGauge already loaded.
    if (current_state_ == BoardGameState::DuelStealOrCurse) {
        duel_steal_or_curse_screen_.render();
        return;
    }

    if (current_state_ == BoardGameState::PauseScreen) {
        pause_screen_.render();
        return;
    }

    // FlightMap: province map (M1/FLIGHT.BMP) + 6 clickable zones while choosing, then the
    // chosen FLIGHT{n}.AVI fullscreen once a province is picked.
    if (current_state_ == BoardGameState::FlightMap) {
        flight_map_screen_.render();
        return;
    }

    if (current_state_ == BoardGameState::CureCurseGame) {
        curecurse_screen_.render();
        return;
    }

    // Elapsed time since entering Moving/Encounter, compared against kDiceGraceMs below —
    // gates the Khufu curse substitution and the dice/counter swap in this function.
    long long ms_in_moving =
        (current_state_ == BoardGameState::Moving || current_state_ == BoardGameState::Encounter)
            ? std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - state_start_time_)
                  .count()
            : 9999LL;

    // Layer 1: FULLMAP.AVI background — seek to correct frame via MAPFILE lookup
    //
    // Khufu curse (curse_type==3): shows the curse bitmap (C1/K_CURSE.BMP) instead of the
    // normal counter, from the dice->counter swap onward for the rest of the walk.
    //
    // Also covers Encounter: once all steps are taken, ResolveStepArrival() transitions
    // current_state_ to the generic Encounter state (the enc type 1-16 roll), which has no
    // dedicated render() block of its own and falls through to this same default board-drawing
    // path — for a still-cursed player that would let one frame of fullmap sneak in between
    // the last cursed Moving frame and whatever Encounter dispatches to next.
    if ((current_state_ == BoardGameState::Moving || current_state_ == BoardGameState::Encounter) &&
        ms_in_moving >= kDiceGraceMs && render_state_[current_player_].curse_type == 3) {
        if (portrait_.curse_indicator_tex) {
            renderer_.drawBitmap(portrait_.curse_indicator_tex, 0, 0, 0, 0, 640, 480);
        }
    } else if (current_state_ == BoardGameState::KeyAwardGame) {
        // KeyAward (enc type 15) — video_ still holds the GKLOOP intro clip here, NOT
        // FULLMAP.AVI, so the generic province/stone/direction seekFrame logic below would
        // seek into the wrong clip. renderFrame() alone is safe regardless: once the clip
        // naturally stops, it just keeps redrawing its last frame, per IVideoPlayer's own
        // contract. The one-time "make sure a valid texture exists"
        // reopen (for the rare case the video was already torn down the instant we arrived
        // here) lives in this state's entry init, not here, so it only runs once per state
        // entry instead of every frame.
        video_.renderFrame();
    } else if (video_loaded_) {
        GameState* gs = GameStateManager::Instance().Get();
        if (gs) {
            const uint16_t province = gs->player_idx;
            const uint16_t stone_id = gs->sub_idx;
            const uint8_t direction = mapfile_direction_;  // MAPFILE direction: 0=N,1=E,2=S,3=W

            int target_frame = -1;
            uint16_t frame = mapfile_.GetFrame(province, stone_id, direction);
            if (frame != kFrameInvalid) {
                target_frame = static_cast<int>(frame);
            }
            // On kFrameInvalid: keep last_video_frame_ as-is, do NOT seek, do NOT
            // fallback to another direction.

            // Only seek if frame changed (avoid redundant decoding)
            if (target_frame >= 0 && target_frame != last_video_frame_) {
                video_.seekFrame(target_frame);
                last_video_frame_ = target_frame;
            }
        }

        // Render the current video frame
        video_.renderFrame();
    }

    // ===== MOVEMENT COUNTER + DICE VISIBILITY =====
    // Dice and counter are mutually exclusive during Moving entry.

    // Counter: appears after the grace period; stays during Encounter. During
    // TeleportVideoPreload, matches whatever visibility teleport_.shows_counter says the
    // AVI itself will have once it opens.
    bool counter_visible =
        (current_state_ == BoardGameState::Moving && ms_in_moving >= kDiceGraceMs) ||
        current_state_ == BoardGameState::Encounter ||
        (current_state_ == BoardGameState::TeleportVideoPreload && teleport_.shows_counter);
    if (counter_visible) {
        DrawStepCounter(render_state_[current_player_].steps_remaining);
    }

    // Dice overlay (MVDICE.BMP)
    bool dice_visible = (current_state_ == BoardGameState::RollingDice) ||
                        (current_state_ == BoardGameState::Moving && ms_in_moving < kDiceGraceMs);

    if (dice_tex_ && dice_visible) {
        // MVDICE.BMP — 600x151 px, 6 face columns x 100 px each.
        // Top row (src_y=0, h=75): 6 static face images (dice stopped).
        // Bottom row (src_y=76, h=74): 6 spinning animation frames (dice spinning).
        // Column N: src_x = N*100+1 (N=0..5). Screen X: Die1=45, Die2=430, Die3=518, Y=403.
        auto face_src_x = [](int val) -> int {
            int v = (val >= 1 && val <= 6) ? val : 6;
            return v * 100 - 99;
        };
        // Die1 stopped: top row, width=99.
        auto draw_stopped_d1 = [&](int screen_x, int val) {
            renderer_.drawBitmap(dice_tex_, screen_x, 403, face_src_x(val), 0, 99, 75);
        };
        // Die2/3 stopped: top row, width=95.
        auto draw_stopped_d23 = [&](int screen_x, int val) {
            renderer_.drawBitmap(dice_tex_, screen_x, 403, face_src_x(val), 0, 95, 75);
        };
        // Spinning: bottom row, width=99.
        auto draw_spinning = [&](int screen_x, int frame_idx) {
            renderer_.drawBitmap(dice_tex_, screen_x, 403, frame_idx * 100 + 1, 76, 99, 74);
        };

        bool spinning = (dice_.anim_counter < 10000);

        if (spinning) {
            // frame_idx = (counter/4) % 6, src_x = frame_idx*100+1. Die3 one frame ahead, wraps
            // at 6.
            int frame_idx = (dice_.anim_counter / 4) % 6;
            int frame_idx3 = (frame_idx + 1) % 6;
            draw_spinning(45, frame_idx);

            // Die2 and Die3 rendered only when:
            //   dice_restricted==2 (normal, not forced 1-die) AND even_dice==0 AND
            //   curse_type!=2 (Anne/frog curse forces 1-die too).
            const auto& rs_anim = render_state_[current_player_];
            bool show_extra_dice = (rs_anim.dice_restricted == 2) && (rs_anim.even_dice == 0) &&
                                   (rs_anim.curse_type != 2);
            if (show_extra_dice) {
                draw_spinning(430, frame_idx);
                draw_spinning(518, frame_idx3);
            }

            // Gamepad focus indicator (dice-count choice while both options are still
            // spinning): the focused die stays exactly as already drawn above (untouched),
            // the non-focused one is redrawn once at a fixed dim tint on top — reads as
            // "greyed out / not the current choice" rather than a blinking cue.
            if (gamepad_mode_ && !dice_.result_locked) {
                constexpr uint8_t kDimV = 110;  // fixed, no animation
                if (dice_.focus == 0 && show_extra_dice) {
                    renderer_.drawSpriteTinted(dice_sprite_, 430, 403, frame_idx * 100 + 1, 76, 99,
                                               74, kDimV, kDimV, kDimV);
                    renderer_.drawSpriteTinted(dice_sprite_, 518, 403, frame_idx3 * 100 + 1, 76, 99,
                                               74, kDimV, kDimV, kDimV);
                } else if (dice_.focus == 1 && show_extra_dice) {
                    renderer_.drawSpriteTinted(dice_sprite_, 45, 403, frame_idx * 100 + 1, 76, 99,
                                               74, kDimV, kDimV, kDimV);
                }
            }
        } else {
            // dice_.selected_option==1 (1-die mode): die1 rendered at X=45.
            // dice_.selected_option!=1 (2-dice mode): ONLY die2+die3 rendered at X=430/518,
            // die1 is NOT rendered.
            if (dice_.selected_option == 1) {
                draw_stopped_d1(45, dice_.result);
            } else if (dice_.selected_option == 2) {
                draw_stopped_d23(430, dice_.result2);  // die2 at X=430
                draw_stopped_d23(518, dice_.result3);  // die3 at X=518
            }
        }
    }

    // KeyAward (enc type 15) — real dice overlay, drawn over the GKLOOP intro clip already
    // rendered above by this state's own branch (no custom background here).
    if (dice_tex_ && current_state_ == BoardGameState::KeyAwardGame) {
        keyaward_screen_.render();
    }

    // Fortune card overlay — only draws while card_active_, never mutates state; the
    // audio-done check that clears it lives in update()'s TurnEnd case.
    if (card_active_ && card_tex_) {
        renderer_.drawBitmap(card_tex_, 0, 0, 0, 0, 640, 480);
    }
}

void BoardGameScreen::RenderShowingPortrait() {
    // BlackFlash sets current_state_=ShowingPortrait and returns, but ShowingPortrait's
    // own `!state_action_executed_` init block (which reloads portrait_.tex for the
    // NEW current_player_) only runs on the FOLLOWING update() call — for that one frame in
    // between, portrait_.tex still holds whoever's portrait was showing last (stale,
    // not null). Draw black instead of the stale portrait until this turn's own init has
    // actually run.
    if (!state_action_executed_) {
        renderer_.fillRect(0, 0, 640, 480, 0, 0, 0);
        return;
    }
    // portrait_.tex and portrait_.key_tex are both fresh disk loads each turn — gate the
    // whole overlay on portrait_.tex being valid this frame so nothing draws floating
    // over a blank background on a frame where the load hasn't landed yet.
    if (portrait_.tex) {
        // INFO{n}_0.BMP: full portrait, draw at (0,0) covering the screen
        renderer_.drawBitmap(portrait_.tex, 0, 0, 0, 0, 640, 480);

        // Key sprites overlay: ONE sprite per character (INFO{char_id}_1.BMP) contains all 6
        // province key images as a horizontal strip: province i is at
        // src_x = slot[i].x1 - slot[0].x1. Slot positions loaded from DATA/PLAYER.BIN in
        // load() — see key_slot_rects_.
        {
            // Curse-driven suppression: mode 1 (Gevaudan) hides key icons once curse_step_counter
            // reaches its max stage (3); mode 5 (Hellin) hides them unconditionally.
            const auto& key_rs = render_state_[current_player_];
            const bool suppress_keys = (key_rs.curse_type == 1 && key_rs.curse_step_counter >= 3) ||
                                       (key_rs.curse_type == 5);
            const auto& slot0 = key_slot_rects_[0];
            if (!suppress_keys) {
                for (int i = 0; i < 6; ++i) {
                    if (key_rs.keys[i] && portrait_.key_tex) {
                        const auto& slot = key_slot_rects_[i];
                        int src_x =
                            slot.x1 - slot0.x1;  // province i offset within the sprite strip
                        renderer_.drawBitmap(portrait_.key_tex, slot.x1, slot.y1, src_x, 0,
                                             slot.x2 - slot.x1, slot.y2 - slot.y1);
                    }
                }
            }
        }

        // Time remaining + power overlays — static strings computed once on portrait entry,
        // drawn at a fixed position. No WM_TIMER-driven refresh — matches the original's own
        // lack of periodic update while the portrait is shown.
        //
        // Curse-driven suppression: curse_type==5 (Hellin) always skips both; curse_type
        // ==1 (Gevaudan) skips once curse_step_counter>=2 — a different threshold than the key-icon
        // suppression above (2, not 3), both share the same single early return in the real
        // function so neither time nor power text shows once gated.
        const auto& tp_rs = render_state_[current_player_];
        const bool suppress_time_power =
            (tp_rs.curse_type == 5) || (tp_rs.curse_type == 1 && tp_rs.curse_step_counter >= 2);
        if (!suppress_time_power) {
            if (portrait_time_str_[0]) DrawStatText(portrait_time_str_, 37, 401);
            if (portrait_power_str_[0]) DrawStatText(portrait_power_str_, 37, 438);
        }
    }  // if (portrait_.tex)

    return;
}

void BoardGameScreen::ReopenFullmapAvi() {
    // Called from EnterShowingPortrait() every normal turn, plus a few AVI-sequence
    // completion/fallback paths and FlightMap's own direct restore — once near_end_flag_ is
    // set, the VERY NEXT EndTurn() unconditionally
    // fires the timer-expiry eyeloop, so reopening the board here is guaranteed wasted work
    // that also renders one stray frame before that transition takes effect on the following
    // tick. Skip it — matches the real "never returns to board display once heading to game
    // end" behavior, generalized to the timer-expiry trigger too.
    if (near_end_flag_ && !timer_expired_) {
        return;
    }
    // Shared reopen logic for every caller above. Mirrors the load() setup: same path,
    // same render rect.
    const auto fullmap_path = AssetPaths::avi("AV2", "FULLMAP");
    video_.close();
    if (video_.open(fullmap_path.string())) {
        video_.setRenderRect(kBoardAreaX, kBoardAreaY, kBoardAreaW, kBoardAreaH);
        video_loaded_ = true;
        last_video_frame_ = -1;  // force re-seek on next render
    }
}

void BoardGameScreen::StartGatekeeperResultTaunt() {
    // Fixed GKLOOP1 (never randomized here unlike the other EYELOOP call sites). Insult pool
    // selected by gatekeeper_taunt_survived_:
    //   survived   -> picks 1 of 6 (W3/AT_0342A..0347A)
    //   eliminated -> picks 1 of 3 (W1/AT_0070..0072)
    static const char* kGkSurvivedWav[6] = {
        "W3\\AT_0342A.WAV", "W3\\AT_0343A.WAV", "W3\\AT_0344A.WAV",
        "W3\\AT_0345A.WAV", "W3\\AT_0346A.WAV", "W3\\AT_0347A.WAV",
    };
    static const char* kGkEliminatedWav[3] = {
        "W1\\AT_0070.WAV",
        "W1\\AT_0071.WAV",
        "W1\\AT_0072.WAV",
    };

    teleport_.kind = TeleportAviState::Kind::RollEnding;

    const char* insult_wav = gatekeeper_taunt_survived_ ? kGkSurvivedWav[RollIndex(RngNext(), 6)]
                                                        : kGkEliminatedWav[RollIndex(RngNext(), 3)];

    gkloop_avi_path_ = AssetPaths::avi("AVI", "GKLOOP1").string();
    gkloop_post_state_ = teleport_.post_state;
    teleport_.post_state = BoardGameState::Moving;  // reset to default
    gkloop_enc_audio_ = playWav(insult_wav, AudioFlags::Async);

    video_.close();
    video_loaded_ = false;
    video_.setRenderRect(kBoardAreaX, kBoardAreaY, kBoardAreaW, kBoardAreaH);
    if (video_.open(gkloop_avi_path_)) {
        video_.play(true);
    }
    black_flash_next_state_ = BoardGameState::GkLoopAVI;
    current_state_ = BoardGameState::BlackFlash;
    state_start_time_ = std::chrono::steady_clock::now();
    state_action_executed_ = false;
}

void BoardGameScreen::DrawStepCounter(int steps) {
    // COUNTER.BMP background (65x64) at (290, 410).
    //
    // Khufu curse (curse_type==3): the decorative frame is absent while cursed — just
    // digits on a flat black patch at (300, 428), a tighter 46x30 box instead of 65x64. That
    // placement lines up with where this file's own 2-digit centering math already puts the
    // glyphs (x0~308, y0~426 for the 28x32 digit block below).
    // current_state_==Encounter is included here too (not just Moving): once all steps are
    // taken, a still-cursed player hands off from Moving straight into Encounter, and without
    // this the decorative COUNTER.BMP box would flash back in for those frames instead of
    // staying on the Khufu black rect.
    const bool khufu_box =
        (current_state_ == BoardGameState::Moving || current_state_ == BoardGameState::Encounter) &&
        render_state_[current_player_].curse_type == 3;
    if (khufu_box) {
        renderer_.fillRect(300, 428, 46, 30, 0, 0, 0);
    } else if (counter_tex_) {
        renderer_.drawBitmap(counter_tex_, 290, 410, 0, 0, 65, 64);
    }

    // Digit strip rendering — #ED1C24, RGBA PNG.
    // Strip: 10 cells of 16x20px, digit N at src_x = N*16. Centered inside the 65x64
    // COUNTER box at (290, 410).
    if (!digit_font_sprite_.handle) return;

    constexpr int kCW = 16;  // cell width in strip
    constexpr int kCH = 32;  // cell height: ascent(22)+descent(6)+2*PADDING(4)=32
    // kGap=-4 compensates for 2px left+right padding inside each cell, so glyphs sit at
    // natural proportional spacing as in the original GDI render.
    constexpr int kGap = -4;

    // Original always shows 2 digits with leading zero (e.g. "01", "08", "18")
    int n = (steps < 0) ? 0 : (steps > 99) ? 99 : steps;
    int tens = n / 10;
    int units = n % 10;

    // Centered against whichever box render() actually drew above — the 65x64/x=290
    // COUNTER.BMP box and the 46x30/x=300 Khufu black rect have different centers.
    constexpr int total_w = 2 * kCW + kGap;
    int x0 = khufu_box ? (300 + (46 - total_w) / 2) : (290 + (65 - total_w) / 2);
    int y0 = khufu_box ? (428 + (30 - kCH) / 2) : (410 + (64 - kCH) / 2);

    renderer_.drawSpriteTinted(digit_font_sprite_, x0, y0, tens * kCW, 0, kCW, kCH, 0xED, 0x1C,
                               0x24);
    renderer_.drawSpriteTinted(digit_font_sprite_, x0 + kCW + kGap, y0, units * kCW, 0, kCW, kCH,
                               0xED, 0x1C, 0x24);
}

void BoardGameScreen::DrawStatText(const char* text, int x, int y) {
    // Renders digits '0'-'9' and ':' from digit_font_sprite_ strip, LEFT-aligned.
    // Strip layout: cells 0-9 = digits, cell 10 = ':'. Cell size = 16x20 with kGap=-4.
    // Color: near-black (0x0A,0x0A,0x0A).
    if (!digit_font_sprite_.handle || !text || !*text) return;

    constexpr int kCW = 16;
    constexpr int kCH = 32;  // ascent(22)+descent(6)+2*PADDING(4)=32
    constexpr int kGap = -4;

    for (const char* p = text; *p; ++p) {
        int cell = (*p == ':') ? 10 : (*p - '0');
        renderer_.drawSpriteTinted(digit_font_sprite_, x, y, cell * kCW, 0, kCW, kCH, 0x0A, 0x0A,
                                   0x0A);
        // ':' is visually narrow — tighten spacing before and after it.
        if (*p == ':')
            x += 10;  // after ':'
        else if (*(p + 1) == ':')
            x += 10;  // before ':' (+1px)
        else
            x += (kCW + kGap);  // normal digit advance
    }
}

}  // namespace openfear::game
