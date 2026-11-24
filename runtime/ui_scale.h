// [XCDE] GitHub #121: HUD scale. The game lays its HUD out on its 1280x720 frame and every element is its own draw
// (a quad, or one instanced draw per text string) whose vertex constant buffer 3 starts with the element's 2D affine
// transform to clip space (rows a b 0 tx / c d 0 ty, y up). exec.c shrinks each UI draw by s around an anchor picked
// from where the element sits - the screen edge or corner it belongs to, or the centre - by folding that scale into
// the draw's viewport mapping (viewport and scissor follow; the shaders are untouched). Header-only: tests/native.
#pragma once
#include <stdbool.h>
#include <math.h>
#include <stdint.h>

// fraction of the frame, per axis: an element whose near side is within it hugs that edge
#define UI_SCALE_EDGE 0.1f
#define UI_SCALE_MIN 50  // (% : smaller and the HUD's text is no longer readable at 1080p)

// xcde.ini ui_scale (percent) -> a valid setting: 100 (the game's own size) when missing or out of range below 1
static inline int ui_scale_percent(int v) { return v <= 0 ? 100 : v < UI_SCALE_MIN ? UI_SCALE_MIN : v > 100 ? 100 : v; }

// the anchor along one axis for an element spanning [lo, hi] of a frame of the given size. An element near an edge
// shrinks towards it (the minimap, the party list, the arts palette, the hints keep hugging their edge); any other
// shrinks in place, around its own centre, and the pieces of a component that end up in different groups keep their
// spacing. Across the width (centre_line), an element near a side edge and over the middle of the frame shrinks
// around the frame's centre: the arts palette stays centred whatever joined its group this frame (a corner gauge that
// comes and goes). Down the height there is no such rule: the party list reaches past the middle yet hugs the top.
// (ui_scale_hud_anchor adds the top and bottom edges; markers over people and enemies: ui_scale_frame_begin.)
static inline float ui_scale_anchor_axis_(float lo, float hi, float size, bool centre_line) {
    bool near0 = lo < size * UI_SCALE_EDGE, near1 = hi > size * (1.0f - UI_SCALE_EDGE);
    if (centre_line && (near0 || near1) && lo < size * 0.5f && hi > size * 0.5f) return size * 0.5f;
    if (near0 && near1) return size * 0.5f;
    if (near0) return 0;
    if (near1) return size;
    return 0.5f * (lo + hi);
}
static inline float ui_scale_anchor_x(float lo, float hi, float w) { return ui_scale_anchor_axis_(lo, hi, w, true); }
static inline float ui_scale_anchor_y(float lo, float hi, float h) { return ui_scale_anchor_axis_(lo, hi, h, false); }
// both axes of a HUD box. Over the middle of the width and at the top or bottom edge (a banner, the arts palette, a
// subtitle): centred across. A box on the left or right edge goes to a corner: down the height it hugs the top or the
// bottom edge it is near, else the half its top is in - the hints under the minimap, the party list, the "Location"
// popup belong to the top corner whether or not they joined the minimap's group (in place, they jumped as the area
// name came and went). inx / iny: the anchor is the box's own centre.
static inline void ui_scale_hud_anchor(const float b[4], float w, float h, float *ax, float *ay, bool *inx, bool *iny) {
    *ax = ui_scale_anchor_x(b[0], b[2], w);
    *ay = ui_scale_anchor_y(b[1], b[3], h);
    if ((b[1] < h * UI_SCALE_EDGE || b[3] > h * (1 - UI_SCALE_EDGE)) && b[0] < w * 0.5f && b[2] > w * 0.5f) *ax = w * 0.5f;
    if ((*ax == 0 || *ax == w) && *ay != 0 && *ay != h) *ay = b[1] < h * 0.5f ? 0 : h;
    *inx = *ax != 0 && *ax != w && *ax != w * 0.5f;
    *iny = *ay != 0 && *ay != h && *ay != h * 0.5f;
}

// box: the element in guest pixels (x0, y0, x1, y1; y down) on a w x h frame.
// true when the box covers the whole frame (a full-screen tint, fade or vignette).
static inline bool ui_scale_is_full_frame(const float box[4], float w, float h) {
    return box[2] - box[0] >= 0.9f * w && box[3] - box[1] >= 0.9f * h;
}
// false: leave the draw alone (it covers the frame: a full-screen tint, fade or vignette must keep covering it)
static inline bool ui_scale_anchor(const float box[4], float w, float h, float *ax, float *ay) {
    if (ui_scale_is_full_frame(box, w, h)) return false;
    bool inx, iny;
    ui_scale_hud_anchor(box, w, h, ax, ay, &inx, &iny);
    return true;
}

