// [XCDE] Unit tests for custom FPS parsing/clamping and the RTSS / MangoHud "early" style limiter state machine.
#include <assert.h>
#include <stdio.h>
#include "../../runtime/fps_limiter.h"

int main(void) {
    // Clamping bounds (15..1000, 0 = unlimited)
    assert(xcde_fps_clamp(0) == 0);
    assert(xcde_fps_clamp(-5) == 0);
    assert(xcde_fps_clamp(1) == XCDE_FPS_MIN);
    assert(xcde_fps_clamp(14) == XCDE_FPS_MIN);
    assert(xcde_fps_clamp(15) == 15);
    assert(xcde_fps_clamp(45) == 45);
    assert(xcde_fps_clamp(72) == 72);
    assert(xcde_fps_clamp(240) == 240);
    assert(xcde_fps_clamp(1000) == 1000);
    assert(xcde_fps_clamp(1500) == XCDE_FPS_MAX);

    // INI parsing
    assert(xcde_fps_parse("30") == 30);
    assert(xcde_fps_parse("45") == 45);
    assert(xcde_fps_parse("72") == 72);
    assert(xcde_fps_parse("144") == 144);
    assert(xcde_fps_parse("240") == 240);
    assert(xcde_fps_parse("10") == XCDE_FPS_MIN);
    assert(xcde_fps_parse("5000") == XCDE_FPS_MAX);
    assert(xcde_fps_parse("unlimited") == 0);
    assert(xcde_fps_parse("0") == 0);
    assert(xcde_fps_parse("ilimitado") == 0);
    assert(xcde_fps_parse("60abc") == 30);
    assert(xcde_fps_parse("") == 30);
    assert(xcde_fps_parse("-10") == 30);

    // Limiter mode parsing
    assert(xcde_fps_limiter_parse("default") == FPS_LIMITER_DEFAULT);
    assert(xcde_fps_limiter_parse("guest") == FPS_LIMITER_DEFAULT);
    assert(xcde_fps_limiter_parse("early") == FPS_LIMITER_EARLY);
    assert(xcde_fps_limiter_parse("rtss") == FPS_LIMITER_EARLY);
    assert(xcde_fps_limiter_parse("1") == FPS_LIMITER_EARLY);
    assert(xcde_fps_limiter_parse("bogus") == FPS_LIMITER_DEFAULT);
    assert(xcde_fps_limiter_valid(FPS_LIMITER_EARLY) == FPS_LIMITER_EARLY);
    assert(xcde_fps_limiter_valid(99) == FPS_LIMITER_DEFAULT);

    // Early limiter fixed-cadence state machine
    XcdeEarlyLimiter lim = {0};
    uint64_t t = 1000000000ull;  // 1.0 s
    uint64_t p60 = xcde_fps_period_ns(60);
    assert(p60 == 16666667ull);

    // Frame 0: anchor only, no wait
    assert(xcde_early_limiter_step(&lim, t, 60) == 0);
    assert(lim.next_deadline_ns == t + p60);

    // Frame 1: arrives 5 ms later -> waits until exact t + p60, next deadline advances to t + 2*p60
    uint64_t w1 = xcde_early_limiter_step(&lim, t + 5000000ull, 60);
    assert(w1 == t + p60);
    assert(lim.next_deadline_ns == t + 2 * p60);

    // Frame 2: woke 25 us late, work took 4 ms -> still targets exact t + 2*p60 (no cumulative drift)
    uint64_t w2 = xcde_early_limiter_step(&lim, w1 + 25000ull + 4000000ull, 60);
    assert(w2 == t + 2 * p60);
    assert(lim.next_deadline_ns == t + 3 * p60);

    // Frame 3: hitch (arrives 10 ms past deadline) -> no wait, re-anchors to now + p60 (no burst catch-up)
    uint64_t hitch_now = t + 3 * p60 + 10000000ull;
    assert(xcde_early_limiter_step(&lim, hitch_now, 60) == 0);
    assert(lim.next_deadline_ns == hitch_now + p60);

    // Live FPS change to custom 72 FPS -> re-anchors cleanly
    uint64_t p72 = xcde_fps_period_ns(72);
    uint64_t t_switch = hitch_now + 5000000ull;
    assert(xcde_early_limiter_step(&lim, t_switch, 72) == 0);
    assert(lim.next_deadline_ns == t_switch + p72);
    assert(xcde_early_limiter_step(&lim, t_switch + 2000000ull, 72) == t_switch + p72);

    // Unlimited (0) -> resets state and never waits
    assert(xcde_early_limiter_step(&lim, t_switch + 20000000ull, 0) == 0);
    assert(lim.next_deadline_ns == 0 && lim.last_fps == 0);

    puts("fps limiter: custom FPS bounds/parsing and early fixed-cadence pacing OK");
    return 0;
}
