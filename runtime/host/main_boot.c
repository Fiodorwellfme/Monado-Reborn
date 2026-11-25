// Headless boot host (M3a): loads the modules, sets up the kernel and runs nn::init::Start, with strict logging of
// every service, SVC and import the game touches. No window, no renderer. Derived from LASRecomp runtime/main.c (MIT). [XCDE]
#include "../diag/diag.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#include "../gamelib.h"
#include "../hle.h"
#include "../kernel.h"
#include "../paths.h"
#include "../rt.h"
#include "../settings.h"
#include "../services/game_files.h"  // [XCDE] T280
#include "utf8_path.h"  // [XCDE] T278: UTF-8 paths through the wide APIs

const char *g_romfs_dir;

#if defined(XCDE_GFX) && defined(_WIN32)
// [XCDE] T194: laptops with two GPUs (NVIDIA Optimus / AMD switchable graphics) run a program on the integrated one
// unless the exe exports these; the driver reads them at start. (device.c then picks the discrete GPU among those
// Vulkan lists; LARECOMP_GPU=<n> overrides.)
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif

// T265: the same lifted camera code is also linked into the headless host.
uint32_t xcde_scene_aspect(uint32_t original, uint32_t camera) {
#if defined(XCDE_GFX)  // (the Linux window build too: with _WIN32 here its view never widened)
    extern uint32_t vk_scene_aspect(uint32_t original, uint32_t camera);
    return vk_scene_aspect(original, camera);
#else
    (void)camera;
    return original;
#endif
}

// [XCDE] GitHub #182: the targeting reticle clamps its projected screen position to the 16:9 UI bounds
// ([0, 1280] x [0, 720]), pinning it to the 16:9 edge once the target leaves the central 16:9 area on wider
// (or taller) windows. Widen the clamp to the active scene aspect ratio so the reticle stays on the enemy.
static float xcde_wide_factor_x(void) {
#if defined(XCDE_GFX)
    extern float vk_scene_aspect_ratio(void);
    float f = vk_scene_aspect_ratio() / (16.0f / 9.0f);
    return f > 1.0f ? f : 1.0f;
#else
    return 1.0f;
#endif
}

static float xcde_wide_factor_y(void) {
#if defined(XCDE_GFX)
    extern float vk_scene_aspect_ratio(void);
    float a = vk_scene_aspect_ratio();
    float f = a > 0.0f ? (16.0f / 9.0f) / a : 1.0f;
    return f > 1.0f ? f : 1.0f;
#else
    return 1.0f;
#endif
}

float xcde_reticle_clamp_x(float x, float w) {
    float f = xcde_wide_factor_x();
    float lo = w * 0.5f * (1.0f - f);
    float hi = w * 0.5f * (1.0f + f);
    return x < lo ? lo : x > hi ? hi : x;
}

float xcde_reticle_clamp_y(float y, float h) {
    float f = xcde_wide_factor_y();
    float lo = h * 0.5f * (1.0f - f);
    float hi = h * 0.5f * (1.0f + f);
    return y < lo ? lo : y > hi ? hi : y;
}

float xcde_reticle_edge_x(float edge) {
    float f = xcde_wide_factor_x();
    return edge <= 0.0f ? 640.0f * (1.0f - f) : edge * 0.5f * (1.0f + f);
}

// [XCDE] GitHub #182: widen the UI pane 2D frustum cull ([0, 1] x [0, 1] normalized 16:9 bounds)
// to match the active widened aspect ratio so off-16:9 world markers and edge-clamped reticles are not culled.
float xcde_ui_cull_adjust_x(float v, int is_min) {
    float pad = 0.5f * (xcde_wide_factor_x() - 1.0f);
    return is_min ? v - pad : v + pad;
}

float xcde_ui_cull_adjust_y(float v, int is_min) {
    float pad = 0.5f * (xcde_wide_factor_y() - 1.0f);
    return is_min ? v - pad : v + pad;
}

static void run_array(Cpu *c, uint64_t arr, uint64_t size) {
    for (uint64_t i = 0; i < size / 8; i++) {
        uint64_t fn = ((uint64_t *)arr)[i];
        if (fn) guest_call(c, fn);
    }
}
static void module_init(Cpu *c, Module *m) {
    LOG("init %s (DT_INIT %llx, %llu init_array entries)\n", m->name, (unsigned long long)m->init,
        (unsigned long long)(m->init_array_sz / 8));
    if (m->init) guest_call(c, m->init);
    else run_array(c, m->init_array, m->init_array_sz);
}
static void rtld_init_sdk(Cpu *c) { module_init(c, &g_modules[1]); }
static void rtld_init_main(Cpu *c) { module_init(c, &g_modules[0]); }

