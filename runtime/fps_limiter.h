// [XCDE] FPS target validation and RTSS / MangoHud "early" style host frame limiter policy.
// Shared by settings_ini.c, framepace.c, quality_controls.h, tools/launcher/launcher.cpp, and native tests.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { XCDE_FPS_MIN = 15, XCDE_FPS_MAX = 1000 };
enum { FPS_LIMITER_DEFAULT = 0, FPS_LIMITER_EARLY = 1, FPS_LIMITER_COUNT = 2 };

static const char *const xcde_fps_limiter_ini[FPS_LIMITER_COUNT] = {"default", "early"};

static inline int xcde_fps_clamp(int fps) {
    if (fps <= 0) return 0;
    if (fps < XCDE_FPS_MIN) return XCDE_FPS_MIN;
    if (fps > XCDE_FPS_MAX) return XCDE_FPS_MAX;
    return fps;
}

static inline int xcde_fps_parse(const char *v) {
    if (!v || !*v) return 30;
    if (!strcmp(v, "unlimited") || !strcmp(v, "0") || !strcmp(v, "unl\xc3\xadmited") || !strcmp(v, "ilimitado")) return 0;
    char *end = NULL;
    long n = strtol(v, &end, 10);
    if (end == v || *end != '\0' || n < 0) return 30;
    if (n == 0) return 0;
    if (n > XCDE_FPS_MAX) return XCDE_FPS_MAX;
    return xcde_fps_clamp((int)n);
}

static inline int xcde_fps_limiter_valid(int mode) {
    return mode == FPS_LIMITER_EARLY ? FPS_LIMITER_EARLY : FPS_LIMITER_DEFAULT;
}

static inline int xcde_fps_limiter_parse(const char *v) {
    if (v && (!strcmp(v, "early") || !strcmp(v, "rtss") || !strcmp(v, "1"))) return FPS_LIMITER_EARLY;
    return FPS_LIMITER_DEFAULT;
}

// Exact rounded frame period in nanoseconds for a positive integer FPS target.
static inline uint64_t xcde_fps_period_ns(int fps) {
    if (fps <= 0) return 0;
    return (1000000000ull + (uint64_t)fps / 2) / (uint64_t)fps;
}

// Fixed-cadence "early" limiter state (RTSS / MangoHud style):
// - When ahead of schedule, returns the exact nanosecond deadline to sleep+spin until and advances
//   the deadline by one period (zero cumulative wakeup drift).
// - When behind schedule (GPU-bound or hitch), returns 0 (no wait) and re-anchors to now + period
//   so hitches never trigger short burst-catchup frames.
typedef struct {
    uint64_t next_deadline_ns;
    int last_fps;
} XcdeEarlyLimiter;

static inline void xcde_early_limiter_reset(XcdeEarlyLimiter *lim) {
    lim->next_deadline_ns = 0;
    lim->last_fps = 0;
}

static inline uint64_t xcde_early_limiter_step(XcdeEarlyLimiter *lim, uint64_t now_ns, int target_fps) {
    if (target_fps <= 0) {
        xcde_early_limiter_reset(lim);
        return 0;
    }
    uint64_t period_ns = xcde_fps_period_ns(target_fps);
    if (!lim->next_deadline_ns || lim->last_fps != target_fps) {
        lim->last_fps = target_fps;
        lim->next_deadline_ns = now_ns + period_ns;
        return 0;
    }
    if (now_ns < lim->next_deadline_ns) {
        uint64_t wake_ns = lim->next_deadline_ns;
        lim->next_deadline_ns += period_ns;
        return wake_ns;
    }
    lim->next_deadline_ns = now_ns + period_ns;
    return 0;
}

#ifdef __cplusplus
}
#endif
