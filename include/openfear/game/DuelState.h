#pragma once
#include "openfear/platform/IRenderer.h"
#include "openfear/platform/IAudio.h"

namespace openfear::game {

// Duel subsystem's shared state — triggered by the portrait-corner click
// (DuelChooseOpponent), genuinely used across all 3 real duel screens
// (DuelChooseOpponent, DuelAttackGauge, DuelStealOrCurse). One instance,
// owned by BoardGameScreen (board_.duel_), friended to those 3 classes.
struct DuelState {
    // Gauge battle (DuelAttackGauge): total=power+initial; fills
    // proportional to power; higher power wins. Key transfer: loser must
    // give one held key to winner.
    int attacker = -1;         // player initiating Duel (current_player_)
    int defender = -1;         // opponent player in Duel
    int winner = -1;           // winner of gauge battle (-1 = not yet decided)
    int loser = -1;            // loser of gauge battle (-1 = not yet decided)
    float gauge_atk = 0.0f;    // attacker cur/(276-1) ratio 0..1 (1=drained/lost)
    float gauge_def = 0.0f;    // defender cur/(276-1) ratio 0..1 (same)
    TextureHandle bg_tex = 0;  // A1/ATTACKB.BMP — gauge battle background
    // Real vertical gauge-fill sprites, cropped from the BOTTOM up by fill
    // amount — real thermometers fill bottom-to-top.
    TextureHandle gauge_atk_tex = 0;  // A1/G1.BMP — attacker gauge fill sprite
    TextureHandle gauge_def_tex = 0;  // A1/G2.BMP — defender gauge fill sprite
    TextureHandle getkeys_tex = 0;    // A1/ATTACKL.BMP — DuelStealOrCurse's bg (real KeyTransfer)
    TextureHandle key_highlight_tex =
        0;  // A1/ATTACKC.BMP — colored/"lit" key overlay for stealable keys
    SpriteInfo key_highlight_sprite{};  // key_highlight_tex wrapped for DrawFocusTint
    TextureHandle sigil_tex = 0;        // A1/ATTACK2C.BMP — 6-icon sigil strip that
                                        // persists across DuelAttackGauge/DuelStealOrCurse

    // Opponent picker (DuelChooseOpponent) — populated by BoardGameScreen's
    // own portrait-corner-click/AI-eligibility trigger sites before this
    // screen ever mounts.
    int choose_num_candidates = 0;
    // -1 = no AI-driven pick pending, else the pre-known target slot the
    // AI attacker will auto-select — AI duels still show this screen,
    // just auto-resolving.
    int choose_ai_target = -1;
    float choice_ai_timer = 0.0f;  // AI winner auto-decide delay (shared with DuelStealOrCurse)
    TextureHandle choose_icons_tex = 0;   // MISC/ATTKCHSE.BMP — ChooseOpponent's 6 fixed sigil
                                          // icons baked into the 3x2 CHOOSE.BIN grid
    TextureHandle choose_broken_tex = 0;  // MISC/ATTKEMPT.BMP — same 6-cell grid, broken glass,
                                          // stamped over ineligible target cells
    SoundHandle choose_glass_wav = 0;     // W9/AT_GLASS.WAV — plays once a valid target is picked,
                                          // not when the picker opens
    SoundHandle choose_prompt_wav = 0;    // WAV/AT_0631.WAV — fixed opponent-pick announcement,
                                        // played right after DrawPortraits at screen open (not the
                                        // RNG 1-of-4 steal-result pool)

    // Steal-vs-curse choice (DuelStealOrCurse): zones 0-5 = steal that
    // province's key (only valid if loser holds it and winner doesn't),
    // zone 6 = curse (always valid).
    int steal_or_curse_zone = -1;  // -1=none yet, 0-5=province steal, 6=curse
    // Duel-loss taunt, started in DuelAttackGauge's win/loss block
    // (kDuelLoseWav) — keeps playing into DuelStealOrCurse (mounts 1 frame
    // later), which withholds clicks/AI-timer until it finishes.
    SoundHandle lose_taunt_wav = 0;
};

}  // namespace openfear::game