__attribute__((constructor(101))) static void reserve_guest_space(void) {
    bool reserved = plat_reserve_guest(GUEST_LO, GUEST_HI);
#ifdef _WIN32
    // [XCDE] T278 test hook: force the first start into the relaunch path (the ASLR collision is random, ~1 in 5).
    if (reserved && getenv("XCDE_TEST_RESERVE_FAIL") && atoi(getenv("XCDE_TEST_RESERVE_FAIL")) > 0 && !getenv("XCDE_RELAUNCH"))
        reserved = false;
#endif
    if (!reserved) {
        // [XCDE] T135: something the loader placed at random (ASLR) sits in the guest range in ~1 of 5 starts. A new
        // process gets a new layout: start this one again (same command line, same console), up to 8 times.
#ifdef _WIN32
        wchar_t n[16] = L"0";
        GetEnvironmentVariableW(L"XCDE_RELAUNCH", n, 16);
        int tries = _wtoi(n);
        rt_logf("[boot] guest address range %llx-%llx is not free (try %d)%s\n", (unsigned long long)GUEST_LO,
                (unsigned long long)GUEST_HI, tries + 1, tries < 8 ? ": starting again" : "");
        if (tries < 8) {
            _snwprintf(n, 16, L"%d", tries + 1);
            SetEnvironmentVariableW(L"XCDE_RELAUNCH", n);
            // Wide: the exe and the working folder may have non-ASCII characters (GitHub #218). GetCommandLineW keeps
            // the arguments exactly as this process got them.
            STARTUPINFOW si = {.cb = sizeof si};
            PROCESS_INFORMATION pi;
            wchar_t exe[XCDE_PATH_W];
            DWORD exe_len = GetModuleFileNameW(NULL, exe, XCDE_PATH_W);
            // the new process dies with this one (a test's timeout or a closed console must not leave it running)
            HANDLE job = CreateJobObjectW(NULL, NULL);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jl = {0};
            jl.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (job) SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jl, sizeof jl);
            if (exe_len && exe_len < XCDE_PATH_W &&
                CreateProcessW(exe, GetCommandLineW(), NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
                if (job) AssignProcessToJobObject(job, pi.hProcess);
                ResumeThread(pi.hThread);
                WaitForSingleObject(pi.hProcess, INFINITE);
                DWORD code = 1;
                GetExitCodeProcess(pi.hProcess, &code);
                ExitProcess(code);
            }
            if (job) CloseHandle(job);
        }
#else
        // [XCDE] T-LINUX: re-exec preserves arguments and the AppImage mount.
        const char *retry = getenv("XCDE_RELAUNCH");
        int tries = retry ? atoi(retry) : 0;
        if (tries < 8) {
            char n[16];
            snprintf(n, sizeof n, "%d", tries + 1);
            setenv("XCDE_RELAUNCH", n, 1);
            plat_exec(NULL);
        }
#endif
        rt_logf("FATAL: guest address range %llx-%llx is not free\n", (unsigned long long)GUEST_LO,
                (unsigned long long)GUEST_HI);
        exit(1);
    }
}

// [XCDE] Watchdog (T027, first version): every few seconds the state of every guest thread; XCDE_WATCHDOG=<seconds>.
#ifdef _WIN32
static DWORD WINAPI watchdog_main(LPVOID p)
#else
static void *watchdog_main(void *p)
#endif
{
    unsigned secs = (unsigned)(uintptr_t)p;
    for (;;) { plat_sleep_until_ns(plat_now_ns() + (uint64_t)secs * 1000000000); LOG("[wd] ---- %u s ----"" %c", secs, 10); kernel_dump_threads(stderr); }
}

// [XCDE] --game <dir> (launchers): xcde.ini, save/, cache/ and gamebuild/ are found from the folder above build/ (where
// "Play Monado.bat" starts us), but a launcher may start us from anywhere. On Windows, go to that install folder.
// (Linux AppImages/launchers already run from the writable data root, so the working folder is left alone there.)
#ifdef _WIN32
static bool enter_install_dir(void) {
    char dir[1024];
    if (!plat_exe_path(dir, sizeof dir)) return false;
    char *exe = strrchr(dir, '\\');
    if (!exe) return false;
    *exe = 0;  // <install>\build
    char *build = strrchr(dir, '\\');
    if (!build || _stricmp(build + 1, "build") != 0) return false;
    *build = 0;  // <install>
    if (dir[0] && dir[1] == ':' && !dir[2]) { dir[2] = '\\'; dir[3] = 0; }  // "C:" -> "C:\"
    return xcde_utf8_chdir(dir) == 0;
}
#endif