// The element's box from its clip-space transform: m = cb3 floats 0..7 (a b _ tx c d _ ty), the local corners
// lx0..lx1 / ly0..ly1 (or a single point: lx0 == lx1, ly0 == ly1), the draw's guest viewport vp (x, y, w, h).
// false when m does not look like a 2D transform (another shader layout: the draw is not scaled).
static inline bool ui_scale_box(const float m[8], float lx0, float ly0, float lx1, float ly1, const float vp[4], float box[4]) {
    if (m[2] != 0 || m[6] != 0) return false;
    for (int i = 0; i < 8; i++) if (!isfinite(m[i])) return false;
    if (fabsf(m[3]) > 4 || fabsf(m[7]) > 4) return false;
    float cx0 = 1e30f, cy0 = 1e30f, cx1 = -1e30f, cy1 = -1e30f;
    for (int k = 0; k < 4; k++) {
        float lx = (k & 1) ? lx1 : lx0, ly = (k & 2) ? ly1 : ly0;
        float x = m[0] * lx + m[1] * ly + m[3], y = m[4] * lx + m[5] * ly + m[7];
        cx0 = fminf(cx0, x); cx1 = fmaxf(cx1, x); cy0 = fminf(cy0, y); cy1 = fmaxf(cy1, y);
    }
    // clip (y up) -> guest pixels (y down)
    box[0] = vp[0] + (cx0 + 1) * 0.5f * vp[2];
    box[2] = vp[0] + (cx1 + 1) * 0.5f * vp[2];
    box[1] = vp[1] + (1 - cy1) * 0.5f * vp[3];
    box[3] = vp[1] + (1 - cy0) * 0.5f * vp[3];
    return true;
}

// Pieces of one HUD component (the minimap and its icons, a hint list, a dialogue box and its text) must share one
// anchor, or a component on a zone boundary falls apart. The game draws a component's pieces one after another, so
// a draw whose box touches the current group's box (grown by a margin) joins the group and uses its anchor; any
// other draw starts a new group, anchored from its own box. The caller resets the group per frame and target.
static inline bool ui_scale_in_band_(const float b[4], float w, float h) {  // (touches the frame's edge band)
    return b[0] < w * UI_SCALE_EDGE || b[2] > w * (1 - UI_SCALE_EDGE) || b[1] < h * UI_SCALE_EDGE || b[3] > h * (1 - UI_SCALE_EDGE);
}
// Only the HUD on the frame's edges is scaled: a group that touches the edge band (the minimap and the hints, the party
// list, the arts palette, the enemy's bar, "Story"). What sits away from the edges keeps the game's size: popups and
// dialogue boxes, the area banner, markers over people and enemies, the reticle - scaled piece by piece they came apart
// (a panel and its text with different anchors) and moved off what they mark. A group that crosses the middle of the
// width and covers half the height or more is a window (a menu) and is not scaled either.
static inline bool ui_scale_hud_group_(const float b[4], float w, float h) {
    if (!ui_scale_in_band_(b, w, h)) return false;
    return !(b[0] < w * 0.5f && b[2] > w * 0.5f && b[3] - b[1] >= h * 0.5f);
}
typedef struct {
    bool valid;
    bool scaled;   // a HUD group on the frame's edge (ui_scale_hud_group_)
    float box[4];  // the union of the group's boxes (guest pixels)
    float ax, ay;  // its anchor
} UiScaleGroup;

