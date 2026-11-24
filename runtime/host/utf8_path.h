// [XCDE] T277/T278: paths are UTF-8 everywhere in this project (SDL_GetBasePath, argv, the settings files), but Windows'
// ANSI (…A) APIs and the CRT read narrow path strings in the system code page. Under "C:\Users\João\" the bytes for
// 'ã' (C3 A3 in UTF-8) were taken as CP1252 "JoÃ£o", a folder that does not exist: CreateProcessA failed with
// error 267, fopen() could not find xcde.ini, and opendir() (which absolutizes with the ANSI GetFullPathNameA) found
// no entries (GitHub #218). These wrappers convert the path to UTF-16, the encoding Windows stores paths in, and call
// the wide API; on other systems they are the libc call they replace.
#pragma once
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <wchar.h>  // _wrename (in stdio.h only on some CRTs; _wfopen/_wremove are there too)

// 64-bit file metadata shared by the cache validator, game-file reader and signature walk.
typedef struct { bool isdir, isreg; uint64_t size; int64_t mtime; } XcdeFileInfo;

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <direct.h>   // _wchdir, _wgetcwd, _wmkdir
#include <fcntl.h>    // _wopen
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>  // _wstat
#include <windows.h>

// UTF-16 code units for one path: MAX_PATH is 260 and the wide Win32 APIs accept longer paths than the narrow ones.
#define XCDE_PATH_W 1024

// Win32 error -> errno, for the helpers that report failures through errno (a missing path, not every failure, is safe
// to treat as "this entry does not exist").
static inline void xcde_errno_from_win32(DWORD error) {
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME: errno = ENOENT; break;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION: errno = EACCES; break;
    case ERROR_DIRECTORY: errno = ENOTDIR; break;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY: errno = ENOMEM; break;
    default: errno = EIO; break;
    }
}

// UTF-8 -> UTF-16 into out (cap code units, terminator included). Returns the units written without the terminator,
// or -1 when it does not fit. out == NULL with cap == 0 returns the needed length (as snprintf does). Bytes that are
// not UTF-8 become U+FFFD, as MultiByteToWideChar(CP_UTF8) defines.
static inline int xcde_utf8_to_wide(const char *s, wchar_t *out, size_t cap) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, out, (int)cap);
    return n > 0 ? n - 1 : -1;
}
// The reverse: UTF-16 -> UTF-8 (the file names a wide directory listing returns).
static inline int xcde_utf8_from_wide(const wchar_t *s, char *out, size_t cap) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, out, (int)cap, NULL, NULL);
    return n > 0 ? n - 1 : -1;
}

