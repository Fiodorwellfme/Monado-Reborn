// Data root and shipped resources (runtime/paths.h).
#include <SDL3/SDL.h>
#include "platform/dir_utf8.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "host/utf8_path.h"  // [XCDE] T278: UTF-8 paths through the wide APIs
#include "gamelib.h"
#include "paths.h"
#include "platform.h"
#include "ui/monado_icon.h"

static char g_root[1024], g_start[1024], g_exe_dir[1024];

#ifdef _WIN32
#define SEP "\\"
static bool is_link(const char *p) {  // symlink or junction
    wchar_t wp[XCDE_PATH_W];
    DWORD a;
    if (xcde_utf8_to_wide(p, wp, XCDE_PATH_W) < 0) return false;
    a = GetFileAttributesW(wp);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
}
static bool resolve(const char *p, char *out, size_t n) {
    wchar_t wp[XCDE_PATH_W], wo[XCDE_PATH_W];
    if (xcde_utf8_to_wide(p, wp, XCDE_PATH_W) < 0 || !_wfullpath(wo, wp, XCDE_PATH_W)) return false;
    return xcde_utf8_from_wide(wo, out, n) >= 0;
}
#define path_eq(a, b) (_stricmp(a, b) == 0)
#else
#define SEP "/"
static bool is_link(const char *p) {
    struct stat st;
    return lstat(p, &st) == 0 && S_ISLNK(st.st_mode);
}
static bool resolve(const char *p, char *out, size_t n) {
    char buf[PATH_MAX];
    if (!realpath(p, buf)) return false;
    size_t len = strlen(buf);
    if (len >= n) return false;
    memcpy(out, buf, len + 1);
    return true;
}
#define path_eq(a, b) (strcmp(a, b) == 0)
#endif

