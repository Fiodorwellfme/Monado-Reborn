// [XCDE] T259: one quality policy for the launcher and the runtime. Applying is explicit;
// loading an old INI must never silently overwrite the user's individual choices.
#pragma once
#include "settings.h"
#include <string.h>

enum { QUALITY_BALANCED = 0, QUALITY_QUALITY, QUALITY_HANDHELD, QUALITY_PERFORMANCE, QUALITY_POTATO, QUALITY_CUSTOM, QUALITY_COUNT };
enum { EFFECT_TSSAO = 1, EFFECT_SSR = 2, EFFECT_BLOOM = 4, EFFECT_GODRAY = 8, EFFECT_CLOUD_SHADOW = 16 };
typedef struct {
    int resolution, upscaler, aa, min_h, max_h, upscale_quality, grass_lod;
    bool automatic, dof_off, blur, taa_off, frame_gen;
    unsigned effects;
    uint64_t graphics_packed;
} XcdeQuality;
static inline bool xcde_quality_is_performance(int p) {
    return p == QUALITY_HANDHELD || p == QUALITY_PERFORMANCE || p == QUALITY_POTATO;
}
static inline int xcde_quality_parse(const char *name) {
    return !strcmp(name, "quality") ? QUALITY_QUALITY : !strcmp(name, "handheld") ? QUALITY_HANDHELD :
           !strcmp(name, "performance") ? QUALITY_PERFORMANCE : !strcmp(name, "potato") ? QUALITY_POTATO :
           !strcmp(name, "custom") ? QUALITY_CUSTOM : QUALITY_BALANCED;
}
static inline const char *xcde_quality_name(int p) {
    static const char *names[] = {"balanced", "quality", "handheld", "performance", "potato", "custom"};
    return names[p >= 0 && p < QUALITY_COUNT ? p : 0];
}
static inline GraphicsOptions xcde_quality_graphics(int preset) {
    GraphicsOptions g = gfx_defaults();
    if (preset == QUALITY_BALANCED) {
        g.value[GFX_FILTERING] = 4;    // 16x anisotropic filtering
    } else if (preset == QUALITY_QUALITY) {
        g.value[GFX_SHADOW_MASK] = 1;  // full-resolution shadow mask
        g.value[GFX_SHADOW_MAPS] = 1;  // 2048 px shadow maps
        g.value[GFX_PROP_LOD] = 3;     // Ultra (2x)
        g.value[GFX_FILTERING] = 4;    // 16x anisotropic filtering
    } else if (preset == QUALITY_HANDHELD) {
        g.value[GFX_SHADOW_MASK] = 0;  // original half-res shadow mask
        g.value[GFX_FILTERING] = 3;    // 8x anisotropic filtering
        g.value[GFX_REFLECTIONS] = 1;  // Off
        g.value[GFX_GODRAYS] = 1;      // Off
    } else if (preset == QUALITY_PERFORMANCE) {
        g.value[GFX_SHADOW_MASK] = 0;  // original half-res shadow mask
        g.value[GFX_PROP_LOD] = 1;     // Low (0.5x)
        g.value[GFX_CLOUD] = 1;        // Off
        g.value[GFX_REFLECTIONS] = 1;  // Off
        g.value[GFX_GODRAYS] = 1;      // Off
    } else if (preset == QUALITY_POTATO) {
        g.value[GFX_SHADOWS] = 1;      // Off
        g.value[GFX_SHADOW_MASK] = 0;  // original
        g.value[GFX_CLOUD] = 1;        // Off
        g.value[GFX_AO] = 1;           // Off
        g.value[GFX_REFLECTIONS] = 1;  // Off
        g.value[GFX_BLOOM] = 1;        // Off
        g.value[GFX_GODRAYS] = 1;      // Off
        g.value[GFX_SHAFTS] = 1;       // Off
        g.value[GFX_MOTION_BLUR] = 1;  // Off
        g.value[GFX_FUR_BLUR] = 1;     // Off
        g.value[GFX_PROP_LOD] = 1;     // Low (0.5x)
    }
    return g;
}
static inline XcdeQuality xcde_quality_values(int preset) {
    XcdeQuality q = {0};
    q.resolution = RES_MATCH;
    q.upscaler = UPSCALE_FSR1;
    q.automatic = true;
    q.upscale_quality = UQ_QUALITY;
    q.blur = true;
    q.grass_lod = GRASS_DEFAULT;
    if (preset == QUALITY_QUALITY) {
        q.upscale_quality = UQ_NATIVE;
        q.grass_lod = GRASS_ULTRA;
    } else if (preset == QUALITY_HANDHELD) {
        q.resolution = RES_DYNAMIC;
        q.upscaler = UPSCALE_FSR3;
        q.automatic = false;
        q.upscale_quality = UQ_QUALITY;
        q.min_h = 540;
        q.max_h = 720;
        q.grass_lod = GRASS_ORIGINAL;
    } else if (preset == QUALITY_PERFORMANCE) {
        q.resolution = RES_DYNAMIC;
        q.upscaler = UPSCALE_FSR3;
        q.automatic = false;
        q.upscale_quality = UQ_PERFORMANCE;
        q.blur = false;
        q.dof_off = true;
        q.min_h = 540;
        q.max_h = 720;
        q.grass_lod = GRASS_ORIGINAL;
    } else if (preset == QUALITY_POTATO) {
        q.resolution = RES_DYNAMIC;
        q.automatic = false;
        q.upscale_quality = UQ_ULTRA;
        q.blur = false;
        q.dof_off = true;
        q.taa_off = true;
        q.min_h = 360;
        q.max_h = 540;
        q.grass_lod = GRASS_LOW;
    }
    GraphicsOptions g = xcde_quality_graphics(preset);
    q.graphics_packed = gfx_pack(&g);
    return q;
}
static inline XcdeQuality xcde_quality_current(const Settings *s) {
    XcdeQuality q = {0};
    q.resolution=s->res_mode; q.upscaler=s->upscaler; q.aa=s->antialias;
    q.min_h=s->dynamic_min_height; q.max_h=s->dynamic_max_height;
    q.upscale_quality=s->upscale_quality; q.grass_lod=grass_lod_valid(s->grass_lod);
    q.automatic=s->upscaler_auto; q.dof_off=s->dof_off; q.blur=s->game_blur; q.taa_off=s->taa_off;
    q.frame_gen=s->frame_gen || s->cutscene_mode==2; q.effects=s->effects_off;
    q.graphics_packed=gfx_pack(&s->graphics);
    return q;
}
static inline bool xcde_quality_equal(XcdeQuality a, XcdeQuality b) {
    bool upscaler_same = (a.automatic && b.automatic) || a.upscaler == b.upscaler;
    return a.resolution==b.resolution && upscaler_same && a.aa==b.aa && a.min_h==b.min_h &&
           a.max_h==b.max_h && a.automatic==b.automatic && a.dof_off==b.dof_off && a.blur==b.blur &&
           a.taa_off==b.taa_off && a.frame_gen==b.frame_gen && a.effects==b.effects &&
           a.upscale_quality==b.upscale_quality && a.grass_lod==b.grass_lod &&
           a.graphics_packed==b.graphics_packed;
}
static inline void xcde_quality_apply(Settings *s, int p) {
    if (p < 0 || p >= QUALITY_CUSTOM) return;
    XcdeQuality q=xcde_quality_values(p);
    s->quality_preset=p; s->gpu_performance=xcde_quality_is_performance(p);
    s->res_mode=q.resolution; s->upscaler=q.upscaler; s->antialias=q.aa;
    s->dynamic_min_height=q.min_h; s->dynamic_max_height=q.max_h;
    s->upscale_quality=q.upscale_quality; s->grass_lod=q.grass_lod;
    s->upscaler_auto=q.automatic; s->dof_off=q.dof_off; s->game_blur=q.blur; s->taa_off=q.taa_off;
    s->frame_gen=q.frame_gen; s->effects_off=q.effects;
    s->graphics=xcde_quality_graphics(p);
    if (s->cutscene_mode==2) s->cutscene_mode=1; // restoring any preset also clears cutscene-only generation
}
