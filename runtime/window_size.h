// [XCDE] Window size presets (xcde.ini "window = WxH"), shared by the launcher and the in-game F1 menu.
// Steam Deck: the 16:10 sizes (its 1280x800 and the 1920x1200 / 2560x1600 external-display peers) come after the 16:9
// ones, followed by 21:9, 32:9, and 48:9 (triple-monitor) sizes; with "Automatic" aspect the game widens its view to fit them.
#pragma once
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *const xcde_window_size_names[] = {
    "1280x720", "1600x900", "1920x1080", "2560x1440", "3840x2160", "1280x800", "1920x1200", "2560x1600",
    "2560x1080", "3440x1440", "3840x1080", "5120x1440", "5760x1080", "7680x1440",
};
enum { XCDE_WINDOW_SIZE_COUNT = (int)(sizeof xcde_window_size_names / sizeof xcde_window_size_names[0]) };

// Index of the preset with this size, or -1 (a size typed into xcde.ini, or a window the player resized by hand).
static inline int xcde_window_size_index(int w, int h) {
    char size[32];
    snprintf(size, sizeof size, "%dx%d", w, h);
    for (int i = 0; i < XCDE_WINDOW_SIZE_COUNT; i++)
        if (!strcmp(xcde_window_size_names[i], size)) return i;
    return -1;
}

// "WxH" -> width and height. False for anything else (the reader then keeps the previous size).
static inline bool xcde_window_size_parse(const char *text, int *w, int *h) {
    int a = 0, b = 0, n = 0;
    if (!text || sscanf(text, "%dx%d%n", &a, &b, &n) != 2 || n <= 0 || text[n] || a <= 0 || b <= 0) return false;
    *w = a;
    *h = b;
    return true;
}
