// [XCDE] T277 (GitHub #218): the launcher's paths are UTF-8, and runtime/host/utf8_path.h hands them to the wide
// Windows APIs. GitHub #218: under "C:\Users\João\" the narrow CRT and Win32 calls read "JoÃ£o" (CP1252), so
// CreateProcessA failed with error 267 and xcde.ini was never found. This test makes a folder with the same non-ASCII
// name the report used (plus an emoji: a UTF-16 surrogate pair) and opens, renames, replaces and removes a file in it
// through the wrappers; on Windows it also starts a child process there the way #218 did (CreateProcessA against a
// UTF-8 current directory) and the way the launcher now does (CreateProcessW against the converted one).
// Build/run: cc -std=c11 tests/native/test_utf8_paths.c -o t && ./t [folder]
// Linux: scripts/native_tests_linux.sh. Windows: scripts/native_tests.sh and tools/diag/validate_port.py, where the
// wide CreateDirectoryW below is what creates the folder and the wrappers are what find it.
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "../../runtime/host/ini_writer.h"  // the launcher's xcde.ini and the game's settings
#include "../../runtime/host/utf8_path.h"
#include "../../runtime/platform/dir_utf8.h"
#ifdef _WIN32
#include <dirent.h>  // (only to report what the CRT's opendir does in the same folder)
#include <winioctl.h>
#endif

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static int fails;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