static inline bool ui_scale_group_anchor(UiScaleGroup *g, const float box[4], float w, float h, float *ax, float *ay) {
    if (ui_scale_is_full_frame(box, w, h)) return false;  // (full frame: not scaled, no group)
    // (32 px of the 1280x720 frame between pieces in the edge band, else 12: as ui_scale_frame_begin)
    bool band = ui_scale_in_band_(box, w, h) && g->valid && ui_scale_in_band_(g->box, w, h);
    float m = band ? h * 0.045f : h * 0.017f;
    if (g->valid && box[0] <= g->box[2] + m && box[2] >= g->box[0] - m && box[1] <= g->box[3] + m && box[3] >= g->box[1] - m) {
        g->box[0] = fminf(g->box[0], box[0]); g->box[1] = fminf(g->box[1], box[1]);
        g->box[2] = fmaxf(g->box[2], box[2]); g->box[3] = fmaxf(g->box[3], box[3]);
        bool now_scaled = ui_scale_hud_group_(g->box, w, h);
        if (!g->scaled && now_scaled) {
            if (ui_scale_anchor(g->box, w, h, &g->ax, &g->ay)) g->scaled = true;
        } else if (g->scaled && !now_scaled) {
            g->scaled = false;
        }
    } else {
        if (!ui_scale_anchor(box, w, h, &g->ax, &g->ay)) return false;
        g->valid = true;
        g->scaled = ui_scale_hud_group_(box, w, h);
        for (int i = 0; i < 4; i++) g->box[i] = box[i];
    }
    *ax = g->ax; *ay = g->ay;
    return g->scaled;
}

// Order-free groups, kept steady from frame to frame. The boxes of a whole frame are grouped and the next frame's draws
// find their box among them (the HUD barely moves between frames) and use that group's anchor; a draw with no match (an
// element that just appeared) takes the anchor of a still element it lies in, else the in-order chaining above.
//  - Markers in the world (the target reticle, name plates, speech bubbles over people, damage numbers) move with the
//    camera: an element away from the frame's edges that kept moving without changing size (UI_SCALE_MOVES of 6
//    frames, not with a HUD element beside it: a palette slot shaking as its art fires, a panel sliding) is one for as
//    long as it stays there (near an edge it is the HUD's: pieces slide in there, markers seldom go). A marker is never
//    scaled (unless it lies in a HUD element: an icon in a panel) and never joins the HUD's groups: markers came and went
//    from them and swung their anchors (the battle HUD, the reticle and the bubbles shook).
//  - The rest groups within the margin, transitively; only a group on the frame's edge is scaled (ui_scale_hud_group_).
//    An element changes an established anchor, or whether it is scaled, only once its group has disagreed for
//    UI_SCALE_SETTLE frames: an element that joins a group for a frame or two no longer moves it.
#define UI_SCALE_MAX_ITEMS 768
#define UI_SCALE_SETTLE 12
#define UI_SCALE_MOVES 3  // (moves in the last 6 frames that make a marker)
typedef struct {
    float box[4];
    int match;       // the last frame's element it continues (-1: new)
    bool still;      // the same box as that one
    bool world;      // a marker in the world (it or an element it continues kept moving without changing size)
    bool was_world;  // (the element it continues was one)
    uint8_t moves;   // its moves over the last frames (bit 0: this one)
    float dx, dy;    // its move since the last frame
    bool scaled;     // this frame's decision (for later draws in the same frame)
    float ax, ay;
} UiScaleItem;
typedef struct {
    float box[4];
    float ax, ay;    // the anchor its successor uses
    bool inx, iny;   // that anchor is the element's group centre (it follows a moving element)
    bool scaled;     // its group is scaled (the HUD on the frame's edges)
    bool world;
    uint8_t moves;
    float cx, cy;    // the group's anchor when it differs from the one held, and for how many frames it has
    int settle;
    int age;         // frames the anchor has been in use (a young one - an element just shown - is not held)
} UiScalePrev;
typedef struct {
    UiScaleItem cur[UI_SCALE_MAX_ITEMS];  // this frame's elements so far
    int ncur;
    UiScalePrev prev[UI_SCALE_MAX_ITEMS]; // the last frame's
    int nprev;
    UiScaleGroup chain;
    bool bubble;     // the draws since a speech bubble's panel (ui_scale_frame_anchor) are the bubble's
    float bx, by;    // that panel's point
    bool dlg;        // GitHub #241: a non-cutscene bottom dialogue speaker bar was seen this frame
    bool dlg_prev;   // ...or last frame (the "A" icon and dialogue text draw before the bar)
} UiScaleFrame;

