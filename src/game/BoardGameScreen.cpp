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

// Digit font strip (steps/time/power displays), compiled in by
// openfear_embed_binary_file() (see src/game/CMakeLists.txt) — OpenFear's
// own asset, not part of the CD data, since no runtime path would be
// reliable on every target.
extern "C" const unsigned char g_digitfont_bmp[];
extern "C" const unsigned long g_digitfont_bmp_len;

namespace openfear::game {

static const char* kRollingWav = "W11\\ROLLING.WAV";
static const char* kRolledWav = "W11\\ROLLED.WAV";

// GateOpen taunt pool — shared by DispatchBlackHoleTeleport (enc type 3, in
// BoardGameScreenEncounters.cpp) and ResolveFateCard's GateOpen variant (in
// BoardGameScreenDuelAndKeys.cpp) — same mechanic, 2 trigger paths in 2
// different translation units, so this needs real (external) linkage.
extern const char* kGateOpenWav[7] = {"W5\\AT_0553.WAV", "W5\\AT_0554.WAV", "W5\\AT_0556.WAV",
                                      "W5\\AT_0557.WAV", "W5\\AT_0559.WAV", "W5\\AT_0560.WAV",
                                      "W2\\AT_0303.WAV"};

// Fortune-card plaque table — shared by ResolveFortuneCard's own grant-side
// reveal (in BoardGameScreenDuelAndKeys.cpp) and DuelChooseOpponent's
// Protection pre-resolve overlay (in subscreens/DuelChooseOpponent.cpp) —
// same mechanic, 2 trigger paths in 2 different translation units, so this
// needs real (external) linkage.
extern const char* kFortuneCard[6] = {"FORT_S08", "FORT_S12", "FORT_S11",
                                      "FORT_S10", "FORT_S09", "FORT_S13"};

// Inter-province portal table (24 entries x 8 bytes), extracted from the
// original's data segment. Real (external) linkage, not static/constexpr —
// BoardGameScreenDebug.cpp's DebugSetupGateArrivalTest() and
// BoardGameScreenMovement.cpp's HandlePortalCrossing/HandleWallDestination
// need it too, each with its own matching `struct PortalEntry` + `extern`
// declaration (kept an internal implementation detail, not exposed via
// BoardGameScreen.h).
struct PortalEntry {
    uint16_t src_stone, src_prov, dst_stone, dst_prov;
};
extern const PortalEntry kPortalTable[24] = {
    {63, 0, 118, 5},  {43, 0, 29, 6},  {139, 0, 135, 3},  // group 0
    {95, 1, 42, 4},   {105, 1, 15, 6}, {115, 1, 77, 5},   // group 1
    {65, 2, 106, 3},  {55, 2, 5, 6},   {135, 2, 84, 4},   // group 2
    {135, 3, 139, 0}, {124, 3, 38, 6}, {106, 3, 65, 2},   // group 3
    {84, 4, 135, 2},  {35, 4, 10, 6},  {42, 4, 95, 1},    // group 4
    {77, 5, 115, 1},  {128, 5, 24, 6}, {118, 5, 63, 0},   // group 5
    {10, 6, 35, 4},   {5, 6, 55, 2},   {38, 6, 124, 3},   // group 6
    {29, 6, 43, 0},   {24, 6, 128, 5}, {15, 6, 105, 1},   // group 7
};

// Key-rotation table (province offsets by char_id), extracted from the original's data
// segment. Real (external) linkage, not static/constexpr — BoardGameScreenDuelAndKeys.cpp's
// FindUnheldProvince/FindHeldProvinceByRotation and BoardGameScreenUpdate.cpp's KeyAward
// target roll need it too, each with its own matching `extern` declaration (kept an internal
// implementation detail, not exposed via BoardGameScreen.h).
extern const int kKeyTable[6] = {0, 5, 1, 4, 2, 3};

// DATA/MAPMOVE.BIN parser: 3 polygons, each u16 count followed by
// count*(i16 x, i16 y), then a trailing 8-byte rect (i16 x1,y1,x2,y2) for
// the step counter box's click zone — same file, one continuous read
// session, matching the real ASM's own layout. NOT the simple 4-value
// rect format RectFile handles — a distinct, custom format.
struct MapMoveData {
    std::array<std::vector<std::pair<int16_t, int16_t>>, 3> polygons;
    int16_t counter_box[4] = {0, 0, 0, 0};  // x1, y1, x2, y2
    bool counter_box_valid = false;
};
static MapMoveData LoadMapMoveZones(openfear::IAssetProvider& provider, const std::string& path) {
    MapMoveData result;
    std::vector<uint8_t> raw = provider.ReadFile(path);
    size_t pos = 0;
    auto read_bytes = [&](void* dst, size_t n) -> bool {
        if (pos + n > raw.size()) return false;
        std::memcpy(dst, raw.data() + pos, n);
        pos += n;
        return true;
    };
    for (int p = 0; p < 3; ++p) {
        uint16_t count = 0;
        if (!read_bytes(&count, sizeof(count))) {
            result.polygons[p].clear();
            return result;
        }
        result.polygons[p].reserve(count);
        for (uint16_t i = 0; i < count; ++i) {
            int16_t x = 0, y = 0;
            if (!read_bytes(&x, sizeof(x)) || !read_bytes(&y, sizeof(y))) {
                result.polygons[p].clear();
                return result;
            }
            result.polygons[p].emplace_back(x, y);
        }
    }
    result.counter_box_valid = read_bytes(result.counter_box, sizeof(result.counter_box));
    return result;
}

BoardGameScreen::BoardGameScreen(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio,
                                 IVideoPlayer& video)
    : GameScreen(assets, renderer, audio), video_(video),
      pause_screen_(assets, renderer, audio, *this),
      minimap_screen_(assets, renderer, audio, *this),
      bribe_screen_(assets, renderer, audio, *this),
      curecurse_screen_(assets, renderer, audio, *this),
      fortune_door_screen_(assets, renderer, audio, *this),
      flight_map_screen_(assets, renderer, audio, *this),
      coffin_screen_(assets, renderer, audio, *this), slot_screen_(assets, renderer, audio, *this),
      doors_screen_(assets, renderer, audio, *this),
      dicekeys_screen_(assets, renderer, audio, *this),
      onedie_screen_(assets, renderer, audio, *this),
      keyaward_screen_(assets, renderer, audio, *this),
      duel_attack_gauge_(assets, renderer, audio, *this),
      duel_choose_screen_(assets, renderer, audio, *this, duel_attack_gauge_),
      duel_steal_or_curse_screen_(assets, renderer, audio, *this),
      blackjack_screen_(assets, renderer, audio, *this) {}

bool BoardGameScreen::load(const AssetPaths&) {
    const auto load_t0 = std::chrono::steady_clock::now();
    auto load_elapsed_ms = [&]() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - load_t0)
            .count();
    };

    GameState* gs = GameStateManager::Instance().Get();
    if (!gs || !gs->IsValid()) {
        std::fprintf(stderr, "[BoardGameScreen] ERROR: GameState invalid\n");
        return false;
    }

    if (!BoardInitializer::IsInitialized()) {
        std::fprintf(stderr, "[BoardGameScreen] ERROR: BoardInitializer not initialized\n");
        return false;
    }

    total_players_ = gs->total_players;
    if (total_players_ < 1 || total_players_ > 6) {
        std::fprintf(stderr, "[BoardGameScreen] ERROR: Invalid player count %d\n", total_players_);
        return false;
    }

    current_player_ = 0;
    current_state_ = BoardGameState::ShowingPortrait;

    // Per-player initial state, active players only (0..total_players-1).
    // Province = character_id. Starting stone = 0 for all. Starting
    // direction: province 1 -> South(2), others -> North(0).
    for (int i = 0; i < total_players_ && i < 6; ++i) {
        PlayerObject* po = PlayerInitializer::GetPlayer(static_cast<uint8_t>(i));
        uint16_t province =
            (po && po->character_id <= 5) ? po->character_id : static_cast<uint16_t>(i);
        render_state_[i].province = province;
        render_state_[i].stone_id = 0;
        render_state_[i].direction = (province == 1) ? 2 : 0;
    }

    gs->player_idx = render_state_[0].province;
    gs->sub_idx = 0;
    mapfile_direction_ = static_cast<uint8_t>(render_state_[0].direction);

    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    uint32_t time_seed = static_cast<uint32_t>(duration.count() & 0xFFFFFFFF);
    rng_seed_ = time_seed ? time_seed : 0x4650;

    const auto fullmap_path = AssetPaths::avi("AV2", "FULLMAP");
    if (!video_.open(fullmap_path.string())) {
        std::fprintf(stderr, "[BoardGameScreen] ERROR: FULLMAP.AVI not found at %s\n",
                     fullmap_path.string().c_str());
        return false;
    }
    // Do NOT call play() — video frames are selected by zone_idx + direction via seekFrame()
    video_.setRenderRect(kBoardAreaX, kBoardAreaY, kBoardAreaW, kBoardAreaH);
    video_loaded_ = true;

    {
        const std::string mapfile_path = "MAPFILE";
        if (!mapfile_.Load(assets_.provider(), mapfile_path)) {
            std::fprintf(stderr, "[BoardGameScreen] ERROR: MAPFILE not loaded\n");
            return false;
        }
    }

    {
        auto rects = assets_.rect_file("player");
        if (rects && rects->size() == 6) {
            for (int i = 0; i < 6; ++i) key_slot_rects_[i] = (*rects)[i];
        } else {
            std::fprintf(stderr, "[BoardGameScreen] WARNING: PLAYER.BIN not loaded/wrong size — "
                                 "key icons will not render\n");
        }
    }
    if (!duel_choose_screen_.LoadZoneRects()) return false;
    if (!dicekeys_screen_.LoadZoneRects()) return false;
    if (!blackjack_screen_.LoadZoneRects()) return false;
    if (!doors_screen_.LoadZoneRects()) return false;
    if (!fortune_door_screen_.LoadZoneRects()) return false;

    {
        // move_zone_polys_ storage order matches on-disk order: LEFT, RIGHT, CENTER.
        const std::string mapmove_path = "DATA/MAPMOVE.BIN";
        auto file_order = LoadMapMoveZones(assets_.provider(), mapmove_path);
        move_zone_polys_ = file_order.polygons;
        const bool ok = !move_zone_polys_[0].empty() && !move_zone_polys_[1].empty() &&
                        !move_zone_polys_[2].empty();
        if (!ok) {
            std::fprintf(stderr, "[BoardGameScreen] ERROR: MAPMOVE.BIN not loaded — "
                                 "movement/dice zones would never respond to clicks\n");
            return false;
        }
        // Falls back to counter_box_{x,y,w,h}_'s known-good default
        // values (set at declaration) if these 8 trailing bytes aren't
        // present — this part alone stays lenient.
        if (file_order.counter_box_valid) {
            counter_box_x_ = file_order.counter_box[0];
            counter_box_y_ = file_order.counter_box[1];
            counter_box_w_ = file_order.counter_box[2] - file_order.counter_box[0];
            counter_box_h_ = file_order.counter_box[3] - file_order.counter_box[1];
        }
    }

    // COUNTER.BMP — step count display box, 65x64, rendered at (290, 410).
    counter_tex_ = assets_.misc_texture("COUNTER");

    // Digit font strip (10 cells of 16x20px, digit N at src_x = N*16).
    {
        TextureHandle fh = renderer_.loadBitmapFromMemory(g_digitfont_bmp,
                                                          static_cast<size_t>(g_digitfont_bmp_len));
        if (fh) {
            digit_font_sprite_ = renderer_.createSprite(fh);
        } else {
            std::fprintf(stderr,
                         "[BoardGameScreen] WARNING: embedded digitfont.bmp failed to decode\n");
        }
    }

    coffin_screen_.LoadPolygon();
    if (!coffin_screen_.LoadZoneRects()) return false;
    if (!curecurse_screen_.LoadZoneRects()) return false;
    if (!duel_attack_gauge_.LoadZoneRects()) return false;

    // MVDICE.BMP dice overlay (600x151, 6 frames of 100x151).
    dice_tex_ = assets_.misc_texture("MVDICE");
    if (dice_tex_) {
        dice_sprite_ = renderer_.createSprite(dice_tex_);  // for gamepad focus pulse
    }

    rolling_wav_ = preloadWav(kRollingWav);
    rolled_wav_ = preloadWav(kRolledWav);

    // DiceKeys, OneDie, SlotMachine, Blackjack, Bribe, and FortuneDoor —
    // each triggered by its own real enc type roll
    // (BoardGameScreenEncounters.cpp's DispatchEncounter) — plus FlightMap
    // (triggered separately, by the Flight Key on a type-6 stone) all
    // lazy-load their TEXTURES/AUDIO independently at their own real
    // trigger sites instead of preloading here. Their click-ZONE rects
    // (BJACK1/2.BIN, NUMS.BIN, DOOR3/6/10.BIN) are the exception: those
    // preload above (LoadZoneRects()), same hard-dependency treatment as
    // CHOOSE.BIN/GETKEYS.BIN/MAPMOVE.BIN - a missing zone file fails
    // load() outright instead of surfacing only when the encounter fires.

    // Duel (DoAttack) + DuelChooseOpponent assets lazy-load at their real
    // trigger sites (HandleDuelCornerClick() on the human/AI-picker path,
    // DuelAttackGauge::Enter() for the gauge/key-transfer assets).
    if (!duel_steal_or_curse_screen_.LoadZoneRects()) return false;

    {
        for (int i = 0; i < total_players_ && i < 6; ++i) {
            PlayerObject* po = PlayerInitializer::GetPlayer(static_cast<uint8_t>(i));
            int char_id = (po && po->character_id <= 5)
                              ? static_cast<int>(po->character_id)
                              : (i % 6);  // fallback: cycle through chars if uninitialized
            render_state_[i].char_id = static_cast<uint16_t>(char_id);
            render_state_[i].is_ai = po && (po->human_flag != 0x1);

            // Power: humans start at 25. AI: starts at ai_initial_power_
            // (10, 20, or 25 from difficulty; 0 if not in 1P mode).
            render_state_[i].power = render_state_[i].is_ai ? ai_initial_power_ : 25;

            // Two distinct real render_state-creation functions: the
            // human path sets dice_restricted=2 (normal), the AI path
            // sets it=1 (forced 1-die).
            render_state_[i].dice_restricted = render_state_[i].is_ai ? 1 : 2;

            // Initial keys: human player(s) get initial_keys_count_ keys,
            // via the real GainKey path (also ORs kKeyPowerBitmask into
            // key_power_flags — a direct array write would silently
            // disable every key power).
            if (!render_state_[i].is_ai) {
                for (int k = 0; k < initial_keys_count_ && k < 6; ++k) {
                    GainKey(i, (kKeyTable[k] + char_id) % 6);
                }
            }
        }
    }

    // Idle ambient nag pool — global, not per-character.
    static const char* kAmbientNagWavs[12] = {
        "WAV\\AT_0471E.WAV", "WAV\\AT_0472C.WAV", "WAV\\AT_0474A.WAV", "WAV\\AT_0474B.WAV",
        "WAV\\AT_0474C.WAV", "WAV\\AT_0474D.WAV", "WAV\\AT_0474E.WAV", "WAV\\AT_0474F.WAV",
        "WAV\\AT_0474G.WAV", "WAV\\AT_0475A.WAV", "WAV\\AT_0475B.WAV", "WAV\\AT_0475C.WAV",
    };
    for (int i = 0; i < 12; ++i) {
        ambient_nag_wavs_[i] = preloadWav(kAmbientNagWavs[i]);
    }

    // Stone pixel coordinates (DATA/MAP{n}.PNT) — tiny binary tables, fine
    // to keep all 8 resident. The MAP0{n}.BMP overview bitmap itself is
    // loaded on demand, when the minimap overlay actually opens (see
    // MinimapScreen::Enter()).
    minimap_screen_.LoadProvincePoints();

    // Turn timeout announcement.
    timeout_wav_ = preloadWav("W1\\AT_0063.WAV");
    portal_blocked_wav_ = preloadWav("WAV\\STOP.WAV");

    // Turn time limit, captured once at game start (turn_time_limit_ms_ is
    // reset to 0 each turn by ResetPerTurnFields()).
    { turn_time_limit_ms_ = (gs->turn_time_limit_ms_ > 0) ? gs->turn_time_limit_ms_ : 0; }

    cursor_state_.animation_frame = 0;
    cursor_state_.direction = 2;         // HORIZONTAL (initial, UI visual only)
    cursor_state_.is_animating = false;  // don't animate until dice are rolled
    cursor_state_.last_frame_time = std::chrono::steady_clock::now();
    cursor_state_.frame_accumulator = 0.0f;

    render_ctx_.tile_width = 64;
    render_ctx_.tile_height = 64;
    render_ctx_.board_x = 0;
    render_ctx_.board_y = 77;

    teleport_.dest_prov = -1;

    state_start_time_ = std::chrono::steady_clock::now();
    state_action_executed_ = false;

    platform_log_memory_stats("board-loaded", renderer_);
    return true;
}

