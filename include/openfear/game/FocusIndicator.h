#pragma once
#include "../platform/IRenderer.h"
#include <vector>

namespace openfear::game {

// Shared gamepad-focus indicator + navigation helpers. Every screen's
// gamepad UX is discrete D-pad navigation between known targets (jump, not
// steer) — these helpers draw the highlight and pick the next target;
// platform::Cursor::SetGamepadFocus() is the low-level primitive
// DrawFocusCursor calls to park the real cursor sprite on it.

// Overlays a pulsing translucent white rect (~1s sine wave, alpha 0..25 by
// default) on top of the target's already-drawn content. Call in addition
// to (after) the normal draw call.
// `focus_change_anchor_ms`: steady_clock ms timestamp of the last focus
// change, so the pulse peaks right on switch instead of landing mid-fade
// on a fast D-pad/stick scan; -1 (default) just follows the wall clock,
// fine for a single static target.
// `intensity`: scales the alpha ceiling, for targets where the default
// reads too subtle.
void DrawFocusPulse(IRenderer& renderer, const SpriteInfo& sprite, int dst_x, int dst_y, int src_x,
                    int src_y, int w, int h, long long focus_change_anchor_ms = -1,
                    float intensity = 1.0f);

// Tints a sprite crop with a ~1s sine-wave brightness pulse: dark half
// multiplicative (darkens toward black), bright half additive (glows past
// the source, respecting its own alpha/shape). Call once per focused
// target per frame, INSTEAD OF the normal draw call — this function draws
// the crop itself, tinted.
// `focus_change_anchor_ms`: see DrawFocusPulse.
// `intensity`: scales how far the pulse swings each way; pass higher for
// targets whose own content is already animating (drowns out the default
// swing otherwise).
void DrawFocusTint(IRenderer& renderer, const SpriteInfo& sprite, int dst_x, int dst_y, int src_x,
                   int src_y, int w, int h, long long focus_change_anchor_ms = -1,
                   float intensity = 1.0f);

// Fills an arbitrary closed polygon with the same pulsing white overlay as
// DrawFocusPulse — for click zones with no baked-in button art, where a
// rectangle silhouette reads wrong. Needs at least 3 verts.
// `focus_change_anchor_ms`/`intensity`: see DrawFocusPulse.
void DrawFocusPolygonFill(IRenderer& renderer, const std::vector<Point>& verts,
                          long long focus_change_anchor_ms = -1, float intensity = 1.0f);

// Repositions the shared cursor sprite over a focused target's center
// (cx, cy) and makes it visible for this frame — see
// platform::Cursor::SetGamepadFocus. Call once per frame, in addition to
// DrawFocusPulse/DrawFocusTint/DrawFocusPolygonFill, for every discrete
// focus target — except purely decorative reuses of the tint/pulse look
// with no target to jump to (e.g. DuelAttackGauge's fill-bar tint).
void DrawFocusCursor(IRenderer& renderer, int cx, int cy);

// Given target centers and a current focus index, finds the best next
// index in the requested D-pad direction (dx, dy each -1/0/1, not both
// zero). Rejects candidates outside a ~72-degree cone around the
// requested direction; among the rest, prefers closer + better-aligned
// targets. Returns `current` unchanged if nothing qualifies.
int FindNextFocus(const std::vector<Point>& centers, int current, int dx, int dy);

// Converts a continuous analog stick into discrete D-pad-equivalent step
// events (one step per deliberate push past the deadzone, then an
// auto-repeat delay while held) — so a discrete D-pad-focus screen can
// accept the left stick too with one extra call. Each navigable UI should
// own one persistent instance (state carries between frames).
class StickRepeater {
public:
    // Call every frame with the stick's raw x/y (-1..1) and delta time
    // (seconds). Returns dx/dy in {-1,0,1} (at most one nonzero, same
    // shape as a D-pad press) on the frame a step fires, {0,0} otherwise.
    // Diagonal input picks whichever axis is more deflected — a physical
    // D-pad can't press two directions on one axis at once.
    // `repeat_rate` (seconds between repeats while held): defaults to
    // stick speed; pass a larger value to repeat slower (see kDpadRepeatRate).
    // `repeat=false`: one push past the deadzone fires once, then the
    // stick must return below kReleaseAt to re-arm — for callers where
    // each push is one discrete action (board movement), not a
    // continuous stream (menu scrolling).
    void Poll(float stick_x, float stick_y, float dt, int& out_dx, int& out_dy,
              float repeat_rate = kStickRepeatRate, bool repeat = true);

    // Same as Poll(), for callers that only navigate a single row and have
    // no use for the vertical axis.
    void PollHorizontal(float stick_x, float stick_y, float dt, int& out_dx,
                        float repeat_rate = kStickRepeatRate, bool repeat = true) {
        int dy_unused = 0;
        Poll(stick_x, stick_y, dt, out_dx, dy_unused, repeat_rate, repeat);
    }

    // Same as Poll(), for callers that only navigate a single column and have
    // no use for the horizontal axis.
    void PollVertical(float stick_x, float stick_y, float dt, int& out_dy,
                      float repeat_rate = kStickRepeatRate, bool repeat = true) {
        int dx_unused = 0;
        Poll(stick_x, stick_y, dt, dx_unused, out_dy, repeat_rate, repeat);
    }

    // Feed a D-pad's held[] state through this same class (as if it were a
    // fully-deflected stick: -1/0/1) to get analogous auto-repeat-while-
    // held behavior for digital buttons, which edge-triggered pressed[]
    // alone doesn't provide. Use a SEPARATE instance from the stick's own
    // (different timer state) with kDpadRepeatRate so it repeats slower.
    static constexpr float kDpadRepeatRate = 0.40f;
    static constexpr float kStickRepeatRate =
        0.15f;  // seconds between repeats while held (stick, fast) - also the Poll() default

private:
    static constexpr float kDeadzone = 0.5f;   // must push decisively, not just drift
    static constexpr float kReleaseAt = 0.3f;  // hysteresis: must return below this to re-arm

    bool armed_ = true;   // true = ready to fire on next deadzone crossing
    float timer_ = 0.0f;  // counts down to the next repeat while held past the deadzone
    // Seconds spent continuously past the deadzone; the longer held, the
    // shorter the repeat interval (see Poll's acceleration formula),
    // floored so it never becomes uncontrollably fast. Resets on release.
    float held_time_ = 0.0f;
};

}  // namespace openfear::game
