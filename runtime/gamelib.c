// Loads the recompiled game code library (runtime/gamelib.h).
#include "gamelib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "host/utf8_path.h"  // [XCDE] T278: UTF-8 paths through the wide APIs
#include "paths.h"
#include "rt.h"

static const GameLibTables *g_tables;
static char g_error[256];

// The library's id file (<library>.id, written next to it by the build): its GAMELIB_ID, read without loading it.
static bool id_matches(const char *lib) {
    char path[1100], id[128] = {0};
    snprintf(path, sizeof path, "%s.id", lib);
    FILE *f = xcde_utf8_fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(id, 1, sizeof id - 1, f);
    fclose(f);
    while (n && (id[n - 1] == '\n' || id[n - 1] == '\r')) id[--n] = 0;
    return !strcmp(id, GAMELIB_ID);
}

static bool exists(const char *p) {
    FILE *f = xcde_utf8_fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}

// The places to look, in order (gamelib.h). Returns how many were filled.
static int candidates(const char *game_dir, char out[7][1100]) {
    int n = 0;
    char env[1100];
    if (xcde_utf8_getenv("LARECOMP_GAME_LIB", env, sizeof env)) snprintf(out[n++], 1100, "%s", env);
    char exe_side[1024];
    if (rt_resource(GAMELIB_NAME, NULL, exe_side, sizeof exe_side) && exists(exe_side))
        snprintf(out[n++], 1100, "%s", exe_side);
    if (game_dir && *game_dir) {
        struct stat st;
        bool is_file = xcde_utf8_stat(game_dir, &st) == 0 && !S_ISDIR(st.st_mode);
        if (is_file) {
            // [XCDE] GitHub #259: a .nup / .nsp file: check gamebuild/ next to the package file, then beside it.
            char parent[1024];
            snprintf(parent, sizeof parent, "%s", game_dir);
            char *s = strrchr(parent, '/'), *b = strrchr(parent, '\\');
            if (b > s) s = b;
            if (s) {
                if (s == parent) s[1] = 0;
                else *s = 0;
                snprintf(out[n++], 1100, "%s/gamebuild/%s", parent, GAMELIB_NAME);
                snprintf(out[n++], 1100, "%s/%s", parent, GAMELIB_NAME);
            }
        } else {
            snprintf(out[n++], 1100, "%s/%s", game_dir, GAMELIB_NAME);
            // [XCDE] GitHub #259: generated gamebuild folder kept inside the dump folder.
            snprintf(out[n++], 1100, "%s/gamebuild/%s", game_dir, GAMELIB_NAME);
        }
    }
    // Release packages build it once into the writable data folder (tools/gamebuild/gamebuild.py). That is also the working
    // directory the launcher and AppRun enter; rt_data_root() is empty here (rt_paths_init has no caller).
    if (xcde_utf8_getenv("LARECOMP_DATA_ROOT", env, sizeof env)) snprintf(out[n++], 1100, "%s/gamebuild/%s", env, GAMELIB_NAME);
    snprintf(out[n++], 1100, "gamebuild/%s", GAMELIB_NAME);
    return n;
}

bool gamelib_present(const char *game_dir) {
    if (g_tables) return true;
    char c[7][1100];
    int n = candidates(game_dir, c);
    for (int i = 0; i < n; i++)
        if (exists(c[i]) && id_matches(c[i])) return true;
    return false;
}

static const GameLibTables *open_lib(const char *path) {
#ifdef _WIN32
    HMODULE h = (HMODULE)xcde_utf8_load_library(path);
    if (!h) { snprintf(g_error, sizeof g_error, "can't load %s (error %lu)", path, GetLastError()); return NULL; }
    GameLibEntry entry = (GameLibEntry)(void *)GetProcAddress(h, GAMELIB_ENTRY);
#else
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) { snprintf(g_error, sizeof g_error, "%s", dlerror()); return NULL; }
    GameLibEntry entry = (GameLibEntry)dlsym(h, GAMELIB_ENTRY);
#endif
    if (!entry) { snprintf(g_error, sizeof g_error, "%s: not game code", path); return NULL; }
    const GameLibTables *t = entry();
    if (!t || t->abi != GAMELIB_ABI || !t->id || strcmp(t->id, GAMELIB_ID)) {
        snprintf(g_error, sizeof g_error, "%s was built for another version", path);
        return NULL;  // (left loaded: nothing of it runs)
    }
    return t;
}

bool gamelib_load(const char *game_dir, char *why, size_t why_size) {
    static bool tried;
    char data_env[1100];
    if (!tried) {
        tried = true;
        const char *data = xcde_utf8_getenv("LARECOMP_DATA_ROOT", data_env, sizeof data_env) ? data_env : NULL;
        snprintf(g_error, sizeof g_error,
                 "the game code hasn't been built in %s/gamebuild/%s: start the launcher once so it prepares it from "
                 "your game files",
                 data ? data : ".", GAMELIB_NAME);
        char c[7][1100];
        int n = candidates(game_dir, c);
        const char *env = xcde_utf8_getenv("LARECOMP_GAME_LIB", data_env, sizeof data_env) ? data_env : NULL;
        for (int i = 0; i < n && !g_tables; i++) {
            if (!exists(c[i])) continue;
            bool given = i == 0 && env && *env;  // LARECOMP_GAME_LIB: loaded as given, the library's own id decides
            if (!given && !id_matches(c[i])) {
                snprintf(g_error, sizeof g_error, "%s was built for another version", c[i]);
                continue;
            }
            g_tables = open_lib(c[i]);
            if (g_tables) LOG("[gamelib] %s (%llu + %llu functions)\n", c[i], (unsigned long long)*g_tables->main_count,
                              (unsigned long long)*g_tables->sdk_count);
        }
    }
    if (!g_tables && why) snprintf(why, why_size, "%s", g_error);
    return g_tables != NULL;
}

const GameLibTables *gamelib_tables(void) { return g_tables; }