// GitHub #241: non-cutscene gameplay dialogue lays out its speaker name (y ~491..543), speaker name bar (y ~535..574,
// 426x39 px), Line 1 (y ~563..612), Line 2 (y ~563..661) and "A" button (y ~657..715) across the bottom of the 1280x720
// frame. Line 1 sits above the 10% bottom edge band (y > 648), so the dialogue stayed at 100% for 1-2 s until Line 2
// wrapped into the band and then shrank to (w/2, h). Detecting the speaker bar anchors the whole dialogue at (w/2, h)
// from its very first frame.
static inline bool ui_scale_is_dlg_bar_(const float b[4], float w, float h) {
    float bw = b[2] - b[0], bh = b[3] - b[1];
    return b[1] >= 0.72f * h && b[1] <= 0.77f * h && b[3] >= 0.77f * h && b[3] <= 0.82f * h &&
           bw >= 0.25f * w && bw <= 0.45f * w && bh >= 0.03f * h && bh <= 0.08f * h;
}
static inline bool ui_scale_in_dlg_(const float b[4], float w, float h) {
    return b[1] >= 0.65f * h && b[3] <= h + 0.02f * h && b[0] < 0.88f * w;
}

// GitHub #153: the area a speech bubble's pieces lie in, from its panel's point (the panel is drawn at its top-left
// corner, a bit left of it: the line text, the name and its bar, the tail, an icon past the right end). Measured at
// 1280x720 (XCDE_UI_SCALE_TRACE): line text from +80 / +34, the tail to +106 down, a shop icon to +542 across.
static inline bool ui_scale_in_bubble_(const float b[4], float px, float py, float w, float h) {
    return b[0] >= px - 0.04f * w && b[2] <= px + 0.5f * w && b[1] >= py - 0.08f * h && b[3] <= py + 0.3f * h;
}

