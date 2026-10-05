#include "openfear/game/FocusIndicator.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace openfear::game {

namespace {

// Shared ~1s sine-wave phase for every focus pulse/tint/fill below: raw
// wall-clock cycle by default (anchor_ms < 0), or anchored to peak
// brightness (pi/2) at a given steady_clock ms timestamp — so landing on a
// new target after a fast scan doesn't start mid-fade.
float FocusPulsePhase(long long anchor_ms) {
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    if (anchor_ms >= 0) {
        const long long elapsed_ms = now_ms - anchor_ms;
        return (static_cast<float>(elapsed_ms % 1000) / 1000.0f) * 2.0f * 3.14159265f + 1.57079633f;
    }
    return (static_cast<float>(now_ms % 1000) / 1000.0f) * 2.0f * 3.14159265f;
}

// Shared alpha ramp for the flat-rect pulse look (DrawFocusPulse and
// DrawFocusPolygonFill's per-scanline fill) - 0..25 by default.
uint8_t PulseAlpha(long long anchor_ms, float intensity) {
    const float phase = FocusPulsePhase(anchor_ms);
    const float glow = 0.5f + 0.5f * std::sin(phase);  // 0..1
    return static_cast<uint8_t>(std::clamp(glow * 25.0f * intensity, 0.0f, 255.0f));
}

}  // namespace

void DrawFocusPulse(IRenderer& renderer, const SpriteInfo& sprite, int dst_x, int dst_y, int src_x,
                    int src_y, int w, int h, long long focus_change_anchor_ms, float intensity) {
    // ADD-mode flat rect, not BLEND: BLEND washes every pixel toward white
    // uniformly regardless of its own color; ADD-mode at a low alpha
    // ceiling stays visible on dark art (e.g. PickCharScreen's near-black
    // background) without washing to white.
    (void)sprite;
    (void)src_x;
    (void)src_y;
    const uint8_t alpha = PulseAlpha(focus_change_anchor_ms, intensity);
    renderer.fillRectAdditive(dst_x, dst_y, w, h, 255, 255, 255, alpha);
}

void DrawFocusTint(IRenderer& renderer, const SpriteInfo& sprite, int dst_x, int dst_y, int src_x,
                   int src_y, int w, int h, long long focus_change_anchor_ms, float intensity) {
    // Dark half: multiplicative tint (darkens toward black). Bright half:
    // ADD-mode (dst += sprite*alpha, clamped) instead of BLEND, so each
    // pixel brightens by its own color rather than washing toward white —
    // reads as the sprite itself glowing.
    const float phase = FocusPulsePhase(focus_change_anchor_ms);
    const float s = std::sin(phase);  // -1..1
    if (s < 0.0f) {
        const float darken = std::clamp(0.4f * intensity, 0.0f, 1.0f);
        const uint8_t v =
            static_cast<uint8_t>(std::clamp((1.0f + s * darken), 0.0f, 1.0f) * 255.0f);
        renderer.drawSpriteTinted(sprite, dst_x, dst_y, src_x, src_y, w, h, v, v, v);
    } else {
        renderer.drawSprite(sprite, dst_x, dst_y, src_x, src_y, w, h);
        const uint8_t alpha =
            static_cast<uint8_t>(std::clamp(s * 110.0f * intensity, 0.0f, 255.0f));
        renderer.drawSpriteAdditive(sprite, dst_x, dst_y, src_x, src_y, w, h, alpha);
    }
}

void DrawFocusPolygonFill(IRenderer& renderer, const std::vector<Point>& verts,
                          long long focus_change_anchor_ms, float intensity) {
    if (verts.size() < 3) return;
    const uint8_t alpha = PulseAlpha(focus_change_anchor_ms, intensity);

    int16_t min_y = verts[0].y, max_y = verts[0].y;
    for (const auto& v : verts) {
        min_y = std::min(min_y, v.y);
        max_y = std::max(max_y, v.y);
    }

    for (int16_t y = min_y; y <= max_y; ++y) {
        std::vector<int> xs;
        for (size_t i = 0; i < verts.size(); ++i) {
            const Point& a = verts[i];
            const Point& b = verts[(i + 1) % verts.size()];
            if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y)) {
                const float t = static_cast<float>(y - a.y) / static_cast<float>(b.y - a.y);
                xs.push_back(static_cast<int>(a.x + t * (b.x - a.x)));
            }
        }
        std::sort(xs.begin(), xs.end());
        for (size_t i = 0; i + 1 < xs.size(); i += 2) {
            const int w = xs[i + 1] - xs[i];
            if (w > 0) renderer.fillRectAdditive(xs[i], y, w, 1, 255, 255, 255, alpha);
        }
    }
}

void DrawFocusCursor(IRenderer& renderer, int cx, int cy) {
    renderer.setGamepadCursorPosition(cx, cy);
}

int FindNextFocus(const std::vector<Point>& centers, int current, int dx, int dy) {
    if (current < 0 || current >= static_cast<int>(centers.size())) return current;
    const float ddx = static_cast<float>(dx), ddy = static_cast<float>(dy);
    const float dlen = std::sqrt(ddx * ddx + ddy * ddy);
    if (dlen < 0.001f) return current;
    const float ndx = ddx / dlen, ndy = ddy / dlen;

    int best = -1;
    float best_score = -1.0f;
    for (int i = 0; i < static_cast<int>(centers.size()); ++i) {
        if (i == current) continue;
        const float vx = static_cast<float>(centers[i].x - centers[current].x);
        const float vy = static_cast<float>(centers[i].y - centers[current].y);
        const float vlen = std::sqrt(vx * vx + vy * vy);
        if (vlen < 0.001f) continue;
        const float dot = (vx * ndx + vy * ndy) / vlen;  // cosine similarity to requested direction
        if (dot <= 0.3f) continue;                       // reject outside a ~72-degree cone
        const float score = dot / vlen;                  // closer + better-aligned wins
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    return (best >= 0) ? best : current;
}

void StickRepeater::Poll(float stick_x, float stick_y, float dt, int& out_dx, int& out_dy,
                         float repeat_rate, bool repeat) {
    out_dx = 0;
    out_dy = 0;
    const float ax = std::fabs(stick_x), ay = std::fabs(stick_y);
    const float mag = std::max(ax, ay);  // dominant-axis magnitude, matches a physical D-pad's
                                         // inability to press two buttons on the same axis
    if (mag < kReleaseAt) {
        armed_ = true;
        timer_ = 0.0f;
        held_time_ = 0.0f;
        return;
    }
    if (mag < kDeadzone) return;  // between release/deadzone thresholds: neither fires nor re-arms

    held_time_ += dt;  // accumulates the whole time held past the deadzone, first press included

    if (armed_) {
        armed_ = false;  // fires below with zero delay, same call
    } else if (!repeat) {
        return;  // already fired this hold, no repeat until release re-arms
    } else {
        timer_ -= dt;
        if (timer_ > 0.0f) return;
    }
    // Acceleration: repeat interval shrinks the longer the stick's been
    // held, floored at 20% of the base rate so it stays controllable.
    const float accel = 1.0f + held_time_ * 2.0f;
    timer_ = std::max(repeat_rate * 0.2f, repeat_rate / accel);

    if (ax >= ay)
        out_dx = (stick_x > 0.0f) ? 1 : -1;
    else
        out_dy = (stick_y > 0.0f) ? 1 : -1;
}

}  // namespace openfear::game