int main(int argc, char **argv) {
#ifdef _WIN32
    {  // [XCDE] T278: a non-ASCII path in the arguments must not be decoded in the ANSI code page (GitHub #218)
        char **utf8 = NULL;
        int n = plat_argv_utf8(argc, argv, &utf8);
        if (utf8) { argc = n; argv = utf8; }
    }
#endif
    if (argc >= 6 && !strcmp(argv[1], "--minidump")) return diag_minidump_helper(argc, argv);  // [XCDE] T028: crash helper process
#ifdef XCDE_GFX
    if (argc >= 3 && !strcmp(argv[1], "--precompile")) {  // [XCDE] T187: fill the shader cache from the RomFS, then exit
        extern int vk_precompile_main(const char *root);
        return vk_precompile_main(argv[2]);
    }
    // [XCDE] T282: is the precompiled shader cache current for this build and these game files? (the launcher's JUGAR
    // check and RecompLauncher's builder; no window, no Vulkan device, no scan - see precompile_sig.c)
    if (argc >= 3 && !strcmp(argv[1], "--precompile-status")) {
        extern int vk_precompile_status(const char *root);
        return vk_precompile_status(argv[2]);
    }
#endif
    plat_init();
    setvbuf(stderr, NULL, _IONBF, 0);
    bool game_opt = argc >= 2 && !strcmp(argv[1], "--game");  // [XCDE] the same dump folder by name, from a launcher
    int first = game_opt ? 2 : 1;
    static char ini_dump[1024];
    static char *ini_argv[2];
    if (argc <= first) {
        if (!game_opt && settings_read_game_dump(ini_dump, sizeof ini_dump)) {
            ini_argv[0] = argv[0];
            ini_argv[1] = ini_dump;
            argv = ini_argv;
            argc = 2;
        } else {
            rt_logf("usage: xcde_boot [--game] <dump: a folder with exefs/ and romfs/, or a .nup> [seconds]\n");
            return 2;
        }
    }
    const char *root = argv[first];
    int seconds = argc > first + 1 ? atoi(argv[first + 1]) : 20;
    char abs_root[1024];
    if (game_opt) {
        // Make the dump path independent of the launcher's working folder before changing ours.
        if (!rt_path_absolute(root, abs_root, sizeof abs_root)) {
            rt_logf("xcde: --game dump not found: %s\n", root);
            return 2;
        }
        root = abs_root;
#ifdef _WIN32
        if (!enter_install_dir())
            rt_logf("xcde: warning: could not switch to the install folder; settings and saves may go elsewhere\n");
#endif
    }
    char exefs[1200], romfs[1200], npdm_path[1200];
    snprintf(exefs, sizeof exefs, "%s/exefs", root);
    snprintf(romfs, sizeof romfs, "%s/romfs", root);
    snprintf(npdm_path, sizeof npdm_path, "%s/main.npdm", exefs);
    g_romfs_dir = strdup(romfs);
    // [XCDE] T280: the dump is a folder with exefs/ and romfs/, or a .nup package read in place; every reader below
    // goes through runtime/services/game_files.c with the same "<root>/exefs/..." and "<root>/romfs/..." paths.
    char why[768];
    if (!gf_init(root, why, sizeof why)) FATAL("Cannot read the game files: %s\n", why);
    if (gf_is_package()) LOG("[boot] game files from the package %s\n", root);

    diag_watchdog_start();
#ifdef _WIN32
    if (getenv("XCDE_WATCHDOG")) CreateThread(NULL, 0, watchdog_main, (LPVOID)(uintptr_t)atoi(getenv("XCDE_WATCHDOG")), 0, NULL);
#else
    const char *wd = getenv("XCDE_WATCHDOG");
    if (wd && atoi(wd) > 0) {
        pthread_t thread;
        if (!pthread_create(&thread, NULL, watchdog_main, (void *)(uintptr_t)atoi(wd))) pthread_detach(thread);
    }
#endif
    loader_load(exefs);
    // The recompiled game code: compiled into the executable by a source build, or into the library the launcher
    // prepares once from the player's own game files (release packages; tools/gamebuild/gamebuild.py).
    char no_code[256];
    if (!gamelib_load(root, no_code, sizeof no_code)) FATAL("Cannot start the game: %s\n", no_code);
    kernel_init();
    dispatch_init();

    uint32_t stack_size = 0x100000;
    int prio = 44;
    size_t meta_len = 0;
    uint8_t *meta = gf_read(npdm_path, &meta_len);  // [XCDE] T280: from the folder or the package
    if (meta && meta_len >= 0x80 && !memcmp(meta, "META", 4)) {
        prio = meta[0xE];
        stack_size = *(uint32_t *)(meta + 0x1C);
    }
    free(meta);
    LOG("main thread: prio %d, stack 0x%x\n", prio, stack_size);

    uint64_t start = loader_find_symbol("_ZN2nn4init5StartEmmPFvvES2_");
    if (!start) FATAL("nn::init::Start not found\n");
    uint64_t init_fn = hle_register("<rtld_init_sdk>", rtld_init_sdk);
    uint64_t fini_fn = hle_register("<rtld_init_main>", rtld_init_main);
    KThread *t = kernel_create_main_thread(start, stack_size, prio);
    kernel_run_main(t, t->handle, 0, init_fn, fini_fn);
#ifdef XCDE_GFX
    extern int ui_run(void);  // window + Vulkan presenter on the main thread (runtime/nvn/vulkan/device.c)
    LOG("[boot] running with the window\n");
    (void)seconds;
    return ui_run();
#else
    LOG("[boot] running; stopping after %d s\n", seconds);
    plat_sleep_until_ns(plat_now_ns() + (uint64_t)seconds * 1000000000ull);
    LOG("[boot] timeout reached, exiting\n");
    return 0;
#endif
}
