// [XCDE] T259/T261: identical profile semantics in both frontends.
#pragma once
#include "imgui.h"
#include "responsive.h"
#include "../quality_policy.h"
#include "../graphics_options.h"
#include "../fps_limiter.h"
static inline bool monado_quality_controls(Settings *s, bool en) {
    XcdeQuality before=xcde_quality_current(s); int old_fps=s->fps, old_lim=s->fps_limiter, old_preset=s->quality_preset;
    int p=s->quality_preset;
    if (monado_combo(en ? "Quality preset" : "Perfil de calidad", &p,
                     en ? "Balanced\0Performance\0Potato\0Custom\0" : "Equilibrado\0Rendimiento\0Patata\0Personalizado\0")) {
        if (p==QUALITY_CUSTOM) s->quality_preset=p; else xcde_quality_apply(s,p);
    }
    static bool custom_fps_open = false;
    bool is_preset = s->fps==30 || s->fps==60 || s->fps==0 || s->fps==120 || s->fps==144 || s->fps==165;
    if (!is_preset) custom_fps_open = true;
    int target = custom_fps_open ? 4 : s->fps==30 ? 0 : s->fps==60 ? 1 : !s->fps ? 2 : 3;
    if (monado_combo(en ? "Frame rate target" : "Objetivo de FPS", &target,
                     en ? "30 FPS - steady target\0 60 FPS\0Maximum - uncapped, may fluctuate\0Advanced\0Custom\0" :
                          "30 FPS - objetivo estable\0 60 FPS\0Máximo - sin límite, puede fluctuar\0Avanzado\0Personalizado\0")) {
        if (target<3) { custom_fps_open = false; s->fps=target==0 ? 30 : target==1 ? 60 : 0; }
        else if (target==3) { custom_fps_open = false; if (s->fps!=120 && s->fps!=144 && s->fps!=165) s->fps=120; }
        else { custom_fps_open = true; if (s->fps<=0) s->fps=60; }
    }
    if (target==3) {
        int rate=s->fps==144 ? 1 : s->fps==165 ? 2 : 0;
        if (monado_combo(en ? "Advanced FPS" : "FPS avanzados",&rate,"120\0 144\0 165\0")) {
            const int rates[]={120,144,165}; s->fps=rates[rate];
        }
    } else if (target==4) {
        static int custom_edit = 60;
        static bool editing_fps = false;
        if (!editing_fps) custom_edit = s->fps > 0 ? s->fps : 60;
        bool changed = ImGui::InputInt(en ? "Custom FPS (15-1000)" : "FPS personalizados (15-1000)", &custom_edit, 1, 10);
        editing_fps = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit() || (changed && !editing_fps)) {
            custom_edit = xcde_fps_clamp(custom_edit > 0 ? custom_edit : XCDE_FPS_MIN);
            s->fps = custom_edit;
        }
        ImGui::SetItemTooltip("%s", en ? "Type any integer from 15 to 1000 and press Enter (or use -/+). Applies live."
                                       : "Escribe cualquier valor entre 15 y 1000 y pulsa Intro (o usa -/+). Se aplica al instante.");
    }
    int lim = xcde_fps_limiter_valid(s->fps_limiter);
    if (monado_combo(en ? "FPS limiter" : "Limitador de FPS", &lim,
                     en ? "Default (game limiter)\0Early (RTSS / MangoHud style)\0" :
                          "Predeterminado (limitador del juego)\0Temprano (estilo RTSS / MangoHud)\0")) {
        s->fps_limiter = lim;
    }
    ImGui::SetItemTooltip("%s", en
        ? "Default: uses the game's own limiter.\n"
          "Early: RTSS / MangoHud style host limiter before each frame (fixed-cadence sleep + spin) for flatter frame times and lower latency. Applies live."
        : "Predeterminado: usa el limitador del juego.\n"
          "Temprano: limitador del host estilo RTSS / MangoHud antes de cada frame (espera fija + spin) para tiempos más estables y menor latencia. Se aplica al instante.");
    ImGui::TextWrapped("%s",en ? "Targets depend on hardware. Frame rate changes apply live (F1), no restart needed." :
                       "El objetivo depende del equipo. Los FPS se aplican en vivo (F1), sin reiniciar.");
    if (s->res_mode==RES_DYNAMIC && s->dynamic_max_height)
        ImGui::Text(en ? "Internal range: %d-%dp (capped by output)" : "Rango interno: %d-%dp (limitado por la salida)",s->dynamic_min_height,s->dynamic_max_height);
    if (s->res_mode==RES_DYNAMIC && ImGui::CollapsingHeader(en ? "Dynamic resolution limits" : "Límites de resolución dinamica")) {
        bool changed=ImGui::InputInt(en ? "Minimum height (0: auto)" : "Altura mínima (0: auto)",&s->dynamic_min_height,90);
        changed|=ImGui::InputInt(en ? "Maximum height (0: output)" : "Altura máxima (0: salida)",&s->dynamic_max_height,90);
        if (changed) {
            if (s->dynamic_min_height<0) s->dynamic_min_height=0;
            if (s->dynamic_max_height<0) s->dynamic_max_height=0;
            if (s->dynamic_min_height>4320) s->dynamic_min_height=4320;
            if (s->dynamic_max_height>4320) s->dynamic_max_height=4320;
            if (s->dynamic_max_height && s->dynamic_min_height>s->dynamic_max_height) s->dynamic_min_height=s->dynamic_max_height;
            s->quality_preset=QUALITY_CUSTOM;
        }
    }
    return old_fps!=s->fps || old_lim!=s->fps_limiter || old_preset!=s->quality_preset || !xcde_quality_equal(before,xcde_quality_current(s));
}
// [XCDE] One combo per graphics option (graphics_options.h groups), shared by the launcher and F1: the effects that
// used to have a second, checkbox switch under "Experimental cosmetic reductions" now live only here.
static inline const char *monado_gfx_tooltip(int i, bool en) {
    switch (i) {
    case GFX_SHADOWS: return en ? "Off removes dynamic shadow shading (cloud shadows too); baked shading stays."
                                : "Desactivado quita el sombreado dinámico (también las sombras de nubes); el sombreado precalculado se queda.";
    case GFX_SHADOW_MAPS: return en ? "Cascade shadow-map resolution. 2048 px: four times the original texels."
                                    : "Resolución de los mapas de sombras en cascada. 2048 px: cuatro veces los texels originales.";
    case GFX_CLOUD: return en ? "Shadows Off takes priority over this setting." : "Sombras Desactivado tiene prioridad sobre este ajuste.";
    case GFX_PROP_LOD: return en ? "Intermediate mesh-LOD transitions of recognized trees, ivy and rocks only; final distances stay."
                                 : "Solo las transiciones intermedias de LOD de árboles, hiedra y rocas reconocidos; las distancias finales no cambian.";
    case GFX_AO: return en ? "Original / Off / On: the game's temporal SSAO. Classic: its per-frame SSAO, without the faint trail\n"
                             "behind thin objects (ropes, railings) while the camera moves."
                           : "Original / Desactivado / Activado: el SSAO temporal del juego. Clásica: su SSAO por frame, sin la estela\n"
                             "tenue detrás de objetos finos (cuerdas, barandillas) al mover la cámara.";
    default: return nullptr;
    }
    ImGui::PopID();
}
static inline bool monado_gfx_group(GraphicsOptions *o, const int *ids, int n, bool en, float width = 0) {
    bool changed = false;
    for (int k = 0; k < n; k++) {
        int i = ids[k];
        const char *items[5];
        for (int v = 0; v < gfx_count(i); v++) items[v] = gfx_display_lang(i, v, en);
        if (width > 0) ImGui::SetNextItemWidth(width);
        changed |= monado_combo(gfx_label(i, en), &o->value[i], items, gfx_count(i));
        if (const char *tip = monado_gfx_tooltip(i, en)) ImGui::SetItemTooltip("%s", tip);
    }
    return changed;
}
template <int N> static inline bool monado_gfx_group(GraphicsOptions *o, const int (&ids)[N], bool en, float width = 0) {
    return monado_gfx_group(o, ids, N, en, width);
}
