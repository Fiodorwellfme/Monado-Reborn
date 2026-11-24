// [XCDE] T282: vk_precompile_sig - the signature that decides whether the precompiled shader cache is current. The
// inputs are the build's game-file/translator/replacement-GLSL/SPIR-V compiler fingerprint (XCDE_SHADER_SIG), the
// revision of exec.c's build path, the environment switches that change translation, replacements, pipeline/layout
// artifacts or cache modes, and the game files; the build generates shader_sig.h, this test passes the macro instead.
#ifndef XCDE_SHADER_SIG
#error "test_precompile_sig needs -DXCDE_SHADER_SIG=... (precompile_sig.c takes it when the build header is absent)"
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#include <sys/utime.h>
#include <windows.h>
#define MKDIR(p) _mkdir(p)
#define RMDIR(p) _rmdir(p)
#define PID _getpid()
static void set_env(const char *name, const char *value) {
    // (SetEnvironmentVariable, not _putenv, for empty/removal: _putenv("name=") deletes the variable, and a set-but-empty
    // value is a real signature input here)
    if (value && *value) {
        char line[1200];
        snprintf(line, sizeof line, "%s=%s", name, value);
        _putenv(line);
    } else {
        SetEnvironmentVariableA(name, value ? "" : NULL);
    }
}
static int set_mtime(const char *path, time_t t) {
    struct _utimbuf b;
    b.actime = b.modtime = t;
    return _utime(path, &b);
}
#else
#include <unistd.h>
#include <utime.h>
#define MKDIR(p) mkdir((p), 0755)
#define RMDIR(p) rmdir(p)
#define PID getpid()
static void set_env(const char *name, const char *value) {
    if (value) setenv(name, value, 1);
    else unsetenv(name);
}
static int set_mtime(const char *path, time_t t) {
    struct utimbuf b;
    b.actime = b.modtime = t;
    return utime(path, &b);
}
#endif

#include "../../runtime/nvn/vulkan/precompile_sig.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static char root[1024];

// Keep the test's environment clean even when it inherits a developer's XCDE_* / LARECOMP_* settings.
static const char *const k_sig_env[] = {
    "LARECOMP_VK_OPT", "XCDE_FS_OPT", "XCDE_SPIRV_LOOSE", "LARECOMP_VK_NO_SPIRV_CACHE",
    "XCDE_STATIC_STACKS", "XCDE_RSC_PIXEL", "XCDE_D16_REF", "XCDE_SCALED_GATHER", "LARECOMP_NO_DISCARD",
    "XCDE_HW_FP16", "XCDE_EARLY_FS", "XCDE_STRUCTURED_CFG", "LARECOMP_SAVE_MSL",
    "XCDE_BLOOM_PREFILTER", "XCDE_BLOOM_FILTER", "XCDE_GTAO", "XCDE_CHAR_LIGHT", "XCDE_TMAA_TEXEL",
    "XCDE_GLSL_DIR", "LARECOMP_VK_GPL", "XCDE_VK_GPL", "XCDE_PUSH_DESC", "XCDE_CANON", "XCDE_GPL_RETAIN",
    "LARECOMP_VK_D24", "XCDE_PRECOMPILE_LIBS", "XCDE_PRECOMPILE_PIPELINES",
};
static const struct { const char *name, *value; } k_artifact_env[] = {
    {"LARECOMP_VK_GPL", "0"}, {"XCDE_VK_GPL", "1"},
    {"XCDE_BLOOM_PREFILTER", "1"}, {"XCDE_BLOOM_FILTER", "0"}, {"XCDE_GTAO", "0"},
    {"XCDE_CHAR_LIGHT", "0.5"}, {"XCDE_TMAA_TEXEL", "0"}, {"XCDE_GLSL_DIR", "override-glsl"},
    {"XCDE_PUSH_DESC", "0"}, {"XCDE_CANON", "0"}, {"XCDE_GPL_RETAIN", "1"}, {"LARECOMP_VK_D24", "0"},
    {"LARECOMP_SAVE_MSL", "1"}, {"XCDE_SPIRV_LOOSE", "1"}, {"LARECOMP_VK_NO_SPIRV_CACHE", "1"},
};