void BoardGameScreen::unload() {
    // Restore the default anchor for whatever screen comes next — render()
    // sets Bottom/Center per-state while this screen is active, but
    // nothing calls render() again once we're unloaded.
    renderer_.setSafeAreaAnchor(IRenderer::SafeAreaAnchor::Center);
    platform_log_memory_stats("match-end", renderer_);
    if (video_loaded_) {
        video_.close();
        video_loaded_ = false;
    }

    // Safety net for quitting mid-minigame: FreeLazyMinigameAssets() normally only runs at
    // TurnEnd, so a quit while e.g. Blackjack/SlotMachine/Bribe assets are still loaded would
    // otherwise skip them here. All of its frees are `if (handle != 0)`-guarded, so calling it
    // again on an already-clean state (the common case, quit between turns) is a no-op.
    FreeLazyMinigameAssets();

    // Free persistent match-duration audio: audio_ is owned outside this class and outlives
    // it across a multi-match session, so these handles would otherwise stay resident
    // forever. unload() already stops its own handle, so no separate stop() call is needed —
    // and audio_.stop(0) halts ALL channels, not just this one.
    audio_.unload(rolling_wav_);
    rolling_wav_ = 0;
    audio_.unload(rolled_wav_);
    rolled_wav_ = 0;
    for (auto& h : ambient_nag_wavs_) {
        audio_.unload(h);
        h = 0;
    }
    ambient_nag_wav_ = 0;
    audio_.unload(timeout_wav_);
    timeout_wav_ = 0;
    audio_.unload(portal_blocked_wav_);
    portal_blocked_wav_ = 0;
    audio_.unload(portrait_.theme_wav);
    portrait_.theme_wav = 0;
    audio_.unload(portrait_.turn_wav);
    portrait_.turn_wav = 0;
    audio_.unload(portrait_.move_wav);
    portrait_.move_wav = 0;
    portrait_wav_ = 0;  // alias into portrait_.theme_wav or .curse_hellin_wav, not separately owned

    pause_screen_.unload();

    if (card_tex_) {
        renderer_.destroyTexture(card_tex_);
        card_tex_ = 0;
        card_active_ = false;
    }

    minimap_screen_.unload();

    ReleaseTexture(portrait_.tex);
    ReleaseTexture(portrait_.key_tex);
    ReleaseTexture(portrait_.curse_indicator_tex);
    ReleaseAudio(portrait_.curse_blindness_wav);
    ReleaseAudio(portrait_.curse_hellin_wav);

    // Board-load-time textures — freed here rather than relying solely on
    // renderer_.shutdown() destroying its whole texture map at final process exit.
    ReleaseTexture(counter_tex_);
    ReleaseTexture(dice_tex_);

    // Duel (attack) textures
    ReleaseTexture(duel_.bg_tex);
    ReleaseTexture(duel_.getkeys_tex);
    ReleaseTexture(duel_.choose_icons_tex);
    ReleaseTexture(duel_.choose_broken_tex);
    ReleaseTexture(duel_.key_highlight_tex);
    ReleaseTexture(duel_.sigil_tex);
    ReleaseTexture(duel_.gauge_atk_tex);
    ReleaseTexture(duel_.gauge_def_tex);
    ReleaseAudio(duel_.choose_glass_wav);
    ReleaseAudio(duel_.choose_prompt_wav);
    ReleaseAudio(duel_.lose_taunt_wav);
}