static inline int ui_scale_find_(int *p, int i) { while (p[i] != i) i = p[i] = p[p[i]]; return i; }
static inline bool ui_scale_near_(const float *a, const float *b, float m) {
    return a[0] <= b[2] + m && a[2] >= b[0] - m && a[1] <= b[3] + m && a[3] >= b[1] - m;
}
static inline float ui_scale_overlap_(const float *a, const float *b) {
    float ox = fminf(a[2], b[2]) - fmaxf(a[0], b[0]), oy = fminf(a[3], b[3]) - fmaxf(a[1], b[1]);
    return ox > 0 && oy > 0 ? ox * oy : 0;
}
static inline bool ui_scale_same_(const float *a, const float *b) {
    return fabsf(a[0] - b[0]) < 0.25f && fabsf(a[1] - b[1]) < 0.25f && fabsf(a[2] - b[2]) < 0.25f && fabsf(a[3] - b[3]) < 0.25f;
}
// a new frame (or target) begins: group the elements of the one before
static inline void ui_scale_frame_begin(UiScaleFrame *f, float w, float h) {
    static int parent[UI_SCALE_MAX_ITEMS], owner[UI_SCALE_MAX_ITEMS];
    static float gb[UI_SCALE_MAX_ITEMS][4];
    static UiScalePrev next[UI_SCALE_MAX_ITEMS];
    int n = f->ncur;
    float m = h * 0.065f;  // (47 px of the 1280x720 frame: the party list's rows are 29 and 37 px apart, the hints sit under the area name)
    const UiScaleItem *it = f->cur;
    // an element that only now counts as a marker but moves with a HUD element beside it (a whole panel sliding) is
    // the HUD's
    static bool wf[UI_SCALE_MAX_ITEMS];
    for (int i = 0; i < n; i++) {
        wf[i] = it[i].world;
        if (!wf[i] || it[i].was_world) continue;
        for (int j = 0; j < n && wf[i]; j++)
            if (!it[j].world && !it[j].still && it[j].match >= 0 && ui_scale_near_(it[i].box, it[j].box, m) &&
                fabsf(it[i].dx - it[j].dx) < 0.5f && fabsf(it[i].dy - it[j].dy) < 0.5f)
                wf[i] = false;
    }
    for (int i = 0; i < n; i++) parent[i] = i;
    // (the HUD's elements: within the margin when both are in the frame's edge band, else nearly touching - the target
    // reticle is 22 px under the enemy's name bar, and that would pull it to the top when the camera rests)
    static bool band[UI_SCALE_MAX_ITEMS];
    for (int i = 0; i < n; i++) {
        band[i] = ui_scale_in_band_(it[i].box, w, h);
    }
    float m_mid = h * 0.017f;  // (12 px: the arts palette's slots are 8 px apart)
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            if (wf[i] || wf[j] || !ui_scale_near_(it[i].box, it[j].box, band[i] && band[j] ? m : m_mid)) continue;
            int a = ui_scale_find_(parent, i), b = ui_scale_find_(parent, j);
            if (a != b) parent[b] = a;
        }
    for (int i = 0; i < n; i++) { gb[i][0] = gb[i][1] = 1e30f; gb[i][2] = gb[i][3] = -1e30f; }
    for (int i = 0; i < n; i++) {
        int g = ui_scale_find_(parent, i);
        gb[g][0] = fminf(gb[g][0], it[i].box[0]); gb[g][1] = fminf(gb[g][1], it[i].box[1]);
        gb[g][2] = fmaxf(gb[g][2], it[i].box[2]); gb[g][3] = fmaxf(gb[g][3], it[i].box[3]);
    }
    // a marker mostly inside a larger element - text in its speech bubble, the reticle's centre in its frame, an icon
    // in a HUD panel - takes that element's anchor (the largest such element)
    for (int i = 0; i < n; i++) {
        owner[i] = ui_scale_find_(parent, i);
        if (!wf[i]) continue;
        float a = (it[i].box[2] - it[i].box[0]) * (it[i].box[3] - it[i].box[1]), best = a;
        for (int j = 0; j < n; j++) {
            float aj = (it[j].box[2] - it[j].box[0]) * (it[j].box[3] - it[j].box[1]);
            if (j == i || aj <= best || ui_scale_overlap_(it[i].box, it[j].box) < 0.6f * a) continue;
            best = aj; owner[i] = ui_scale_find_(parent, j);
        }
    }
    for (int i = 0; i < n; i++) {
        int g = owner[i];
        bool inx, iny;
        float ax, ay;
        ui_scale_hud_anchor(gb[g], w, h, &ax, &ay, &inx, &iny);
        bool scaled = !wf[g] && ui_scale_hud_group_(gb[g], w, h);  // (markers and what is away from the edges: as drawn)
        UiScalePrev *o = &next[i];
        for (int k = 0; k < 4; k++) o->box[k] = it[i].box[k];
        o->inx = inx; o->iny = iny; o->scaled = scaled; o->world = wf[i]; o->moves = it[i].moves;
        o->ax = ax; o->ay = ay; o->cx = ax; o->cy = ay; o->settle = 0; o->age = 0;
        const UiScalePrev *p = it[i].match >= 0 && it[i].match < f->nprev ? &f->prev[it[i].match] : NULL;
        if (p) {
            bool same = p->scaled == scaled && (!scaled || (fabsf(p->ax - ax) <= 0.5f && fabsf(p->ay - ay) <= 0.5f));
            if (same) o->age = p->age + 1;
            else if (p->age >= UI_SCALE_SETTLE) {
                // an element keeps an established anchor (or size) until its group has disagreed with it for a while
                // (then takes the latest: a group whose own centre wobbles by a pixel must not hold an old one forever)
                int k = p->settle + 1;
                if (k < UI_SCALE_SETTLE) {
                    o->ax = p->ax; o->ay = p->ay; o->inx = p->inx; o->iny = p->iny; o->scaled = p->scaled;
                    o->settle = k; o->age = p->age;
                }
            }
        }
    }
    for (int i = 0; i < n; i++) f->prev[i] = next[i];
    f->nprev = n;
    f->ncur = 0;
    f->chain.valid = false;
    f->bubble = false;
    f->dlg_prev = f->dlg;
    f->dlg = false;
}