#ifdef _WIN32
// #218's exact failure: the same child (cmd.exe, which Windows and Wine both have) started in a folder whose name has
// accents, once through CreateProcessA with the UTF-8 bytes and once through CreateProcessW with the UTF-16 path.
static void process_test(void) {
    wchar_t wcwd[XCDE_PATH_W], wabs[XCDE_PATH_W], wapp[MAX_PATH], cmdw[1024];
    char abs8[4200], appa[1024], cmda[1024], child[4200];
    DWORD ncwd = GetCurrentDirectoryW(XCDE_PATH_W, wcwd);
    if (!ncwd || ncwd >= XCDE_PATH_W || wcslen(wcwd) + wcslen(L"\\utf8-cmd-Jo\u00e3o-\U0001F600") >= XCDE_PATH_W) {
        printf("FAIL cannot use the current directory for the process test\n");
        fails++;
        return;
    }
    wcscpy(wabs, wcwd);  // (the absolute path CreateProcess wants for its working folder)
    wcscat(wabs, L"\\utf8-cmd-Jo\u00e3o-\U0001F600");
    if (!CreateDirectoryW(wabs, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        printf("FAIL cannot create the folder for the process test (error %lu)\n", (unsigned long)GetLastError());
        fails++;
        return;
    }
    DWORD sys = GetSystemDirectoryW(wapp, MAX_PATH);
    if (xcde_utf8_from_wide(wabs, abs8, sizeof abs8) < 0 || !sys || sys >= MAX_PATH) {
        printf("FAIL cannot convert the folder for the process test\n");
        fails++;
        return;
    }
    wcscat(wapp, L"\\cmd.exe");
    if (xcde_utf8_from_wide(wapp, appa, sizeof appa) < 0) {
        printf("FAIL cannot convert the command interpreter's path\n");
        fails++;
        return;
    }
    // A: the report's call. The UTF-8 bytes are read in the system code page, so the folder resolves to "JoÃ£o" and
    // does not exist: 267 (ERROR_DIRECTORY). A machine with the UTF-8 code page enabled starts it, so this is reported
    // rather than asserted; the W call below is the one that must work.
    STARTUPINFOA sia = {0};
    sia.cb = sizeof sia;
    PROCESS_INFORMATION pia = {0};
    snprintf(cmda, sizeof cmda, "\"%s\" /c echo ok > child-a.txt", appa);
    BOOL oka = CreateProcessA(appa, cmda, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, abs8, &sia, &pia);
    printf("CreateProcessA with the UTF-8 folder: %s (%lu, system code page %u)\n", oka ? "started" : "failed",
           oka ? 0UL : (unsigned long)GetLastError(), (unsigned)GetACP());
    if (oka) {
        WaitForSingleObject(pia.hProcess, 20000);
        CloseHandle(pia.hProcess);
        CloseHandle(pia.hThread);
    }
    // W: the fix. The folder is UTF-16 "João" in Windows' own encoding, so the child starts and writes its file there.
    STARTUPINFOW siw = {0};
    siw.cb = sizeof siw;
    PROCESS_INFORMATION piw = {0};
    _snwprintf(cmdw, 1024, L"\"%s\" /c echo ok > child-w.txt", wapp);
    CHECK(CreateProcessW(wapp, cmdw, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, wabs, &siw, &piw));
    CHECK(WaitForSingleObject(piw.hProcess, 20000) == WAIT_OBJECT_0);
    DWORD code = 1;
    GetExitCodeProcess(piw.hProcess, &code);
    CloseHandle(piw.hProcess);
    CloseHandle(piw.hThread);
    CHECK(code == 0);
    snprintf(child, sizeof child, "%s/child-w.txt", abs8);
    FILE *f = xcde_utf8_fopen(child, "r");  // (the child wrote it in its working folder: this folder)
    CHECK(f != NULL);
    if (f) {
        fclose(f);
        CHECK(xcde_utf8_remove(child) == 0);
    }
    snprintf(child, sizeof child, "%s/child-a.txt", abs8);  // (whatever the ANSI attempt left, if it started)
    xcde_utf8_remove(child);
    RemoveDirectoryW(wabs);
}
#endif

int main(int argc, char **argv) {
    // (the argument is ASCII in every caller: Windows hands main() its argv in the system code page, which is exactly
    // what the launcher does not do - its root comes from SDL_GetBasePath as UTF-8)
    const char *base = argc > 1 ? argv[1] : "build";
    // João + a grinning face, in UTF-8 (the bytes a joão-based install folder has)
    char root[4096], dir[4200], file[4400], file2[4400], tmp[4400], sparse[4400], text[16];
#ifdef _WIN32
    // The launcher's root is absolute: complete a relative folder with the current directory, which is where the
    // non-ASCII part of an install path lives ("C:\Users\João\...").
    char cwd8[4200];
    wchar_t wcwd[XCDE_PATH_W];
    DWORD ncwd = GetCurrentDirectoryW(XCDE_PATH_W, wcwd);
    bool have_cwd = ncwd && ncwd < XCDE_PATH_W && xcde_utf8_from_wide(wcwd, cwd8, sizeof cwd8) > 0;
    if (strchr(base, ':') || base[0] == '\\' || base[0] == '/' || !have_cwd)
        snprintf(root, sizeof root, "%s", base);  // (an absolute base as given, or no absolute folder to prepend)
    else snprintf(root, sizeof root, "%s\\%s", cwd8, base);
#else
    snprintf(root, sizeof root, "%s", base);
#endif
    snprintf(dir, sizeof dir, "%s/utf8-Jo\xC3\xA3" "o-\xF0\x9F\x98\x80", root);
    snprintf(file, sizeof file, "%s/file.txt", dir);
    snprintf(file2, sizeof file2, "%s/renamed.txt", dir);
    snprintf(tmp, sizeof tmp, "%s/file.tmp", dir);
    snprintf(sparse, sizeof sparse, "%s/sparse.bin", dir);
#ifdef _WIN32
    wchar_t wroot[XCDE_PATH_W], wdir[XCDE_PATH_W];
    CHECK(xcde_utf8_to_wide(root, wroot, XCDE_PATH_W) >= 0);
    CHECK(xcde_utf8_to_wide(dir, wdir, XCDE_PATH_W) >= 0);
    CreateDirectoryW(wroot, NULL);  // (the caller's folder, when it does not exist yet: a best effort, as mkdir below)
    if (!CreateDirectoryW(wdir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        printf("FAIL cannot create %s (error %lu)\n", dir, (unsigned long)GetLastError());
        return 1;
    }
    // The conversion itself: these UTF-8 bytes are one 'ã', not the two CP1252 characters the …A APIs made of them.
    wchar_t w[32];
    CHECK(xcde_utf8_to_wide("Jo\xC3\xA3" "o", w, 32) == 4);
    CHECK(!wcscmp(w, L"Jo\u00e3o"));
    CHECK(xcde_utf8_to_wide("Jo\xC3\xA3" "o", NULL, 0) == 4);  // (needed length, terminator excluded)
    char back[32];
    CHECK(xcde_utf8_from_wide(L"Jo\u00e3o", back, sizeof back) == 5);  // J o C3 A3 o
    CHECK(!strcmp(back, "Jo\xC3\xA3" "o"));
    // The same folder through the CRT's narrow fopen: on a CP1252 system it looks for "JoÃ£o" and fails (that is why
    // xcde.ini was never found). A machine with the UTF-8 code page enabled passes here too, so it is reported, not
    // asserted.
    FILE *plain = fopen(file, "w");
    printf("fopen() with the UTF-8 path: %s (system code page %u)\n", plain ? "worked" : "failed", (unsigned)GetACP());
    if (plain) {
        fclose(plain);
        xcde_utf8_remove(file);
    }
#else
    mkdir(root, 0777);  // (best effort: the caller's folder usually exists already)
    if (mkdir(dir, 0777) != 0 && errno != EEXIST) {
        printf("FAIL cannot create %s (%s)\n", dir, strerror(errno));
        return 1;
    }
#endif
    // write, read back
    FILE *f = xcde_utf8_fopen(file, "w");
    CHECK(f != NULL);
    if (f) { CHECK(fputs("first", f) >= 0); CHECK(fclose(f) == 0); }
    text[0] = 0;
    f = xcde_utf8_fopen(file, "r");
    CHECK(f != NULL);
    if (f) { CHECK(fgets(text, sizeof text, f) != NULL); fclose(f); }
    CHECK(!strcmp(text, "first"));

    // [XCDE] T278: enumerating a relative folder from inside the non-ASCII one. The CRT's opendir() makes the path
    // absolute with the ANSI GetFullPathNameA and finds nothing (the game could not read targets/romfs or saves,
    // GitHub #218); the wide listing resolves the relative path against the wide working folder.
    char cwd[4200];
    CHECK(xcde_utf8_getcwd(cwd, sizeof cwd) != NULL);
    CHECK(xcde_utf8_chdir(dir) == 0);
    XcdeDir *dh = xcde_opendir(".");
    int listed = 0;
    CHECK(dh != NULL);
    if (dh) {
        XcdeDirent *entry;
        while ((entry = xcde_readdir(dh))) if (!strcmp(entry->d_name, "file.txt")) listed = 1;
        xcde_closedir(dh);
    }
    CHECK(listed);
    // [XCDE] T278: the CRT's opendir() set errno; the wide shim maps FindFirstFileW's error the same way (fs.c's
    // OpenDirectory reports errno to the game, so a missing folder must stay ENOENT rather than a stale value).
    errno = 0;
    CHECK(xcde_opendir("xcde-does-not-exist") == NULL);
    CHECK(errno == ENOENT);
#ifdef _WIN32
    int crt_listed = 0;
    DIR *crt_dir = opendir(".");
    if (crt_dir) { while (readdir(crt_dir)) crt_listed++; closedir(crt_dir); }
    printf("opendir/readdir in the non-ASCII folder: CRT %d entries, wide %s\n", crt_listed, listed ? "found file.txt" : "missing");
    // getenv() decodes in the ANSI code page too: the wide read gives the same text back.
    SetEnvironmentVariableW(L"XCDE_TEST_UTF8", L"Jo\u00e3o-\U0001F600");
    char env[64];
    CHECK(xcde_utf8_getenv("XCDE_TEST_UTF8", env, sizeof env));
    CHECK(!strcmp(env, "Jo\xC3\xA3" "o-\xF0\x9F\x98\x80"));
#endif
    CHECK(xcde_utf8_chdir(cwd) == 0);

    // A sparse 3 GiB file exercises the 64-bit metadata path that the Windows CRT's _wstat64i32 cannot represent.
    // The stock SPIR-V pack already exceeds 2 GiB, so this is a real cache-validation case, not a synthetic edge only.
    const uint64_t large_size = 3ull << 30;
#ifdef _WIN32
    wchar_t wsparse[XCDE_PATH_W];
    CHECK(xcde_utf8_to_wide(sparse, wsparse, XCDE_PATH_W) >= 0);
    HANDLE hs = CreateFileW(wsparse, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(hs != INVALID_HANDLE_VALUE);
    if (hs != INVALID_HANDLE_VALUE) {
        DWORD ignored = 0;
        CHECK(DeviceIoControl(hs, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &ignored, NULL));
        LARGE_INTEGER end;
        end.QuadPart = (LONGLONG)large_size;
        CHECK(SetFilePointerEx(hs, end, NULL, FILE_BEGIN));
        CHECK(SetEndOfFile(hs));
        CloseHandle(hs);
    }
#else
    int fd = open(sparse, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    CHECK(fd >= 0);
    if (fd >= 0) {
        CHECK(ftruncate(fd, (off_t)large_size) == 0);
        close(fd);
    }
#endif
    XcdeFileInfo large_info = {0};
    CHECK(xcde_utf8_file_info(sparse, &large_info));
    CHECK(!large_info.isdir);
    CHECK(large_info.isreg);
    CHECK(large_info.size == large_size);
    CHECK(xcde_utf8_remove(sparse) == 0);

    // rename, then the same-directory replacement (T247)
    CHECK(xcde_utf8_rename(file, file2) == 0);
    CHECK(xcde_utf8_fopen(file, "r") == NULL);  // (the old name is gone)
    f = xcde_utf8_fopen(tmp, "w");
    CHECK(f != NULL);
    if (f) { CHECK(fputs("second", f) >= 0); CHECK(fclose(f) == 0); }
    CHECK(xcde_utf8_replace(tmp, file2) == 0);
    text[0] = 0;
    f = xcde_utf8_fopen(file2, "r");
    CHECK(f != NULL);
    if (f) { CHECK(fgets(text, sizeof text, f) != NULL); fclose(f); }
    CHECK(!strcmp(text, "second"));

    // the shared INI writer (xcde.ini, the game's settings) saves through the same wrappers: temporary file, merge,
    // same-directory replacement
    char ini_tmp[4300];
    FILE *ini = xcde_ini_begin(tmp, ini_tmp, sizeof ini_tmp);
    CHECK(ini != NULL);
    if (ini) {
        CHECK(fputs("known = value\n", ini) >= 0);
        CHECK(xcde_ini_commit(ini, ini_tmp, file2));  // (replaces renamed.txt: its line has no key and is dropped)
    }
    text[0] = 0;
    f = xcde_utf8_fopen(file2, "r");
    CHECK(f != NULL);
    if (f) { CHECK(fgets(text, sizeof text, f) != NULL); fclose(f); }
    CHECK(!strcmp(text, "known = value\n"));

    // remove: the same reports as the CRT, and nothing is left for the next run
    CHECK(xcde_utf8_remove(file2) == 0);
    CHECK(xcde_utf8_fopen(file2, "r") == NULL);
    CHECK(xcde_utf8_remove(file2) != 0);
#ifdef _WIN32
    process_test();
    CHECK(RemoveDirectoryW(wdir) != 0);
#else
    CHECK(rmdir(dir) == 0);
#endif
    printf(fails ? "utf8_paths: %d failure(s)\n" : "utf8_paths: all passed\n", fails);
    return fails ? 1 : 0;
}