static void path_of(char *out, size_t cap, const char *rel) { snprintf(out, cap, "%s/%s", root, rel); }
static void write_file(const char *rel, const char *data, size_t n) {
    char p[1200];
    path_of(p, sizeof p, rel);
    FILE *f = fopen(p, "wb");
    CHECK(f != NULL);
    if (f) { CHECK(fwrite(data, 1, n, f) == n); fclose(f); }
}
static bool sig_of(const char *rel, char out[17]) {
    char p[1200];
    path_of(p, sizeof p, rel);
    return vk_precompile_sig(p, out);
}
static bool make_dir(const char *rel) {
    char p[1200];
    path_of(p, sizeof p, rel);
    return MKDIR(p) == 0;
}
static void remove_file(const char *rel) {
    char p[1200];
    path_of(p, sizeof p, rel);
    remove(p);
}
static bool hex16(const char *s) {
    if (strlen(s) != 16) return false;
    for (int i = 0; i < 16; i++) if (!isxdigit((unsigned char)s[i]) || isupper((unsigned char)s[i])) return false;
    return true;
}
static void clear_env(void) {
    for (size_t i = 0; i < sizeof k_sig_env / sizeof k_sig_env[0]; i++) set_env(k_sig_env[i], NULL);
}

int main(void) {
    char seed[1200];
    // A unique root per run: a killed earlier run must never leave files that make MKDIR fail (PID reuse is possible).
#ifdef _WIN32
    const char *tmp = getenv("TEMP");
    snprintf(root, sizeof root, "%s/monado-precompile-sig-%ld-%ld", tmp && *tmp ? tmp : ".", (long)PID, (long)time(NULL));
#else
    snprintf(root, sizeof root, "/tmp/monado-precompile-sig-%ld-%ld", (long)PID, (long)time(NULL));
#endif
    RMDIR(root);  // (best effort: only an empty leftover can be removed)
    CHECK(MKDIR(root) == 0);
    CHECK(make_dir("dump"));
    CHECK(make_dir("dump/romfs"));
    CHECK(make_dir("dump/romfs/sub"));
    CHECK(make_dir("nodump"));
    write_file("dump/romfs/a.wimdo", "one", 3);
    write_file("dump/romfs/sub/b.wismt", "two", 3);
    write_file("dump/romfs/c.wishp", "three", 5);
    write_file("seed.bin", "seed", 4);
    write_file("dump.nup", "NUP\x01" "package", 11);  // (a package is told by its magic, not by its name)
    write_file("plain.bin", "not a game root", 15);
    path_of(seed, sizeof seed, "seed.bin");
    set_env("XCDE_VARIANTS_SEED", seed);
    clear_env();

    // Stable, 16 lowercase hex.
    char a[17], b[17];
    CHECK(sig_of("dump", a));
    CHECK(hex16(a));
    CHECK(sig_of("dump", b));
    CHECK(!strcmp(a, b));

    // A changed file (same name, other content/size).
    write_file("dump/romfs/a.wimdo", "a longer content", 15);
    CHECK(sig_of("dump", b));
    CHECK(strcmp(a, b) != 0);

    // A same-size edit with a new mtime (the usual "patched container" case: the size alone does not tell).
    CHECK(sig_of("dump", a));
    write_file("dump/romfs/c.wishp", "THREE", 5);
    {
        char p[1200];
        path_of(p, sizeof p, "dump/romfs/c.wishp");
        CHECK(set_mtime(p, time(NULL) + 5) == 0);
    }
    CHECK(sig_of("dump", b));
    CHECK(strcmp(a, b) != 0);

    // An added file, a removed one, and a renamed one (the relative name is part of the signature: same bytes, other
    // program set). Removal is isolated so a regression there cannot hide behind another change.
    write_file("dump/romfs/sub/d.wiefb", "four", 4);
    CHECK(sig_of("dump", a));
    CHECK(strcmp(a, b) != 0);
    remove_file("dump/romfs/sub/d.wiefb");
    CHECK(sig_of("dump", b));
    CHECK(strcmp(a, b) != 0);
    char from[1200], to[1200];
    path_of(from, sizeof from, "dump/romfs/a.wimdo");
    path_of(to, sizeof to, "dump/romfs/e.wimdo");
    CHECK(rename(from, to) == 0);
    CHECK(sig_of("dump", a));
    CHECK(strcmp(a, b) != 0);

    // A .nup package is one file: its size (and time) is the fingerprint.
    char p1[17], p2[17];
    CHECK(sig_of("dump.nup", p1));
    write_file("dump.nup", "NUP\x01" "package with more", 21);
    CHECK(sig_of("dump.nup", p2));
    CHECK(strcmp(p1, p2) != 0);

    // A root is a dump folder with a romfs/ or a .nup package file; a plain file must answer "unreadable", never
    // "stale" (the callers then skip the doomed run).
    CHECK(!sig_of("plain.bin", a));
    CHECK(!sig_of("nodump", a));  // (a folder without romfs/)

    // The environment switches: presence and value are both inputs. Unset, empty ("optimisation off" for stage_opt) and
    // "0" must all give different signatures.
    CHECK(sig_of("dump", a));
    set_env("XCDE_FS_OPT", "");
    CHECK(sig_of("dump", b));
    CHECK(strcmp(a, b) != 0);  // [review] empty is not unset
    set_env("XCDE_FS_OPT", "0");
    CHECK(sig_of("dump", b));
    CHECK(strcmp(a, b) != 0);
    set_env("LARECOMP_VK_OPT", "0");
    CHECK(sig_of("dump", b));
    CHECK(strcmp(a, b) != 0);
    clear_env();
    CHECK(sig_of("dump", b));
    CHECK(!strcmp(a, b));  // (back to the baseline)
    set_env("XCDE_STATIC_STACKS", "0");  // a translator switch that changes the emitted GLSL
    CHECK(sig_of("dump", b));
    CHECK(strcmp(a, b) != 0);
    clear_env();
    set_env("XCDE_PRECOMPILE_PIPELINES", "0");  // a pass switch: a run without pipelines warmed a different cache
    CHECK(sig_of("dump", b));
    CHECK(strcmp(a, b) != 0);
    clear_env();

    // Every artifact-affecting switch added by the coverage audit independently changes the signature. One
    // representative value per switch is enough: the unset/empty/value contract is exercised above.
    CHECK(sig_of("dump", a));
    for (size_t i = 0; i < sizeof k_artifact_env / sizeof k_artifact_env[0]; i++) {
        set_env(k_artifact_env[i].name, k_artifact_env[i].value);
        CHECK(sig_of("dump", b));
        CHECK(strcmp(a, b) != 0);
        set_env(k_artifact_env[i].name, NULL);
        CHECK(sig_of("dump", b));
        CHECK(!strcmp(a, b));  // (back to the baseline)
    }

    // XCDE_GLSL_DIR's path is an environment input, but an in-place override edit must invalidate the signature too.
    CHECK(sig_of("dump", a));
    char overrides[1200], override_file[1200];
    path_of(overrides, sizeof overrides, "overrides");
    path_of(override_file, sizeof override_file, "overrides/test_fs.glsl");
    CHECK(MKDIR(overrides) == 0);
    write_file("overrides/test_fs.glsl", "one", 3);
    CHECK(set_mtime(override_file, 1234567890) == 0);
    set_env("XCDE_GLSL_DIR", overrides);
    CHECK(sig_of("dump", a));
    write_file("overrides/test_fs.glsl", "two", 3);
    CHECK(set_mtime(override_file, 1234567890) == 0);  // same size and mtime: only the bytes changed
    CHECK(sig_of("dump", b));
    CHECK(strcmp(a, b) != 0);
    clear_env();
    remove_file("overrides/test_fs.glsl");
    RMDIR(overrides);

    // The shipped variant seed: new recorded variants mean new jobs to warm.
    CHECK(sig_of("dump", a));
    write_file("seed.bin", "a bigger seed file", 18);
    CHECK(sig_of("dump", b));
    CHECK(strcmp(a, b) != 0);

    // Unreadable game files: nothing can be said about the cache.
    char missing[1200];
    path_of(missing, sizeof missing, "no-such-dump");
    CHECK(!vk_precompile_sig(missing, a));

    // Clean up (best effort: the test must not leave files behind).
    remove_file("overrides/test_fs.glsl");
    RMDIR(overrides);
    remove_file("dump/romfs/e.wimdo");
    remove_file("dump/romfs/c.wishp");
    remove_file("dump/romfs/sub/b.wismt");
    remove_file("dump.nup");
    remove_file("plain.bin");
    remove_file("seed.bin");
    {
        char p[1200];
        path_of(p, sizeof p, "dump/romfs/sub"); RMDIR(p);
        path_of(p, sizeof p, "dump/romfs"); RMDIR(p);
        path_of(p, sizeof p, "dump"); RMDIR(p);
        path_of(p, sizeof p, "nodump"); RMDIR(p);
    }
    RMDIR(root);

    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    puts("precompile signature: OK");
    return 0;
}
