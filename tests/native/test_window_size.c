// [XCDE] Window size presets shared by the launcher and the F1 menu (runtime/window_size.h).
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../runtime/window_size.h"

int main(void) {
    assert(XCDE_WINDOW_SIZE_COUNT == 14);
    assert(!strcmp(xcde_window_size_names[0], "1280x720"));
    assert(!strcmp(xcde_window_size_names[7], "2560x1600"));
    assert(!strcmp(xcde_window_size_names[XCDE_WINDOW_SIZE_COUNT - 1], "7680x1440"));
    int w = 0, h = 0;
    assert(xcde_window_size_parse("1920x1080", &w, &h) && w == 1920 && h == 1080);
    assert(xcde_window_size_parse("1280x800", &w, &h) && w == 1280 && h == 800);
    w = 111; h = 222;
    assert(!xcde_window_size_parse("1920x1080px", &w, &h));
    assert(!xcde_window_size_parse("1920X1080", &w, &h));
    assert(!xcde_window_size_parse("0x720", &w, &h));
    assert(!xcde_window_size_parse("1920x", &w, &h));
    assert(!xcde_window_size_parse("", &w, &h));
    assert(w == 111 && h == 222);  // (a failed parse leaves the size alone)
    assert(xcde_window_size_index(2560, 1600) == 7);
    assert(xcde_window_size_index(5760, 1080) == 12);
    assert(xcde_window_size_index(7680, 1440) == 13);
    assert(xcde_window_size_index(1600, 900) == 1);
    assert(xcde_window_size_index(1366, 768) == -1);  // a hand-dragged window shows as Custom
    for (int i = 0; i < XCDE_WINDOW_SIZE_COUNT; i++) {  // every preset parses back to the size its name spells
        assert(xcde_window_size_parse(xcde_window_size_names[i], &w, &h));
        assert(xcde_window_size_index(w, h) == i);
    }
    puts("test_window_size: ok");
    return 0;
}
