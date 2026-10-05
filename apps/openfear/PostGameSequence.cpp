#include "PostGameSequence.h"
#include "ScreenLoop.h"
#include "openfear/game/FocusIndicator.h"
#include "openfear/game/GameState.h"
#include "openfear/platform/IInput.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>

namespace openfear {

// Plays AV1/ENDVID.AVI to completion. Not user-skippable — no click/ESC
// handler cuts it short. Returns false on quit (window close).
static bool PlayEndVidBumper(Platform* platform) {
    const auto avi_path = AssetPaths::avi("AV1", "ENDVID");
    auto& vid = *platform->video;
    // Native resolution, centered in a 640x480 frame - not scaled/letterboxed.
    vid.setRenderRect(80, 191, 480, 98);
    // One black frame before the blocking open() call below.
    platform->renderer->beginFrame();
    platform->renderer->endFrame();
    if (!vid.open(avi_path.string())) {
        LOG("AVI not found: %s (skipping)\n", avi_path.string().c_str());
        return true;
    }
    vid.play(true);
    LOG("Playing AVI: %s\n", avi_path.string().c_str());
    auto frame_start = std::chrono::steady_clock::now();
    while (true) {
        auto frame_begin = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(frame_begin - frame_start).count();
        frame_start = frame_begin;
        if (!platform->poll(dt)) {
            vid.close();
            return false;
        }
        platform->renderer->beginFrame();
        vid.renderFrame();
        platform_pre_vsync_yield();
        platform->renderer->endFrame();
        if (vid.status() != VideoStatus::Playing) break;
        platform_pace_frame(frame_begin, kFrameTarget);
    }
    vid.close();
    return true;
}

// M2\PLAYAGN + Yes/No buttons. A click outside both zones only stops
// whatever's playing - it does NOT skip to credits.
//
// Returns true if RunPostGameSequence should return immediately with the
// current play_again value (quit, or Yes resolved); false means fall
// through to the credits roll.
static bool RunPlayAgainPrompt(game::BoardGameScreen& board_game, Platform* platform,
                               bool& play_again) {
    bool skip_to_credits = false;
    const bool gatekeeper_miss = board_game.GatekeeperMiss();
    const auto playagn_path = AssetPaths::data("M2", "PLAYAGN", "BMP");
    auto& rend = *platform->renderer;
    TextureHandle playagn_tex = rend.loadBitmap(playagn_path.string());
    if (!playagn_tex) LOG("M2\\PLAYAGN.BMP not found: %s\n", playagn_path.string().c_str());

    // Guardian's win/lose announcement (W9 pool) - plays automatically the
    // moment PLAYAGN opens, independent of Yes/No.
    static const char* kWonAnnounce[] = {"AT_0755", "AT_0757"};
    static const char* kLostAnnounce[] = {"AT_0751", "AT_0752", "AT_0754"};
    const char* announce_name = gatekeeper_miss
                                    ? kWonAnnounce[board_game.RngNext() * 2u / 0x8000u]
                                    : kLostAnnounce[board_game.RngNext() * 3u / 0x8000u];
    const auto announce_path = AssetPaths::data("W9", announce_name, "WAV");
    LOG("Result announce audio (%s): %s\n", gatekeeper_miss ? "won" : "lost",
        announce_path.string().c_str());
    SoundHandle announce_wav = platform->audio->playWave(announce_path.string(), AudioFlags::Async);

    static const char* kWonTaunt[] = {"AT_0033", "AT_0034"};
    static const char* kLostTaunt[] = {"AT_0031", "AT_0032"};
    const auto gkloop1_path = AssetPaths::avi("AVI", "GKLOOP1");

    // Gamepad: D-pad up/down moves focus between Yes/No, A confirms -
    // matches every other 2-option screen's discrete-focus pattern.
    // focus_no starts true so the default lands on the safer option
    // (No/credits).
    bool playagn_focus_no = true;
    game::StickRepeater playagn_stick;
    auto playagn_stick_tick = std::chrono::steady_clock::now();
    auto frame_start = std::chrono::steady_clock::now();
    bool quit = false;
    while (!play_again && !skip_to_credits) {
        auto frame_begin = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(frame_begin - frame_start).count();
        frame_start = frame_begin;
        if (!platform->poll(dt)) {
            if (playagn_tex) rend.destroyTexture(playagn_tex);
            quit = true;
            break;
        }

        const bool playagn_gamepad = platform->input->activeDevice() == InputDevice::Gamepad;
        bool gp_yes = false, gp_no = false;
        if (playagn_gamepad) {
            const auto& gp_pa = platform->input->gamepad();
            int dx = 0, dy = 0;
            if (gp_pa.pressed[static_cast<size_t>(GamepadButton::DpadUp)]) dy = -1;
            if (gp_pa.pressed[static_cast<size_t>(GamepadButton::DpadDown)]) dy = 1;
            if (dy == 0) {
                const auto now_pa = std::chrono::steady_clock::now();
                const float dt_pa =
                    std::chrono::duration<float>(now_pa - playagn_stick_tick).count();
                playagn_stick_tick = now_pa;
                playagn_stick.Poll(gp_pa.left_x, gp_pa.left_y, dt_pa, dx, dy);
            }
            if (dy != 0) playagn_focus_no = !playagn_focus_no;
            if (gp_pa.pressed[static_cast<size_t>(GamepadButton::A)]) {
                gp_yes = !playagn_focus_no;
                gp_no = playagn_focus_no;
            }
        }
        if (platform->input->mouse().clicked[0] || gp_yes || gp_no) {
            const auto& m = platform->input->mouse();
            const bool hit_yes = gp_yes || (m.x >= 258 && m.x < 372 && m.y >= 274 && m.y < 358);
            const bool hit_no = gp_no || (m.x >= 258 && m.x < 372 && m.y >= 162 && m.y < 248);
            if (announce_wav)
                platform->audio->stop(
                    announce_wav);  // stop(0) halts ALL channels, not a no-op — guard
                                    // against announce_wav==0 (playWave failed to find the WAV)
            if (hit_yes) {
                LOG("PLAYAGN: Yes clicked -> restart taunt eyeloop, then restart\n");
                const char* taunt_name = gatekeeper_miss
                                             ? kWonTaunt[board_game.RngNext() * 2u / 0x8000u]
                                             : kLostTaunt[board_game.RngNext() * 2u / 0x8000u];
                const auto taunt_path = AssetPaths::data("W1", taunt_name, "WAV");
                LOG("Result taunt audio (%s): %s\n", gatekeeper_miss ? "won" : "lost",
                    taunt_path.string().c_str());
                SoundHandle taunt_wav =
                    platform->audio->playWave(taunt_path.string(), AudioFlags::Async);
                auto& taunt_vid = *platform->video;
                taunt_vid.setRenderRect(0, 77, 640, 326);
                bool taunt_video_open = taunt_vid.open(gkloop1_path.string());
                if (taunt_video_open) taunt_vid.play(true);
                auto taunt_frame_start = std::chrono::steady_clock::now();
                while (platform->audio->isPlaying(taunt_wav)) {
                    auto taunt_frame_begin = std::chrono::steady_clock::now();
                    float taunt_dt =
                        std::chrono::duration<float>(taunt_frame_begin - taunt_frame_start).count();
                    taunt_frame_start = taunt_frame_begin;
                    if (!platform->poll(taunt_dt)) {
                        if (taunt_video_open) taunt_vid.close();
                        if (playagn_tex) rend.destroyTexture(playagn_tex);
                        quit = true;
                        break;
                    }
                    if (taunt_video_open && taunt_vid.status() != VideoStatus::Playing) {
                        // Loop GKLOOP1 for as long as the taunt audio plays -
                        // same stream stays open, play() alone rewinds it.
                        taunt_vid.play(true);
                    }
                    // No background bitmap drawn under it either - an
                    // Eyeloop window covers its own area, it doesn't
                    // composite over the parent's last frame.
                    rend.beginFrame();
                    if (taunt_video_open) taunt_vid.renderFrame();
                    platform_pre_vsync_yield();
                    rend.endFrame();
                    platform_pace_frame(taunt_frame_begin, kFrameTarget);
                }
                if (quit) break;
                if (taunt_video_open) taunt_vid.close();
                play_again = true;
            } else if (hit_no) {
                LOG("PLAYAGN: No clicked -> skipping to credits\n");
                skip_to_credits = true;
            } else {
                // Only stops whatever audio is playing, no state change.
                LOG("PLAYAGN: click outside zones -> audio stopped only\n");
            }
            // Without this break, control would fall through to the
            // outer loop's own render below before the while-condition
            // check could see play_again/skip_to_credits - an extra
            // presented frame the real synchronous ASM transition
            // wouldn't have.
            if (hit_yes || hit_no) break;
        }

        rend.beginFrame();
        if (playagn_tex) rend.drawBitmap(playagn_tex, 0, 0, 0, 0, 640, 480);
        if (playagn_gamepad) {
            // Rect pulse, not border - matches PickChar/FearSelect style.
            const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count();
            const float phase = (static_cast<float>(now_ms % 1000) / 1000.0f) * 2.0f * 3.14159265f;
            const float glow = 0.5f + 0.5f * std::sin(phase);
            const uint8_t alpha = static_cast<uint8_t>(glow * 25.0f);
            // x2-x1/y2-y1 here are the same hit_no/hit_yes bounds above,
            // just converted to the (x,y,w,h) fillRectAdditive expects.
            if (playagn_focus_no)
                rend.fillRectAdditive(258, 162, 372 - 258, 248 - 162, 255, 255, 255, alpha);
            else
                rend.fillRectAdditive(258, 274, 372 - 258, 358 - 274, 255, 255, 255, alpha);
        }
        platform_pre_vsync_yield();
        rend.endFrame();
        platform_pace_frame(frame_begin, kFrameTarget);
    }

    if (quit) return true;
    if (playagn_tex) rend.destroyTexture(playagn_tex);

    if (play_again) {
        auto* gs_restart = game::GameStateManager::Instance().Get();
        if (gs_restart && gs_restart->IsValid()) {
            gs_restart->total_players = 0;
            gs_restart->sub_idx = 0;
            gs_restart->game_duration_s_ = 0;
            gs_restart->turn_time_limit_ms_ = 0;
        }
        return true;
    }
    return false;
}

// Credits roll. The original keeps THREE independent index fields:
// display_idx (the slide actually on screen, advances ONLY via the timer
// auto-tick, never touched by a click), single_idx and double_idx (looked
// up against their own jump table when that click type resolves).
// Whenever either click type resolves a jump, all THREE fields sync to
// the new target - but the auto-timer's own advance only ever touches
// display_idx. Net effect: if the timer auto-advances one or more slides
// with no intervening click, single_idx/double_idx go stale - the NEXT
// click's jump is computed from that stale position, not from what's on
// screen. Also real: a single click is a complete no-op if the last
// registered click was a double - only another double-click can un-stick
// it.
// Index 11 in all 3 tables is a shared end-sentinel (duration==1) - only
// 11 real slides exist (CR/CREDIT1..11.BMP). The single-vs-double
// classification of an incoming click uses a timing-window heuristic (SDL
// has no native double-click event) - port approximation, not something
// traceable to the original.
static void RunCreditsRoll(game::BoardGameScreen& board_game, Platform* platform) {
    static const int kCreditDurationMs[12] = {2000, 2000, 1500, 1500, 3000, 3000,
                                              3000, 1500, 1500, 2000, 2000, 1};
    static const int kCreditSingleJump[12] = {4, 4, 4, 4, 5, 6, 9, 9, 9, 10, 10, 1};
    static const int kCreditDoubleJump[12] = {10, 4, 4, 4, 9, 9, 9, 9, 9, 10, 10, 1};
    constexpr auto kDoubleClickWindow = std::chrono::milliseconds(500);

    auto& rend = *platform->renderer;
    int display_idx = 0;
    int single_idx = 0;
    int double_idx = 0;
    int last_click_kind = 0;  // 0=none yet, 1=single, 2=double
    TextureHandle slide_tex = 0;
    // i == 11 is the end sentinel, never a real slide - 11 real slides
    // exist (CR/CREDIT1..11.BMP), indices 0-10.
    auto load_slide = [&](int i) {
        if (slide_tex) {
            rend.destroyTexture(slide_tex);
            slide_tex = 0;
        }
        if (i >= 0 && i < 11) {
            const auto path =
                AssetPaths::data("CR", ("CREDIT" + std::to_string(i + 1)).c_str(), "BMP");
            slide_tex = rend.loadBitmap(path.string());
            if (!slide_tex) LOG("Credits slide not found: %s\n", path.string().c_str());
        }
    };
    load_slide(display_idx);
    auto slide_start = std::chrono::steady_clock::now();
    bool has_last_click = false;
    auto last_click_time = std::chrono::steady_clock::now();
    auto frame_start = std::chrono::steady_clock::now();
    bool quit = false;

    // Insult audio plays continuously while the slides roll, if you don't
    // click: 2000ms before the first clip, 1000ms between subsequent
    // clips. Same 2-pool formula as PauseScreen's own repeating ambient
    // insult (50%/50% split between a 12-clip and a 5-clip pool - same
    // asset files, same RNG formula).
    static const char* kCreditsInsultPoolA[12] = {
        "WAV\\AT_0471E.WAV", "WAV\\AT_0472C.WAV", "WAV\\AT_0474A.WAV", "WAV\\AT_0474B.WAV",
        "WAV\\AT_0474C.WAV", "WAV\\AT_0474D.WAV", "WAV\\AT_0474E.WAV", "WAV\\AT_0474F.WAV",
        "WAV\\AT_0474G.WAV", "WAV\\AT_0475A.WAV", "WAV\\AT_0475B.WAV", "WAV\\AT_0475C.WAV",
    };
    static const char* kCreditsInsultPoolB[5] = {
        "WAV\\AT_0426.WAV",  "WAV\\AT_0514A.WAV", "WAV\\AT_0514B.WAV",
        "WAV\\AT_0514C.WAV", "WAV\\AT_0514E.WAV",
    };
    auto play_credits_insult = [&]() -> SoundHandle {
        const bool pool_a = (board_game.RngNext() * 2u / 0x8000u) == 0;
        const char* rel = pool_a ? kCreditsInsultPoolA[board_game.RngNext() * 12u / 0x8000u]
                                 : kCreditsInsultPoolB[board_game.RngNext() * 5u / 0x8000u];
        return platform->audio->playWave(rel, AudioFlags::Async);
    };
    SoundHandle insult_wav = 0;
    bool insult_waiting = true;
    bool insult_first_wait = true;
    auto insult_wait_start = std::chrono::steady_clock::now();

    LOG("--- Credits roll: 11 real slides, starting idx=%d ---\n", display_idx);
    while (display_idx < 11) {
        auto frame_begin = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(frame_begin - frame_start).count();
        frame_start = frame_begin;
        if (!platform->poll(dt)) {
            LOG("--- Credits roll: quit (SDL_QUIT / platform->poll false) at idx=%d ---\n",
                display_idx);
            quit = true;
            break;
        }

        bool jumped = false;
        const int prev_display = display_idx;
        // Gamepad: A counts as a single click - port-only UX convenience,
        // no attempt at emulating the double-click table via a 2nd
        // button (slides auto-advance by timer anyway).
        const bool gp_credits_click =
            platform->input->activeDevice() == InputDevice::Gamepad &&
            platform->input->gamepad().pressed[static_cast<size_t>(GamepadButton::A)];
        if (platform->input->mouse().clicked[0] || gp_credits_click) {
            const bool is_double =
                has_last_click && (frame_begin - last_click_time) <= kDoubleClickWindow;
            has_last_click = !is_double;
            last_click_time = frame_begin;

            if (!is_double && last_click_kind == 2) {
                // Single click after a double is a no-op - only another double-click un-sticks it.
                LOG("--- Credits roll: single click ignored (last_click_kind=2) at idx=%d "
                    "---\n",
                    display_idx);
            } else {
                const int target =
                    is_double ? kCreditDoubleJump[double_idx] : kCreditSingleJump[single_idx];
                LOG("--- Credits roll: %s click (single_idx=%d double_idx=%d) -> %d ---\n",
                    is_double ? "double" : "single", single_idx, double_idx, target);
                single_idx = double_idx = display_idx = target;
                last_click_kind = is_double ? 2 : 1;
                // Several targets map a slide to itself - only
                // reload/reset the timer on a real change.
                jumped = (display_idx != prev_display);
            }
        } else {
            const auto elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(frame_begin - slide_start)
                    .count();
            if (display_idx < 11 && elapsed_ms >= kCreditDurationMs[display_idx]) {
                ++display_idx;  // timeout only ever advances display_idx, not single/double_idx
                jumped = true;
                LOG("--- Credits roll: timeout (%llums) idx %d -> %d ---\n",
                    (unsigned long long)elapsed_ms, prev_display, display_idx);
            }
        }
        if (jumped && display_idx < 11) {
            load_slide(display_idx);
            slide_start = frame_begin;
        }

        // Re-arm the ambient insult loop (2000ms before the first clip, 1000ms between).
        if (insult_wav == 0 || !platform->audio->isPlaying(insult_wav)) {
            if (!insult_waiting) {
                insult_waiting = true;
                insult_wait_start = frame_begin;
            } else if (std::chrono::duration_cast<std::chrono::milliseconds>(frame_begin -
                                                                             insult_wait_start)
                           .count() >= (insult_first_wait ? 2000 : 1000)) {
                insult_waiting = false;
                insult_first_wait = false;
                insult_wav = play_credits_insult();
            }
        }

        rend.beginFrame();
        if (slide_tex) rend.drawBitmap(slide_tex, 0, 0, 0, 0, 640, 480);
        platform_pre_vsync_yield();
        rend.endFrame();

        platform_pace_frame(frame_begin, kFrameTarget);
    }
    if (!quit) {
        LOG("--- Credits roll: finished naturally at idx=%d ---\n", display_idx);
    }
    if (insult_wav) platform->audio->stop(insult_wav);  // same stop(0)=halt-all guard as above
    if (slide_tex) rend.destroyTexture(slide_tex);
}

bool RunPostGameSequence(game::BoardGameScreen& board_game, Platform* platform) {
    bool play_again = false;

    if (!PlayEndVidBumper(platform)) return play_again;

    if (RunPlayAgainPrompt(board_game, platform, play_again)) return play_again;

    RunCreditsRoll(board_game, platform);
    return play_again;
}

}  // namespace openfear
