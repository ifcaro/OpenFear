#include "openfear/game/subscreens/MinimapScreen.h"
#include "openfear/game/BoardGameScreen.h"
#include <chrono>
#include <cstdio>

namespace openfear::game {

void MinimapScreen::LoadProvincePoints() {
    for (int i = 0; i < 8; ++i) {
        auto pnt_path = AssetPaths::data("DATA", "MAP" + std::to_string(i), "PNT").string();
        if (auto pts = assets::load_pnt_file(assets_.provider(), pnt_path))
            province_pnt_[i] = std::move(*pts);
    }
}

void MinimapScreen::Enter(BoardGameState return_state) {
    return_state_ = return_state;
    saved_start_time_ = board_.state_start_time_;
    saved_action_executed_ = board_.state_action_executed_;
    board_.current_state_ = BoardGameState::MinimapScreen;
    board_.state_start_time_ = std::chrono::steady_clock::now();
    board_.state_action_executed_ = false;

    ReleaseTexture(tex_);
    const int prov = board_.render_state_[board_.current_player_].province;
    if (prov >= 0 && prov < 8) {
        char map_name[8];
        std::snprintf(map_name, sizeof(map_name), "MAP0%d", prov);
        tex_ = assets_.misc_texture(map_name);
    }

    // Picks 1 of a 2-clip pool right after loading the bitmap.
    static const char* kMinimapOpenWav[2] = {"W9\\AT_0739.WAV", "W9\\AT_0740.WAV"};
    const int minimap_wav_idx = RollIndex(board_.RngNext(), 2);
    open_wav_ = playWav(kMinimapOpenWav[minimap_wav_idx], AudioFlags::Async);
}

bool MinimapScreen::update(const IInput& input) {
    const auto& mouse = input.mouse();
    // Gamepad: B closes it (port-only UX — X opened it).
    const bool gp_close =
        board_.gamepad_mode_ && input.gamepad().pressed[static_cast<size_t>(GamepadButton::B)];
    if (mouse.clicked[static_cast<int>(MouseButton::Left)] || gp_close) {
        board_.current_state_ = return_state_;
        board_.state_start_time_ = saved_start_time_;
        board_.state_action_executed_ =
            saved_action_executed_;  // restore so Moving init block does not re-run
        ReleaseTexture(tex_);
        if (open_wav_) audio_.stop(open_wav_);
        open_wav_ = 0;
    }
    return false;
}

void MinimapScreen::render() {
    const uint16_t prov = board_.render_state_[board_.current_player_].province;
    const uint16_t stone = board_.render_state_[board_.current_player_].stone_id;
    if (tex_) {
        renderer_.drawBitmap(tex_, 0, 0, 0, 0, 640, 480);
    } else {
        renderer_.fillRect(0, 0, 640, 480, 0, 0, 0);
    }

    // Player position marker: 22x22 hollow blinking rectangle from PNT stone coordinates
    if (prov < 8 && stone < static_cast<uint16_t>(province_pnt_[prov].size())) {
        const auto& pt = province_pnt_[prov][stone];
        const int cx = static_cast<int>(pt.x);
        const int cy = static_cast<int>(pt.y);
        const int rx = cx - 11;  // center 22x22 on stone point
        const int ry = cy - 11;

        constexpr uint8_t oR = 0xDA, oG = 0x86, oB = 0x28;  // orange
        constexpr uint8_t rR = 0xC8, rG = 0x00, rB = 0x00;  // red

        // Blink: middle ring alternates red <-> orange every 200ms.
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count();
        const bool blink_red = (now_ms / 200) % 2 == 0;
        const uint8_t mR = blink_red ? rR : oR;
        const uint8_t mG = blink_red ? rG : oG;
        const uint8_t mB = blink_red ? rB : oB;

        // Outer orange ring (1px) - rows 0,21; cols 0,21 (excl. corners done by rows)
        renderer_.fillRect(rx, ry, 22, 1, oR, oG, oB);           // top row
        renderer_.fillRect(rx, ry + 21, 22, 1, oR, oG, oB);      // bottom row
        renderer_.fillRect(rx, ry + 1, 1, 20, oR, oG, oB);       // left col
        renderer_.fillRect(rx + 21, ry + 1, 1, 20, oR, oG, oB);  // right col

        // Middle ring (2px) - blinks red <-> orange
        renderer_.fillRect(rx + 1, ry + 1, 20, 2, mR, mG, mB);   // top 2 rows
        renderer_.fillRect(rx + 1, ry + 19, 20, 2, mR, mG, mB);  // bottom 2 rows
        renderer_.fillRect(rx + 1, ry + 3, 2, 16, mR, mG, mB);   // left 2 cols
        renderer_.fillRect(rx + 19, ry + 3, 2, 16, mR, mG, mB);  // right 2 cols

        // Inner orange ring (1px) - row 3 and row 18; col 3 and col 18 (middle strip)
        renderer_.fillRect(rx + 3, ry + 3, 16, 1, oR, oG, oB);   // top inner row
        renderer_.fillRect(rx + 3, ry + 18, 16, 1, oR, oG, oB);  // bottom inner row
        renderer_.fillRect(rx + 3, ry + 4, 1, 14, oR, oG, oB);   // left inner col
        renderer_.fillRect(rx + 18, ry + 4, 1, 14, oR, oG, oB);  // right inner col
    }
}

void MinimapScreen::unload() {
    ReleaseTexture(tex_);
    for (int i = 0; i < 8; ++i) {
        province_pnt_[i].clear();
    }
}

void MinimapScreen::FreeAssets() {
    ReleaseTexture(tex_);
    if (open_wav_) audio_.stop(open_wav_);
    open_wav_ = 0;
}

}  // namespace openfear::game
