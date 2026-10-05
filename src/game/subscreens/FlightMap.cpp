#include "openfear/game/subscreens/FlightMap.h"
#include "openfear/game/BoardGameScreen.h"
#include "openfear/game/GameState.h"
#include <chrono>
#include <cmath>

namespace openfear::game {

void FlightMap::LoadAssets() {
    if (bg_tex_ != 0) return;  // already loaded this turn
    // FlightMap question-overlay graphic ("m1\flight" = M1/FLIGHT.BMP),
    // used as the province-picker background.
    bg_tex_ = assets_.texture("M1", "FLIGHT");

    // FlightMap's 6 province-picker zones — hardcoded, read directly from
    // the original's data segment (not a .BIN file).
    zones_ = {{
        {60, 110, 250, 235},   // province 0
        {410, 96, 610, 236},   // province 1
        {268, 334, 378, 470},  // province 2
        {48, 243, 235, 376},   // province 3
        {412, 248, 594, 365},  // province 4
        {257, 6, 374, 134},    // province 5
    }};
}

void FlightMap::FreeAssets() {
    ReleaseTexture(bg_tex_);
}

void FlightMap::Enter() {
    LoadAssets();
    focus_ = 0;
    selected_ = -1;
    board_.current_state_ = BoardGameState::FlightMap;
    board_.state_start_time_ = std::chrono::steady_clock::now();
    board_.state_action_executed_ = false;
}

void FlightMap::UpdateGamepadFocus(const IInput& input) {
    const int flight_focus_before = focus_;
    const auto& gp = input.gamepad();
    // Wedges share one center, so angle alone can't disambiguate 2 adjacent
    // provinces — D-pad instead walks this fixed ring, picking whichever
    // neighbor best matches the pressed direction.
    // Ring order: 5(top)-1(top-right)-4(bottom-right)-2(bottom)-3(bottom-left)-0(top-left).
    static constexpr int kFlightRing[6] = {5, 1, 4, 2, 3, 0};
    int dpad_dx = 0, dpad_dy = 0;
    if (gp.pressed[static_cast<size_t>(GamepadButton::DpadLeft)]) dpad_dx = -1;
    if (gp.pressed[static_cast<size_t>(GamepadButton::DpadRight)]) dpad_dx = 1;
    if (gp.pressed[static_cast<size_t>(GamepadButton::DpadUp)]) dpad_dy = -1;
    if (gp.pressed[static_cast<size_t>(GamepadButton::DpadDown)]) dpad_dy = 1;
    if (dpad_dx != 0 || dpad_dy != 0) {
        int ring_idx = 0;
        for (int i = 0; i < 6; ++i)
            if (kFlightRing[i] == focus_) {
                ring_idx = i;
                break;
            }
        const int prov_prev = kFlightRing[(ring_idx + 5) % 6];
        const int prov_next = kFlightRing[(ring_idx + 1) % 6];
        auto rect_center = [this](int prov) {
            const auto& z = zones_[prov];
            return Point{static_cast<int16_t>((z.x1 + z.x2) / 2),
                         static_cast<int16_t>((z.y1 + z.y2) / 2)};
        };
        const Point cur_c = rect_center(focus_);
        const Point prev_c = rect_center(prov_prev);
        const Point next_c = rect_center(prov_next);
        const float pdx = static_cast<float>(dpad_dx), pdy = static_cast<float>(dpad_dy);
        const float dot_prev = (prev_c.x - cur_c.x) * pdx + (prev_c.y - cur_c.y) * pdy;
        const float dot_next = (next_c.x - cur_c.x) * pdx + (next_c.y - cur_c.y) * pdy;
        const int chosen = (dot_next >= dot_prev) ? prov_next : prov_prev;
        // Press axis must dominate the chosen neighbor's offset, else no-op
        // instead of a wrong-axis jump.
        const auto& chosen_c = rect_center(chosen);
        const float off_x = static_cast<float>(chosen_c.x - cur_c.x);
        const float off_y = static_cast<float>(chosen_c.y - cur_c.y);
        const bool horizontal_press = (dpad_dx != 0 && dpad_dy == 0);
        const bool vertical_press = (dpad_dy != 0 && dpad_dx == 0);
        const bool axis_ok = (!horizontal_press && !vertical_press) ||
                             (horizontal_press && std::fabs(off_x) >= std::fabs(off_y)) ||
                             (vertical_press && std::fabs(off_y) > std::fabs(off_x));
        if (axis_ok) focus_ = chosen;
    }
    // Stick: absolute compass direction from the hex center, selects that
    // province directly. Continuous while held, gated on no D-pad press.
    if (dpad_dx == 0 && dpad_dy == 0) {
        constexpr float kHexCenterX = 325.0f, kHexCenterY = 238.0f;
        constexpr float kStickDeadzone = 0.5f;
        const float mag = std::sqrt(gp.left_x * gp.left_x + gp.left_y * gp.left_y);
        if (mag >= kStickDeadzone) {
            const float stick_angle = std::atan2(gp.left_y, gp.left_x);
            int best_prov = focus_;
            float best_diff = 1e9f;
            for (int p = 0; p < 6; ++p) {
                const auto& z = zones_[p];
                const float cx = (z.x1 + z.x2) / 2.0f - kHexCenterX;
                const float cy = (z.y1 + z.y2) / 2.0f - kHexCenterY;
                const float prov_angle = std::atan2(cy, cx);
                float diff = std::fabs(stick_angle - prov_angle);
                if (diff > 3.14159265f) diff = 2.0f * 3.14159265f - diff;
                if (diff < best_diff) {
                    best_diff = diff;
                    best_prov = p;
                }
            }
            focus_ = best_prov;
        }
    }
    if (focus_ != flight_focus_before) {
        focus_change_time_ = std::chrono::steady_clock::now();
    }
    if (gp.pressed[static_cast<size_t>(GamepadButton::A)]) {
        selected_ = focus_;
    }
}

void FlightMap::update(const IInput& input) {
    // Clicked zone commits the teleport THEN plays FLIGHT{province}.AVI.
    if (!board_.state_action_executed_) {
        board_.state_action_executed_ = true;
        selected_ = -1;
        focus_change_time_ = std::chrono::steady_clock::now();
        static const char* kFlightOpenWav[2] = {"AT_0589", "AT_0590"};
        open_wav_ =
            playWav(std::string("W6\\") + kFlightOpenWav[RollIndex(board_.RngNext(), 2)] + ".WAV",
                    AudioFlags::Async);
    }

    if (selected_ < 0) {
        if (board_.gamepad_mode_) {
            UpdateGamepadFocus(input);
        }
        const auto& fm_mouse = input.mouse();
        if (selected_ < 0 && fm_mouse.clicked[static_cast<int>(MouseButton::Left)]) {
            for (int i = 0; i < 6; ++i) {
                const auto& z = zones_[i];
                if (PointInRect(fm_mouse.x, fm_mouse.y, z)) {
                    selected_ = i;
                    break;
                }
            }
        }

        if (selected_ >= 0) {
            if (open_wav_) audio_.stop(open_wav_);
            open_wav_ = 0;
            board_.render_state_[board_.current_player_].province =
                static_cast<uint16_t>(selected_);
            board_.render_state_[board_.current_player_].stone_id = 0;
            GameState* gs_fl = GameStateManager::Instance().Get();
            if (gs_fl && gs_fl->IsValid()) {
                gs_fl->player_idx = static_cast<uint16_t>(selected_);
                gs_fl->sub_idx = 0;
            }
            board_.AutoOrientDirection(static_cast<uint16_t>(selected_), 0);
            const std::string flight_avi =
                AssetPaths::avi("AV2", std::string("FLIGHT") + std::to_string(selected_)).string();
            board_.video_.close();
            board_.video_loaded_ = false;
            // Same board-area rect as FULLMAP.AVI/GKLOOP1 everywhere else — NOT fullscreen.
            board_.video_.setRenderRect(BoardGameScreen::kBoardAreaX, BoardGameScreen::kBoardAreaY,
                                        BoardGameScreen::kBoardAreaW, BoardGameScreen::kBoardAreaH);
            if (board_.video_.open(flight_avi)) {
                board_.video_.play(false);
            }
        }
    } else if (board_.video_.status() != VideoStatus::Playing) {
        // steps_remaining already took its -1 when F was pressed, untouched here.
        // Resumes Moving the same way the tunnel-entry teleport does (same
        // type-6 stone, MidRollContinue), not a jump to TurnEnd.
        board_.video_.close();
        board_.video_loaded_ = false;
        board_.ReopenFullmapAvi();
        board_.current_state_ = BoardGameState::Moving;
        board_.state_action_executed_ = true;
        board_.state_start_time_ = std::chrono::steady_clock::now();
    }
}

void FlightMap::render() {
    if (selected_ >= 0 && board_.video_.status() == VideoStatus::Playing) {
        board_.video_.renderFrame();
    } else if (bg_tex_) {
        renderer_.drawBitmap(bg_tex_, 0, 0, 0, 0, 640, 480);
    } else {
        renderer_.fillRect(0, 0, 640, 480, 10, 5, 30);
    }

    // Gamepad focus indicator. M1/FLIGHT.BMP is a hexagon of 6 wedges, not rects — a rect
    // border would clip or bleed into neighbors. Vertices pixel-measured off the asset (top
    // edge mirrored for bottom), cross-checked against zones_' click rects below (each sits
    // inside its own wedge).
    if (board_.gamepad_mode_ && selected_ < 0 && focus_ >= 0 && focus_ < 6) {
        // Wedges are hexagons/heptagons, not quads — inner edge bends at the center hex
        // hole. Side wedges (0/1/3/4) have diagonal outer edges; top(5)/bottom(2) end in
        // a point. ~2-3px gap between neighbors is deliberate, matching the art's border.
        static const std::vector<Point> kFlightWedges[6] = {
            {{194, 23}, {291, 192}, {263, 239}, {66, 239}},    // 0 — top-left
            {{449, 23}, {574, 239}, {378, 239}, {350, 194}},   // 1 — top-right/right
            {{293, 293}, {352, 293}, {446, 461}, {196, 461}},  // 2 — bottom
            {{68, 244}, {263, 244}, {292, 290}, {193, 461}},   // 3 — bottom-left/left
            {{352, 292}, {380, 244}, {574, 244}, {448, 459}},  // 4 — bottom-right/right
            {{196, 23}, {446, 23}, {350, 190}, {292, 190}},    // 5 — top
        };
        const auto anchor_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   focus_change_time_.time_since_epoch())
                                   .count();
        DrawFocusPolygonFill(renderer_, kFlightWedges[focus_], anchor_ms, 3.0f);

        int centroid_x = 0, centroid_y = 0;
        for (const auto& v : kFlightWedges[focus_]) {
            centroid_x += v.x;
            centroid_y += v.y;
        }
        const int n = static_cast<int>(kFlightWedges[focus_].size());
        DrawFocusCursor(renderer_, centroid_x / n, centroid_y / n);
    }
}

}  // namespace openfear::game
