// [XCDE] GitHub #121: HUD scale anchors (runtime/ui_scale.h). The boxes are guest pixels on the 1280x720 frame,
// measured from the field and battle HUD (XCDE_UI_SCALE_LOG); no game data.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../../runtime/ui_scale.h"

static const float W = 1280, H = 720;
static UiScaleFrame f;  // (large: not on the stack)

static bool near_(float a, float b) { return fabsf(a - b) < 0.01f; }

static bool sc[32];  // (the last frame's run: each box scaled?)

// one frame of boxes through the frame grouping; the anchors (and sc) of its run
static void frame(const float (*b)[4], int n, float *ax, float *ay) {
    ui_scale_frame_begin(&f, W, H);
    for (int i = 0; i < n; i++) sc[i] = ui_scale_frame_anchor(&f, b[i], W, H, &ax[i], &ay[i]);
}
static bool all_(int i0, int n, bool v) { for (int i = i0; i < n; i++) if (sc[i] != v) return false; return true; }

int main(void) {
    float ax, ay, box[4];

    // settings: missing / 0 = the game's size, clamped to 50..100
    assert(ui_scale_percent(0) == 100 && ui_scale_percent(30) == UI_SCALE_MIN && ui_scale_percent(75) == 75 &&
           ui_scale_percent(150) == 100);

    // per-axis anchors: an edge it hugs, else its own centre; across the width, over the middle = the centre
    assert(ui_scale_anchor_x(1095, 1285, W) == W);                 // minimap: right edge
    assert(ui_scale_anchor_y(-5, 185, H) == 0);                    // ... top edge
    assert(ui_scale_anchor_x(540, 740, W) == 640);                 // reticle alone: in place
    assert(ui_scale_anchor_y(300, 400, H) == 350);
    assert(ui_scale_anchor_x(0, 1084, W) == W / 2);                // the palette grouped with the left gauge: centred
    assert(ui_scale_anchor_y(27, 412, H) == 0);                    // the party list reaches past the middle: still the top
    assert(ui_scale_anchor_x(-1, 1281, W) == W / 2);               // a bar across the screen

    // a full-frame draw (fade, tint) is never scaled
    float full[4] = {0, 0, 1280, 720};
    assert(!ui_scale_anchor(full, W, H, &ax, &ay));

    // the clip-space transform at the start of vertex cbuf 3 (y up) -> guest pixels (y down)
    const float m[8] = {2, 0, 0, 0.859375f, -0.0f, -3.55556f, 0, 0.75f}, vp[4] = {0, 0, 1280, 720};
    assert(ui_scale_box(m, -0.06875f, -0.06875f, 0.06875f, 0.06875f, vp, box));
    assert(fabsf(box[0] - 1101.5f) < 1 && fabsf(box[2] - 1277.5f) < 1 && box[1] < 2 && fabsf(box[3] - 177.5f) < 1);
    const float m3[8] = {1, 0, 0.5f, 0, 0, 1, 0, 0};  // (a z term: some other layout, not a 2D element)
    assert(!ui_scale_box(m3, 0, 0, 1, 1, vp, box));

    // order-free groups: the field HUD's minimap, area name and hints (drawn bottom-up) share the top-right corner
    const float field[][4] = {
        {1095, -5, 1285, 185}, {1027, 196, 1251, 231},                       // minimap, area name
        {1138, 281, 1225, 309}, {1219, 276, 1255, 312},                      // "Auto-run" + icon
        {1095, 257, 1225, 285}, {1219, 252, 1255, 288},                      // "Move Camera" + icon
        {1157, 233, 1225, 261}, {1243, 228, 1279, 264},                      // "Target" + icon
        {1163, 675, 1228, 710}, {1221, 666, 1269, 714},                      // "Story" + icon
    };
    const int nf = (int)(sizeof field / sizeof field[0]);
    float fx[16], fy[16];
    frame(field, nf, fx, fy);  // (first frame: in-order chaining)
    frame(field, nf, fx, fy);  // (second: they are seen again)
    frame(field, nf, fx, fy);  // (third: still - the groups of the still elements)
    assert(all_(0, nf, true));
    for (int i = 0; i < 8; i++) assert(fx[i] == W && fy[i] == 0);
    assert(fx[8] == W && fy[8] == H && fx[9] == W && fy[9] == H);

    // the battle party list: rows 29 and 37 px apart stay one group - one anchor, on the left edge
    const float party[][4] = {{4, 139, 158, 175}, {13, 147, 33, 167}, {16, 204, 26, 216}, {121, 217, 149, 245},
                              {0, 250, 162, 296}, {4, 255, 158, 291}, {118, 333, 146, 361}, {14, 335, 24, 348},
                              {-2, 366, 159, 412}, {1, 371, 156, 407}};
    float px[10], py[10];
    for (int k = 0; k < 3; k++) frame(party, 10, px, py);
    assert(all_(0, 10, true));
    for (int i = 0; i < 10; i++) assert(px[i] == 0 && py[i] == py[0]);

    // the arts palette with the strip behind the help line and the left gauge: centred, at the bottom
    const float bottom[][4] = {{-1, 640, 639, 720}, {0, 610, 196, 720}, {584, 546, 696, 658}, {523, 680, 946, 710}};
    float bx[4], by[4];
    for (int k = 0; k < 3; k++) frame(bottom, 4, bx, by);
    assert(all_(0, 4, true));
    for (int i = 0; i < 4; i++) assert(bx[i] == W / 2 && by[i] == H);

    // the battle's top banner: its strips slide in every frame; the banner's anchor must not follow them
    float banner[][4] = {{498, 1, 782, 91}, {504, 13, 776, 79}, {616, 10, 664, 18}, {420, 44, 788, 64}, {598, 64, 966, 84}};
    float tx[5], ty[5], t0x = 0;
    for (int k = 0; k < 20; k++) {
        banner[3][0] += 7; banner[3][2] += 7; banner[4][0] -= 11; banner[4][2] -= 11;  // (the strips)
        frame(banner, 5, tx, ty);
        if (k == 3) t0x = tx[0];
        if (k > 3) for (int i = 0; i < 3; i++) assert(sc[i] && tx[i] == t0x && ty[i] == 0);
    }
    assert(t0x == W / 2);  // (the banner crosses the middle of the frame)

    // the target reticle: four pieces moving with the camera (and stopping) keep the game's size - they stay on the
    // enemy - and never join the HUD at the top (which is scaled)
    float ret[][4] = {{1, 1, 782, 91}, {544, 218, 624, 266}, {544, 120, 624, 168}, {509, 151, 557, 231}, {610, 151, 658, 231}};
    float rx[5], ry[5];
    for (int k = 0; k < 30; k++) {
        float d = k < 10 ? 3 : 0;  // (the camera stops after 10 frames)
        for (int i = 1; i < 5; i++) { ret[i][0] += d; ret[i][2] += d; }
        frame(ret, 5, rx, ry);
        if (k > 2) assert(sc[0] && all_(1, 5, false));
    }
    // ...and with the camera at rest from the start, the reticle 22 px under the enemy's bar
    const float rest[][4] = {{498, 1, 782, 91}, {561, 113, 606, 158}, {544, 195, 624, 243}, {509, 151, 557, 231}, {610, 151, 658, 231}};
    for (int k = 0; k < 20; k++) frame(rest, 5, rx, ry);
    assert(sc[0] && all_(1, 5, false));

    // a speech bubble and its text, a dialogue box, a popup window, the area banner: away from the edges, as drawn
    float bub[][4] = {{600, 200, 700, 240}, {615, 210, 685, 230}};
    float ux[2], uy[2];
    for (int k = 0; k < 10; k++) {
        for (int i = 0; i < 2; i++) { bub[i][0] -= 2; bub[i][2] -= 2; bub[i][1] += 1; bub[i][3] += 1; }
        frame(bub, 2, ux, uy);
        if (k > 1) assert(all_(0, 2, false));
    }
    const float popup[][4] = {{360, 175, 732, 380}, {450, 185, 645, 207}, {480, 240, 800, 310}, {515, 395, 765, 416}};
    float ox_[4], oy_[4];
    for (int k = 0; k < 4; k++) { frame(popup, 4, ox_, oy_); assert(all_(0, 4, false)); }
    // a window that crosses the middle and covers half the height (the party menu) is not scaled even at the top edge
    const float window[][4] = {{427, 27, 707, 418}, {527, 160, 753, 236}, {545, 559, 735, 580}};
    for (int k = 0; k < 4; k++) { frame(window, 3, ox_, oy_); assert(all_(0, 3, false)); }

    // a palette slot shaking for a frame as its art fires stays with the palette
    float pal[][4] = {{-1, 640, 639, 720}, {0, 610, 196, 720}, {584, 546, 696, 658}, {523, 680, 946, 710}, {820, 578, 876, 634},
                      {404, 578, 460, 634}, {468, 578, 524, 634}, {532, 578, 588, 634}, {692, 578, 748, 634}, {756, 578, 812, 634}};
    float qx[10], qy[10];
    for (int k = 0; k < 20; k++) {
        float d = k == 8 ? -2.5f : k == 9 ? 2.5f : 0;
        pal[4][0] += d; pal[4][2] += d;
        frame(pal, 10, qx, qy);
        if (k > 3) for (int i = 2; i < 10; i++) assert(sc[i] && qx[i] == W / 2 && qy[i] == H);
    }

    // a hint icon sliding in by the right edge stays with its text (the edge band is the HUD's)
    float hint[][4] = {{1095, 257, 1225, 285}, {1180, 252, 1216, 288}};
    float hx[2], hy[2];
    for (int k = 0; k < 6; k++) { hint[1][0] += 7; hint[1][2] += 7; frame(hint, 2, hx, hy); }
    assert(all_(0, 2, true) && hx[1] == W && hx[0] == W && hy[1] == 0 && hy[0] == 0);
    // ...and the hints keep the top corner when the minimap's group lets them go
    const float hints[][4] = {{1095, 257, 1225, 285}, {1157, 233, 1225, 261}};
    for (int k = 0; k < 3; k++) frame(hints, 2, hx, hy);
    assert(hx[0] == W && hy[0] == 0 && hx[1] == W && hy[1] == 0);

    // hint text missing for a frame, with a marker (not scaled) over it, comes back with the HUD's corner
    const float h2[][4] = {{1157, 233, 1225, 261}, {1095, 257, 1225, 285}, {1031, 198, 1102, 270}};
    float gx[3], gy[3];
    float mv[][4] = {{1157, 233, 1225, 261}, {1031, 198, 1102, 270}};
    for (int k = 0; k < 10; k++) { frame(h2, 3, gx, gy); }
    for (int k = 0; k < 4; k++) { mv[1][0] += 3; mv[1][2] += 3; frame(mv, 2, gx, gy); }  // (the marker moves; the text is out)
    frame(h2, 2, gx, gy);
    assert(sc[0] && sc[1] && gx[1] == W && gy[1] == 0);

    // an element that joins a group for a frame or two keeps its size and anchor; after a while it follows the group
    const float lonely[][4] = {{600, 400, 680, 440}}, joined[][4] = {{600, 400, 680, 440}, {640, 440, 1270, 470}};
    float lx[2], ly[2];
    for (int k = 0; k < UI_SCALE_SETTLE + 4; k++) frame(lonely, 1, lx, ly);  // (established: away from the edges)
    assert(!sc[0]);
    for (int k = 0; k < 3; k++) { frame(joined, 2, lx, ly); assert(!sc[0]); }
    for (int k = 0; k < UI_SCALE_SETTLE + 2; k++) frame(joined, 2, lx, ly);  // (...until the new group has held)
    assert(sc[0] && sc[1] && lx[0] == W / 2 && lx[1] == W / 2);

    // an element that just appeared (no box last frame): chained in draw order; away from the edges it is as drawn
    frame(bottom, 0, bx, by);
    float lone[4] = {600, 300, 680, 340}, edge[4] = {1200, 300, 1270, 340};
    assert(!ui_scale_frame_anchor(&f, lone, W, H, &ax, &ay));
    assert(ui_scale_frame_anchor(&f, edge, W, H, &ax, &ay) && ax == W);

    // a box of no size (a speech bubble panel whose shape is not in its vertex positions) is left as drawn
    float point[4] = {461, 183, 461, 183};
    for (int k = 0; k < 3; k++) { frame(hints, 2, hx, hy); assert(!ui_scale_frame_anchor(&f, point, W, H, &ax, &ay)); }

    // GitHub #153: a bubble near the frame's right edge (a wide window, XCDE_UI_SCALE_TRACE): its pieces after the
    // panel - an icon past the edge, the tail, the line text reaching the edge band, the name bar and name - keep the
    // game's size; the minimap drawn next ends the bubble and is scaled as before, and so are the hints under it
    const float sb[][4] = {
        {813.6f, 169.7f, 813.6f, 169.7f},  // panel
        {1285.2f, 136.5f, 1355.3f, 204.5f}, {1075.6f, 275.8f, 1106.5f, 318.3f}, {893.1f, 203.7f, 1310, 268.7f},
        {853.6f, 161.1f, 853.6f, 161.1f},  // name bar
        {874.7f, 163.9f, 935.8f, 196},
        {1102, 2, 1278, 178}, {1027.2f, 195.5f, 1251.3f, 230.5f},  // minimap, hints
    };
    float sbx[8], sby[8];
    for (int k = 0; k < UI_SCALE_SETTLE + 2; k++) frame(sb, 8, sbx, sby);
    assert(all_(0, 6, false) && sc[6] && sc[7] && sbx[6] == W && sby[6] == 0 && sbx[7] == W && sby[7] == 0);
    // (without a panel first the same text is the edge's HUD)
    frame(sb + 3, 1, sbx, sby); frame(sb + 3, 1, sbx, sby);
    assert(sc[0]);

    // GitHub #241: non-cutscene dialogue text appearing in gameplay must scale on its very first frame (no 100% flash)
    // 1) dialogue panel + unrelated HUD draw + dialogue text appearing on the same frame
    frame(bottom, 0, bx, by);
    const float dlg_same[][4] = {
        {140, 560, 1140, 690},  // bottom dialogue panel (touches bottom edge band)
        {1102, 2, 1278, 178},   // unrelated minimap draw in between (resets chain)
        {240, 595, 258, 622},   // first glyph of dialogue text inside the panel (not itself in edge band)
    };
    float dx_[3], dy_[3];
    frame(dlg_same, 3, dx_, dy_);
    assert(sc[0] && sc[1] && sc[2] && dx_[2] == W / 2 && dy_[2] == H);

    // 2) typewriter dialogue text growing across frames (doubling width, then wrapping to line 2) stays scaled
    const float dlg_grow1[][4] = {{140, 650, 1140, 695}, {240, 620, 260, 644}};   // adjacent (6px above) to bottom bar
    const float dlg_grow2[][4] = {{140, 650, 1140, 695}, {240, 620, 320, 644}};   // 4x wider (d < 0.5)
    const float dlg_grow3[][4] = {{140, 650, 1140, 695}, {240, 620, 760, 672}};   // wraps to 2 lines (height > 2x)
    float dgx[2], dgy[2];
    frame(bottom, 0, bx, by);
    frame(dlg_grow1, 1, dgx, dgy);  // bar established on previous frame
    frame(dlg_grow1, 2, dgx, dgy);  // text appears adjacent to bar: scaled on frame 1
    assert(sc[0] && sc[1] && dgx[1] == W / 2 && dgy[1] == H);
    frame(dlg_grow2, 2, dgx, dgy);  // width quadruples: continues match, stays scaled
    assert(sc[0] && sc[1] && dgx[1] == W / 2 && dgy[1] == H);
    frame(dlg_grow3, 2, dgx, dgy);  // line wrap: continues match, stays scaled
    assert(sc[0] && sc[1] && dgx[1] == W / 2 && dgy[1] == H);

    // 3) chain joining into edge band upgrades g->scaled on frame 1
    UiScaleGroup cg = {0};
    float c1[4] = {240, 615, 760, 642}, c2[4] = {140, 645, 1140, 690};
    assert(!ui_scale_group_anchor(&cg, c1, W, H, &ax, &ay));
    assert(ui_scale_group_anchor(&cg, c2, W, H, &ax, &ay) && ax == W / 2 && ay == H);

    // 4) traced non-cutscene gameplay dialogue (XCDE_UI_SCALE_TRACE frames 6710, 6725, 8858, 15226):
    //    speaker bar + speaker name appear first (above y=648), then "A" button + Line 1 (still above y=648),
    //    then Line 2 wraps past y=648; all pieces scale immediately around (W/2, H) from frame 1 with no 100% delay.
    frame(bottom, 0, bx, by);
    const float dlg_open[][4] = {
        {207.0f, 535.0f, 633.0f, 574.0f},   // speaker name bar
        {235.3f, 491.6f, 419.3f, 543.5f},   // speaker name text
    };
    const float dlg_line1[][4] = {
        {1013.6f, 658.6f, 1068.0f, 713.0f}, // "A" button prompt
        {264.8f, 563.6f, 319.8f, 612.4f},   // Line 1 typing (above y=648)
        {207.0f, 535.0f, 633.0f, 574.0f},   // speaker name bar
        {235.3f, 491.6f, 419.3f, 543.5f},   // speaker name text
    };
    const float dlg_wide[][4] = {
        {62.0f, 535.0f, 488.0f, 574.0f},    // wide dialogue shifts speaker bar into left edge band
        {90.3f, 491.6f, 502.3f, 543.5f},
    };
    float dlx[4], dly[4];
    frame(dlg_open, 2, dlx, dly);
    assert(all_(0, 2, true) && dlx[0] == W / 2 && dly[0] == H && dlx[1] == W / 2 && dly[1] == H);
    for (int k = 0; k < UI_SCALE_SETTLE + 5; k++) {
        frame(dlg_line1, 4, dlx, dly);
        assert(all_(0, 4, true));
        for (int i = 0; i < 4; i++) assert(dlx[i] == W / 2 && dly[i] == H);
    }
    frame(dlg_wide, 2, dlx, dly);
    assert(all_(0, 2, true) && dlx[0] == W / 2 && dly[0] == H && dlx[1] == W / 2 && dly[1] == H);
    // after dialogue ends, normal HUD/popups resume cleanly
    frame(popup, 4, ox_, oy_);
    frame(popup, 4, ox_, oy_);
    assert(all_(0, 4, false));

    // the fold: X = gx * sx + ox, scaled by s around (ax, ay); the anchor stays put
    float sx = 1.5f, sy = 1.5f, ox = 0, oy = 0;
    ui_scale_fold(0.5f, 1920, 0, &sx, &sy, &ox, &oy);
    assert(near_(1280 * sx + ox, 1920) && near_(0 * sy + oy, 0) && near_(640 * sx + ox, 1440));

    puts("test_ui_scale: ok");
    return 0;
}