// this draw's anchor; false: not scaled (away from the frame's edges, a marker, a window, or it covers the frame)
static inline bool ui_scale_frame_anchor(UiScaleFrame *f, const float box[4], float w, float h, float *ax, float *ay) {
    if (ui_scale_is_full_frame(box, w, h)) return false;
    // a box of no size: the draw's shape does not come from the positions read (an NPC's speech bubble panel, built
    // from its constants) - its extent is unknown, it is left as drawn
    if (box[2] - box[0] < 0.5f && box[3] - box[1] < 0.5f) {
        f->bubble = true; f->bx = box[0]; f->by = box[1];
        return false;
    }
    // GitHub #153: the bubble's own pieces, drawn right after its panel (its text, name, tail), are left as drawn too.
    // Near the frame's side they fell in the edge band and shrank towards a corner, out of the panel - on a wide
    // window the widened view puts people there all the time. The first draw outside the bubble ends it (the HUD).
    if (f->bubble) {
        if (ui_scale_in_bubble_(box, f->bx, f->by, w, h)) return false;
        f->bubble = false;
    }
    // GitHub #241: non-cutscene bottom dialogue (speaker bar + speaker name + Line 1/2 text + "A" icon) scales
    // immediately around bottom-center (w/2, h) from frame 1 instead of staying at 100% until Line 2 wraps.
    if (ui_scale_is_dlg_bar_(box, w, h)) {
        f->dlg = true;
        *ax = w * 0.5f; *ay = h;
        return true;
    }
    if ((f->dlg || f->dlg_prev) && ui_scale_in_dlg_(box, w, h)) {
        *ax = w * 0.5f; *ay = h;
        return true;
    }
    // the last frame's element this one continues: the same box, else the one it overlaps most - at least half of the
    // larger area (2 px of slack for thin pieces): an element that was not drawn last frame (hint text blinking out for
    // a frame) took the state of a marker it merely touched
    int best = -1;
    bool still = false;
    float bd = 0.5f, cx = 0.5f * (box[0] + box[2]), cy = 0.5f * (box[1] + box[3]);
    float ga[4] = {box[0] - 2, box[1] - 2, box[2] + 2, box[3] + 2}, area = (ga[2] - ga[0]) * (ga[3] - ga[1]);
    for (int i = 0; i < f->nprev; i++) {
        const float *pb = f->prev[i].box;
        if (ui_scale_same_(box, pb)) { best = i; still = true; break; }
        float pg[4] = {pb[0] - 2, pb[1] - 2, pb[2] + 2, pb[3] + 2};
        float d = ui_scale_overlap_(ga, pg) / fmaxf(area, (pg[2] - pg[0]) * (pg[3] - pg[1]));
        if (d > bd) { bd = d; best = i; }
    }
    // GitHub #241: typewriter / growing dialogue text (same top-left or top-center origin, growing right/down) can
    // double in width or height in one frame (d <= 0.5). Keep its continuation from the previous frame so it does not
    // drop back to 100% mid-line or on line wrap.
    if (best < 0) {
        float best_pa = 0;
        float bh = box[3] - box[1];
        for (int i = 0; i < f->nprev; i++) {
            if (f->prev[i].world) continue;
            const float *pb = f->prev[i].box;
            float ph = pb[3] - pb[1];
            if (fabsf(box[1] - pb[1]) > 2.0f) continue;
            bool same_left = fabsf(box[0] - pb[0]) <= 2.0f;
            bool same_cx = fabsf(cx - 0.5f * (pb[0] + pb[2])) <= 2.0f;
            if (!same_left && !same_cx) continue;
            if (box[0] > pb[0] + 2.0f || box[2] < pb[2] - 2.0f || box[3] < pb[3] - 2.0f) continue;
            if (bh > fmaxf(3.5f * ph, ph + 0.12f * h)) continue;
            float pg[4] = {pb[0] - 2, pb[1] - 2, pb[2] + 2, pb[3] + 2};
            float pa = (pg[2] - pg[0]) * (pg[3] - pg[1]);
            if (pa > best_pa && ui_scale_overlap_(ga, pg) >= 0.85f * pa) { best_pa = pa; best = i; }
        }
    }
    const UiScalePrev *p = best >= 0 ? &f->prev[best] : NULL;
    float dx = p ? cx - 0.5f * (p->box[0] + p->box[2]) : 0, dy = p ? cy - 0.5f * (p->box[1] + p->box[3]) : 0;
    bool band = ui_scale_in_band_(box, w, h);
    int cur_idx = -1;
    if (f->ncur < UI_SCALE_MAX_ITEMS) {
        cur_idx = f->ncur++;
        UiScaleItem *c = &f->cur[cur_idx];
        for (int k = 0; k < 4; k++) c->box[k] = box[k];
        c->match = best; c->still = still; c->dx = dx; c->dy = dy;
        // moved, same size: a marker in the world
        bool moved = p && !still && fabsf((box[2] - box[0]) - (p->box[2] - p->box[0])) < 0.5f &&
                     fabsf((box[3] - box[1]) - (p->box[3] - p->box[1])) < 0.5f && (fabsf(dx) > 0.2f || fabsf(dy) > 0.2f);
        // (in the edge band it is the HUD's: the party list slides in at a battle's start; a marker there is rare)
        c->moves = (uint8_t)((p ? p->moves << 1 : 0) | moved);
        int nm = 0;
        for (int k = 0; k < 6; k++) nm += (c->moves >> k) & 1;
        c->was_world = p && p->world;
        c->world = (c->was_world || nm >= UI_SCALE_MOVES) && !band;
        c->scaled = false; c->ax = 0; c->ay = 0;
    }
    bool res = false;
    if (p) {  // (an anchor in the element's own place moves with it)
        if (p->scaled) {
            *ax = p->ax + (p->inx ? dx : 0);
            *ay = p->ay + (p->iny ? dy : 0);
            res = true;
        }
    } else {
        // just appeared: inside a HUD element of the last frame -> its anchor
        float box_area = (box[2] - box[0]) * (box[3] - box[1]);
        float bestin = 0.6f * box_area;
        int in = -1;
        for (int i = 0; i < f->nprev; i++) {
            if (f->prev[i].world) continue;
            float ov = ui_scale_overlap_(box, f->prev[i].box);
            if (ov > bestin) { bestin = ov; in = i; }
        }
        if (in >= 0) {
            if (f->prev[in].scaled) { *ax = f->prev[in].ax; *ay = f->prev[in].ay; res = true; }
        } else {
            // GitHub #241: inside an element drawn earlier in the same frame (e.g. a dialogue panel and its text
            // appearing together, even when unrelated draws sit between them and reset f->chain)
            int prev_cur = cur_idx >= 0 ? cur_idx : f->ncur;
            float best_cur = 0.6f * box_area;
            int in_cur = -1;
            for (int i = 0; i < prev_cur; i++) {
                if (f->cur[i].world) continue;
                float ov = ui_scale_overlap_(box, f->cur[i].box);
                if (ov > best_cur) { best_cur = ov; in_cur = i; }
            }
            if (in_cur >= 0) {
                if (f->cur[in_cur].scaled) { *ax = f->cur[in_cur].ax; *ay = f->cur[in_cur].ay; res = true; }
            } else {
                // GitHub #241: adjacent (within the frame-grouping margin) to an already-scaled HUD element from last
                // frame or earlier in this frame -> scale immediately on frame 1 instead of popping from 100% on frame 2
                float m_edge = h * 0.065f, m_mid = h * 0.017f;
                int near_p = -1;
                for (int i = 0; i < f->nprev; i++) {
                    if (f->prev[i].world || !f->prev[i].scaled) continue;
                    const float *pb = f->prev[i].box;
                    float mg = (band && ui_scale_in_band_(pb, w, h)) ? m_edge : m_mid;
                    if (!ui_scale_near_(box, pb, mg)) continue;
                    float u[4] = {fminf(box[0], pb[0]), fminf(box[1], pb[1]), fmaxf(box[2], pb[2]), fmaxf(box[3], pb[3])};
                    if (!ui_scale_hud_group_(u, w, h)) continue;
                    near_p = i; break;
                }
                if (near_p >= 0) {
                    *ax = f->prev[near_p].ax; *ay = f->prev[near_p].ay; res = true;
                } else {
                    int near_c = -1;
                    for (int i = 0; i < prev_cur; i++) {
                        if (f->cur[i].world || !f->cur[i].scaled) continue;
                        const float *cb = f->cur[i].box;
                        float mg = (band && ui_scale_in_band_(cb, w, h)) ? m_edge : m_mid;
                        if (!ui_scale_near_(box, cb, mg)) continue;
                        float u[4] = {fminf(box[0], cb[0]), fminf(box[1], cb[1]), fmaxf(box[2], cb[2]), fmaxf(box[3], cb[3])};
                        if (!ui_scale_hud_group_(u, w, h)) continue;
                        near_c = i; break;
                    }
                    if (near_c >= 0) {
                        *ax = f->cur[near_c].ax; *ay = f->cur[near_c].ay; res = true;
                    } else {
                        res = ui_scale_group_anchor(&f->chain, box, w, h, ax, ay);
                    }
                }
            }
        }
    }
    if (cur_idx >= 0) {
        f->cur[cur_idx].scaled = res;
        if (res) { f->cur[cur_idx].ax = *ax; f->cur[cur_idx].ay = *ay; }
    }
    return res;
}

// Fold "scale by s around (ax, ay) in window pixels" into the draw's guest -> window mapping X = gx * sx + ox.
static inline void ui_scale_fold(float s, float ax, float ay, float *sx, float *sy, float *ox, float *oy) {
    *sx *= s; *sy *= s;
    *ox = s * *ox + (1 - s) * ax;
    *oy = s * *oy + (1 - s) * ay;
}