// fopen() / remove() / rename() with a UTF-8 path (same returns as the CRT's).
// open() with a UTF-8 path (the file-service descriptors and the romfs reads).
static inline int xcde_utf8_open(const char *path, int flags, int mode) {
    wchar_t wpath[XCDE_PATH_W];
    if (xcde_utf8_to_wide(path, wpath, XCDE_PATH_W) < 0) return -1;
    return _wopen(wpath, flags, mode);
}
static inline FILE *xcde_utf8_fopen(const char *path, const char *mode) {
    wchar_t wpath[XCDE_PATH_W], wmode[8];
    size_t i;
    if (xcde_utf8_to_wide(path, wpath, XCDE_PATH_W) < 0) return NULL;
    for (i = 0; mode[i] && i + 1 < sizeof wmode / sizeof wmode[0]; i++) wmode[i] = (wchar_t)(unsigned char)mode[i];
    wmode[i] = 0;
    return _wfopen(wpath, wmode);
}
static inline int xcde_utf8_remove(const char *path) {
    wchar_t wpath[XCDE_PATH_W];
    if (xcde_utf8_to_wide(path, wpath, XCDE_PATH_W) < 0) return -1;
    return _wremove(wpath);
}
static inline int xcde_utf8_rename(const char *from, const char *to) {
    wchar_t wfrom[XCDE_PATH_W], wto[XCDE_PATH_W];
    if (xcde_utf8_to_wide(from, wfrom, XCDE_PATH_W) < 0 || xcde_utf8_to_wide(to, wto, XCDE_PATH_W) < 0) return -1;
    return _wrename(wfrom, wto);
}
// [XCDE] T247: the same-directory replacement, 0 when the new file took the old one's place.
static inline int xcde_utf8_replace(const char *tmp, const char *path) {
    wchar_t wtmp[XCDE_PATH_W], wpath[XCDE_PATH_W];
    if (xcde_utf8_to_wide(tmp, wtmp, XCDE_PATH_W) < 0 || xcde_utf8_to_wide(path, wpath, XCDE_PATH_W) < 0) return -1;
    return MoveFileExW(wtmp, wpath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
}
// [XCDE] T278: stat() / mkdir() / chdir() / getcwd() with a UTF-8 path (the CRT's equivalents absolutize or convert in the ANSI
// code page; a relative path is fine, an absolute one with accents is not).
static inline int xcde_utf8_stat(const char *path, struct stat *st) {
    wchar_t wpath[XCDE_PATH_W];
    if (xcde_utf8_to_wide(path, wpath, XCDE_PATH_W) < 0) return -1;
    return wstat(wpath, st);  // (matches struct stat; _wstat maps to the CRT's _wstat64i32)
}
static inline int xcde_utf8_mkdir(const char *path) {
    wchar_t wpath[XCDE_PATH_W];
    if (xcde_utf8_to_wide(path, wpath, XCDE_PATH_W) < 0) return -1;
    return _wmkdir(wpath);
}
static inline int xcde_utf8_rmdir(const char *path) {
    wchar_t wpath[XCDE_PATH_W];
    if (xcde_utf8_to_wide(path, wpath, XCDE_PATH_W) < 0) return -1;
    return _wrmdir(wpath);
}
static inline int xcde_utf8_chdir(const char *path) {
    wchar_t wpath[XCDE_PATH_W];
    if (xcde_utf8_to_wide(path, wpath, XCDE_PATH_W) < 0) return -1;
    return _wchdir(wpath);
}
static inline char *xcde_utf8_getcwd(char *buf, size_t cap) {
    wchar_t wpath[XCDE_PATH_W];
    if (!_wgetcwd(wpath, XCDE_PATH_W)) return NULL;
    return xcde_utf8_from_wide(wpath, buf, cap) >= 0 ? buf : NULL;
}
// getenv() in UTF-8 (the CRT's getenv decodes the value in the ANSI code page): false when unset, empty or too long.
static inline bool xcde_utf8_getenv(const char *name, char *out, size_t cap) {
    wchar_t wname[256];
    DWORD n;
    if (!name || !out || !cap || xcde_utf8_to_wide(name, wname, 256) < 0) return false;
    n = GetEnvironmentVariableW(wname, NULL, 0);
    if (!n) return false;
    wchar_t *value = (wchar_t *)malloc((size_t)n * sizeof *value);
    if (!value) return false;
    bool ok = GetEnvironmentVariableW(wname, value, n) != 0;
    int len = ok ? xcde_utf8_from_wide(value, out, cap) : -1;
    free(value);
    return len > 0;
}
// LoadLibraryW: a module path with accents (an override, a component next to the exe) would not load with ...A.
static inline void *xcde_utf8_load_library(const char *path) {
    wchar_t wpath[XCDE_PATH_W];
    if (xcde_utf8_to_wide(path, wpath, XCDE_PATH_W) < 0) return NULL;
    return (void *)LoadLibraryW(wpath);
}
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static inline int xcde_utf8_open(const char *path, int flags, int mode) { return open(path, flags, mode); }
static inline FILE *xcde_utf8_fopen(const char *path, const char *mode) { return fopen(path, mode); }
static inline int xcde_utf8_remove(const char *path) { return remove(path); }
static inline int xcde_utf8_rename(const char *from, const char *to) { return rename(from, to); }
static inline int xcde_utf8_replace(const char *tmp, const char *path) { return rename(tmp, path); }
static inline int xcde_utf8_stat(const char *path, struct stat *st) { return stat(path, st); }
static inline int xcde_utf8_mkdir(const char *path) { return mkdir(path, 0755); }
static inline int xcde_utf8_rmdir(const char *path) { return rmdir(path); }
static inline int xcde_utf8_chdir(const char *path) { return chdir(path); }
static inline char *xcde_utf8_getcwd(char *buf, size_t cap) { return getcwd(buf, cap); }
static inline bool xcde_utf8_getenv(const char *name, char *out, size_t cap) {
    const char *value = getenv(name);
    if (!value || !*value || strlen(value) >= cap) return false;
    memcpy(out, value, strlen(value) + 1);
    return true;
}
static inline void *xcde_utf8_load_library(const char *path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
#endif

// The Windows CRT's struct stat / _wstat64i32 truncates sizes above 2 GiB (the SPIR-V pack and future/modded game
// containers can be larger); use the wide API there. POSIX stat already has a 64-bit off_t.
static inline bool xcde_utf8_file_info(const char *path, XcdeFileInfo *out) {
#ifdef _WIN32
    wchar_t wpath[XCDE_PATH_W];
    if (!path || !out) { errno = EINVAL; return false; }
    if (xcde_utf8_to_wide(path, wpath, XCDE_PATH_W) < 0) { errno = ENAMETOOLONG; return false; }
    HANDLE h = CreateFileW(wpath, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) { xcde_errno_from_win32(GetLastError()); return false; }
    BY_HANDLE_FILE_INFORMATION fi;
    bool ok = GetFileInformationByHandle(h, &fi) != 0;
    CloseHandle(h);
    if (!ok) { errno = EIO; return false; }
    out->isdir = (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    out->isreg = !out->isdir && !(fi.dwFileAttributes & FILE_ATTRIBUTE_DEVICE);
    out->size = ((uint64_t)fi.nFileSizeHigh << 32) | fi.nFileSizeLow;
    ULARGE_INTEGER t;
    t.LowPart = fi.ftLastWriteTime.dwLowDateTime;
    t.HighPart = fi.ftLastWriteTime.dwHighDateTime;
    out->mtime = (int64_t)(t.QuadPart / 10000000ull) - 11644473600ll;
    return true;
#else
    struct stat sb;
    if (!path || !out) { errno = EINVAL; return false; }
    if (stat(path, &sb)) return false;
    out->isdir = S_ISDIR(sb.st_mode);
    out->isreg = S_ISREG(sb.st_mode);
    out->size = (uint64_t)sb.st_size;
    out->mtime = (int64_t)sb.st_mtime;
    return true;
#endif
}