void BoardGameScreen::UpdateCursorAnimation(float dt) {
    if (!cursor_state_.is_animating) {
        return;
    }

    cursor_state_.frame_accumulator += dt;

    float interval_sec = cursor_state_.animation_interval_ms / 1000.0f;
    if (cursor_state_.frame_accumulator >= interval_sec) {
        cursor_state_.frame_accumulator -= interval_sec;
        cursor_state_.animation_frame = (cursor_state_.animation_frame + 1) % 7;

        // Idle ambient nag rolls right here, inside the animation-frame-advance tick, not on
        // its own schedule — the original drives both the cursor animation and this nag check
        // from the same single timer callback. Bails if the move sound or a previous nag pick
        // is still playing, matching the real busy-slot checks. Simplification vs ASM: the
        // real code picks the clip once per roll and rolls "play now?" separately each tick (2
        // RNG calls); here both happen together at play time — same ~1/15-per-tick observable
        // result.
        const bool move_busy = audio_.isPlaying(portrait_.move_wav);
        const bool ambient_busy = ambient_nag_wav_ != 0 && audio_.isPlaying(ambient_nag_wav_);
        // The real eligibility check (UpdateMoving()'s movement-available gate) tests the
        // same slot the portal-blocked audio plays into, and bails without rolling if it's
        // occupied — the ambient nag is structurally blocked for the whole duration STOP.WAV
        // plays, not just cut once at trigger time. Same check against Khufu's blindness
        // line's own slot, blocked for its whole duration too.
        const bool portal_blocked_busy = audio_.isPlaying(portal_blocked_wav_);
        const bool khufu_blindness_busy =
            portrait_.curse_blindness_wav != 0 && audio_.isPlaying(portrait_.curse_blindness_wav);
        // current_state_==Moving is checked explicitly, not just is_animating: the flag can
        // leak true past Moving via some exit paths, but the original only ever drove this
        // timer from the movement screen's own window — portrait/duel/minimap/etc. are each
        // their own separate window and never got this tick.
        if (current_state_ == BoardGameState::Moving && !move_busy && !ambient_busy &&
            !portal_blocked_busy && !khufu_blindness_busy && RngNext() * 15u / 0x8000u == 0) {
            const int pick = RollIndex(RngNext(), 12);
            ambient_nag_wav_ = ambient_nag_wavs_[pick];
            if (ambient_nag_wav_) {
                audio_.playLoaded(ambient_nag_wav_, AudioFlags::Async);
            }
        }
    }
}

uint16_t BoardGameScreen::RngNext() {
    rng_seed_ = (rng_seed_ * 0x015A4E35UL) + 1;
    return static_cast<uint16_t>((rng_seed_ >> 16) & 0x7FFF);
}

}  // namespace openfear::game
