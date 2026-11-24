// [XCDE] Portable hotkey scancodes (USB HID / SDL3 SDL_Scancode values) shared by settings_ini.c,
// input.c, menu_vk.cpp, launcher.cpp, and native tests without requiring SDL headers in settings_ini.c.
#pragma once
#include <stdbool.h>
#include <string.h>

enum {
    XCDE_KEY_NONE        = 0,
    XCDE_KEY_BACKSLASH   = 49,  // SDL_SCANCODE_BACKSLASH
    XCDE_KEY_GRAVE       = 53,  // SDL_SCANCODE_GRAVE
    XCDE_KEY_F1          = 58,  // SDL_SCANCODE_F1
    XCDE_KEY_F2          = 59,  // SDL_SCANCODE_F2
    XCDE_KEY_F3          = 60,  // SDL_SCANCODE_F3
    XCDE_KEY_F4          = 61,  // SDL_SCANCODE_F4
    XCDE_KEY_F5          = 62,  // SDL_SCANCODE_F5
    XCDE_KEY_F6          = 63,  // SDL_SCANCODE_F6
    XCDE_KEY_F7          = 64,  // SDL_SCANCODE_F7
    XCDE_KEY_F8          = 65,  // SDL_SCANCODE_F8
    XCDE_KEY_F9          = 66,  // SDL_SCANCODE_F9
    XCDE_KEY_F10         = 67,  // SDL_SCANCODE_F10
    XCDE_KEY_F11         = 68,  // SDL_SCANCODE_F11
    XCDE_KEY_F12         = 69,  // SDL_SCANCODE_F12
    XCDE_KEY_PRINTSCREEN = 70,  // SDL_SCANCODE_PRINTSCREEN
    XCDE_KEY_SCROLLLOCK  = 71,  // SDL_SCANCODE_SCROLLLOCK
    XCDE_KEY_PAUSE       = 72,  // SDL_SCANCODE_PAUSE
    XCDE_KEY_INSERT      = 73,  // SDL_SCANCODE_INSERT
    XCDE_KEY_HOME        = 74,  // SDL_SCANCODE_HOME
    XCDE_KEY_PAGEUP      = 75,  // SDL_SCANCODE_PAGEUP
    XCDE_KEY_END         = 77,  // SDL_SCANCODE_END
    XCDE_KEY_PAGEDOWN    = 78   // SDL_SCANCODE_PAGEDOWN
};

typedef struct {
    int scancode;
    const char *ini;
    const char *label;
} XcdeHotkeyChoice;

static const XcdeHotkeyChoice xcde_hotkey_choices[] = {
    {XCDE_KEY_NONE,        "none",        "None"},
    {XCDE_KEY_F1,          "f1",          "F1"},
    {XCDE_KEY_F2,          "f2",          "F2"},
    {XCDE_KEY_F3,          "f3",          "F3"},
    {XCDE_KEY_F4,          "f4",          "F4"},
    {XCDE_KEY_F5,          "f5",          "F5"},
    {XCDE_KEY_F6,          "f6",          "F6"},
    {XCDE_KEY_F7,          "f7",          "F7"},
    {XCDE_KEY_F8,          "f8",          "F8"},
    {XCDE_KEY_F9,          "f9",          "F9"},
    {XCDE_KEY_F10,         "f10",         "F10"},
    {XCDE_KEY_F11,         "f11",         "F11"},
    {XCDE_KEY_F12,         "f12",         "F12"},
    {XCDE_KEY_PRINTSCREEN, "printscreen", "PrintScreen"},
    {XCDE_KEY_SCROLLLOCK,  "scrolllock",  "ScrollLock"},
    {XCDE_KEY_PAUSE,       "pause",       "Pause"},
    {XCDE_KEY_INSERT,      "insert",      "Insert"},
    {XCDE_KEY_HOME,        "home",        "Home"},
    {XCDE_KEY_PAGEUP,      "pageup",      "PageUp"},
    {XCDE_KEY_END,         "end",         "End"},
    {XCDE_KEY_PAGEDOWN,    "pagedown",    "PageDown"},
    {XCDE_KEY_GRAVE,       "grave",       "` / ~"},
    {XCDE_KEY_BACKSLASH,   "backslash",   "\\"},
};

enum { XCDE_HOTKEY_COUNT = (int)(sizeof(xcde_hotkey_choices) / sizeof(xcde_hotkey_choices[0])) };

static inline int xcde_hotkey_index(int scancode, int fallback) {
    for (int i = 0; i < XCDE_HOTKEY_COUNT; i++)
        if (xcde_hotkey_choices[i].scancode == scancode) return i;
    for (int i = 0; i < XCDE_HOTKEY_COUNT; i++)
        if (xcde_hotkey_choices[i].scancode == fallback) return i;
    return 0;
}

static inline int xcde_hotkey_parse(const char *val, int fallback, bool allow_none) {
    if (!val || !*val) return fallback;
    for (int i = 0; i < XCDE_HOTKEY_COUNT; i++) {
        if (!strcmp(val, xcde_hotkey_choices[i].ini)) {
            if (!allow_none && xcde_hotkey_choices[i].scancode == XCDE_KEY_NONE) return fallback;
            return xcde_hotkey_choices[i].scancode;
        }
    }
    return fallback;
}

static inline const char *xcde_hotkey_ini(int scancode, int fallback) {
    return xcde_hotkey_choices[xcde_hotkey_index(scancode, fallback)].ini;
}

static inline const char *xcde_hotkey_label(int scancode, int fallback, bool en) {
    int idx = xcde_hotkey_index(scancode, fallback);
    if (idx == 0) return en ? "None" : "Ninguna";
    return xcde_hotkey_choices[idx].label;
}