static bool is_dir(const char *p) {
    struct stat st;
    return xcde_utf8_stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool exists(const char *p) {
    struct stat st;
    return xcde_utf8_stat(p, &st) == 0;
}

static void mkdirs(const char *path) {
    char p[1024];
    snprintf(p, sizeof p, "%s", path);
    for (char *c = p + 1; *c; c++)
        if (*c == '/' || *c == '\\') { char s = *c; *c = 0; xcde_utf8_mkdir(p); *c = s; }
    xcde_utf8_mkdir(p);
}

static bool writable(const char *dir) {
    char t[1100];
    snprintf(t, sizeof t, "%s" SEP ".write_test", dir);
    FILE *f = xcde_utf8_fopen(t, "wb");
    if (!f) return false;
    fclose(f);
    xcde_utf8_remove(t);
    return true;
}

static void fail(const char *msg) {
    rt_logf("%s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "LAS Recomp", msg, NULL);
    exit(1);
}

static bool g_dev;  // the data root is the starting folder because it has extracted/ (a source checkout)
bool rt_dev_mode(void) { return g_dev; }

void rt_paths_init(void) {
    if (!xcde_utf8_getcwd(g_start, sizeof g_start)) g_start[0] = 0;
    if (plat_exe_path(g_exe_dir, sizeof g_exe_dir)) {
        char *s = strrchr(g_exe_dir, '/'), *b = strrchr(g_exe_dir, '\\');
        if (b > s) s = b;
        if (s) *s = 0;
    }
    char root[1024];
    char root_env[1100];
    const char *env = xcde_utf8_getenv("LARECOMP_DATA_ROOT", root_env, sizeof root_env) ? root_env : NULL;
    if (env) snprintf(root, sizeof root, "%s", env);
    else if (is_dir("extracted")) snprintf(root, sizeof root, "%s", g_start), g_dev = true;  // dev / portable: as before
    else {
#ifdef _WIN32
        snprintf(root, sizeof root, "%s", g_exe_dir);
#elif defined(__linux__)
        // (the AppImage is read-only): $XDG_DATA_HOME/monado, ~/.local/share/monado by default (platform/linux.c)
        plat_data_dir(root, sizeof root);
#else
        const char *home = getenv("HOME");
        snprintf(root, sizeof root, "%s/Library/Application Support/" RT_BUNDLE_ID, home && *home ? home : "/tmp");
#endif
    }
    mkdirs(root);
    char msg[1400];
    if (!is_dir(root) || !writable(root) || xcde_utf8_chdir(root) != 0) {
#ifdef _WIN32
        snprintf(msg, sizeof msg, "LAS Recomp can't write to its folder:\n%s\n\nIt keeps game files, saves and seeds "
                 "next to the program. Move the \"LAS Recomp\" folder somewhere you can write to (e.g. Documents or "
                 "Desktop, not Program Files) and start it again.", root);
#else
        snprintf(msg, sizeof msg, "Monado can't write to its data folder:\n%s", root);
#endif
        fail(msg);
    }
    if (!xcde_utf8_getcwd(g_root, sizeof g_root)) snprintf(g_root, sizeof g_root, "%s", root);
    rt_logf("[paths] data: %s\n", g_root);
}

const char *rt_data_root(void) { return g_root; }

bool rt_resource(const char *name, const char *dev_rel, char *out, size_t n) {
    char c[4][1100] = {{0}};
    if (name) {
        snprintf(c[0], sizeof c[0], "%s" SEP "%s", g_exe_dir, name);
        snprintf(c[1], sizeof c[1], "%s" SEP ".." SEP "Resources" SEP "%s", g_exe_dir, name);  // .app/Contents/Resources
    }
    if (dev_rel) {
        snprintf(c[2], sizeof c[2], "%s" SEP "%s", g_start, dev_rel);
        snprintf(c[3], sizeof c[3], "%s" SEP ".." SEP "%s", g_exe_dir, dev_rel);  // build/lasrecomp -> repo root
    }
    for (int i = 0; i < 4; i++)
        if (c[i][0] && exists(c[i])) return (size_t)snprintf(out, n, "%s", c[i]) < n;  // (as is: a venv python must stay a link)
    return false;
}

bool rt_path_absolute(const char *path, char *out, size_t n) {
    if (!path || !*path || !out || !n) return false;
    return resolve(path, out, n);
}

// ---- game files
// What the setup (runtime/ui/launcher.mm) creates in extracted/, plus what older versions' extraction and manual
// extractions left there (never read at run time).
static const char *const GAME_FILES[] = {"exefs", "romfs", "romfs.bin", GAMELIB_NAME, GAMELIB_NAME ".id", ".gamebuild",
                                         ".extract", "extract.log", "xci_secure", "nso_raw", "update_pfs", ".DS_Store"};

// game_dir must be <data root>/extracted, a real folder (a dev worktree's symlinked extracted/ is refused)
static bool game_dir_ok(const char *game_dir) {
    char want[1100], got[1100];
    snprintf(want, sizeof want, "%s" SEP "extracted", g_root);
    if (!g_root[0] || is_link(game_dir) || !resolve(game_dir, got, sizeof got)) return false;
    return path_eq(want, got);
}

bool rt_remove_game_file(const char *game_dir, const char *name) {
    bool known = false;
    for (size_t i = 0; i < sizeof GAME_FILES / sizeof *GAME_FILES; i++) known |= !strcmp(name, GAME_FILES[i]);
    if (!known || !game_dir_ok(game_dir)) {
        rt_logf("[paths] refusing to delete %s/%s (not app-created game files in the data root)\n", game_dir, name);
        return false;
    }
    char p[1100];
    snprintf(p, sizeof p, "%s" SEP "%s", game_dir, name);
    if (!exists(p) && !is_link(p)) return true;
    if (is_link(p)) {
        rt_logf("[paths] refusing to delete %s (a link)\n", p);
        return false;
    }
    return is_dir(p) ? plat_remove_tree(p) : xcde_utf8_remove(p) == 0;  // (plat_remove_tree doesn't follow links inside)
}

bool rt_remove_game_files(const char *game_dir) {
    if (!game_dir_ok(game_dir)) {
        rt_logf("[paths] refusing to delete game files in %s (not <data root>/extracted)\n", game_dir);
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < sizeof GAME_FILES / sizeof *GAME_FILES; i++) ok &= rt_remove_game_file(game_dir, GAME_FILES[i]);
    xcde_utf8_rmdir(game_dir);  // only if empty: anything else in there stays
    return ok;
}

static unsigned long long tree_size(const char *p) {
    struct stat st;
    if (is_link(p) || xcde_utf8_stat(p, &st) != 0) return 0;
    if (!S_ISDIR(st.st_mode)) return (unsigned long long)st.st_size;
    unsigned long long n = 0;
    XcdeDir *d = xcde_opendir(p);
    if (d) {
        XcdeDirent *e;
        while ((e = xcde_readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char c[2048];
            snprintf(c, sizeof c, "%s" SEP "%s", p, e->d_name);
            n += tree_size(c);
        }
        xcde_closedir(d);
    }
    return n;
}

unsigned long long rt_game_files_size(const char *game_dir) {
    unsigned long long n = 0;
    char p[1100];
    for (size_t i = 0; i < sizeof GAME_FILES / sizeof *GAME_FILES; i++) {
        snprintf(p, sizeof p, "%s" SEP "%s", game_dir, GAME_FILES[i]);
        n += tree_size(p);
    }
    return n;
}

// Register the Win32 icon resource ID before SDL_Init(SDL_INIT_VIDEO) creates the window class.
void rt_window_icon_hints(void) {
    monado_prepare_window_icon_hints();
}

// Window / Taskbar / Dock icon: prefer an external icon.png override if shipped, otherwise use
// the embedded Monado icon (Win32 multi-resolution .ico resource on Windows, embedded PNG elsewhere).
void rt_window_icon(SDL_Window *w) {
    char p[1100];
    if (rt_resource("icon.png", NULL, p, sizeof p)) {
        SDL_Surface *s = SDL_LoadPNG(p);
        if (s) {
            SDL_SetWindowIcon(w, s);
            SDL_DestroySurface(s);
            return;
        }
    }
    monado_apply_window_icon(w);
}
