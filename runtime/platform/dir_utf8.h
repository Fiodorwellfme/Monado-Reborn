// [XCDE] T278 (GitHub #218): UTF-8 directory enumeration that works in a non-ASCII working folder on Windows.
// mingw's opendir() first makes the path absolute with _fullpath / GetFullPathNameA (ANSI, MAX_PATH): under
// "C:\Users\João\" it searches "JoÃ£o" and returns no entries, so the game could not read targets/romfs, mods, save
// folders or the reports folder even though its relative file opens worked. These wrappers enumerate with
// FindFirstFileW, which resolves a relative path against the wide current folder, and give the names back as UTF-8.
// On other systems they are opendir/readdir/closedir.
//
// The whole name list is read when the folder is opened: FindFirstFile cannot be rewound, the game asks for a
// directory's entry count before reading it, and one folder's names are small next to the romfs index built from them.
#pragma once
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <wchar.h>
#include <windows.h>
#include "../host/utf8_path.h"  // xcde_utf8_to_wide / from_wide, XCDE_PATH_W

#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8
#endif

// [XCDE] T278: the CRT's opendir() set errno (fs.c's OpenDirectory reports it to the game). FindFirstFileW does not, so
// map its error the same way (a path that names a file is ERROR_DIRECTORY).
static inline void xcde_dir_errno_from_win32(DWORD error) {
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
#else
#include <dirent.h>
#endif

#define XCDE_DIR_NAME_MAX 1024  // (a Windows name has at most 255 UTF-16 units: 765 UTF-8 bytes)

typedef struct XcdeDirent {
    char d_name[XCDE_DIR_NAME_MAX];
    unsigned char d_type;
} XcdeDirent;

typedef struct XcdeDir {
    char **names;  // UTF-8 names, "." and ".." left out
    unsigned char *types;
    size_t count, cap, pos;
    XcdeDirent ent;
} XcdeDir;

static inline void xcde_closedir(XcdeDir *d) {
    size_t i;
    if (!d) return;
    for (i = 0; i < d->count; i++) free(d->names[i]);
    free(d->names);
    free(d->types);
    free(d);
}

// One more entry; false when it could not be stored (the caller then gives up on the whole folder).
static inline bool xcde_dir_add(XcdeDir *d, const char *name, unsigned char type) {
    char *copy;
    if (d->count == d->cap) {
        size_t ncap = d->cap ? d->cap * 2 : 16;
        char **names = (char **)realloc(d->names, ncap * sizeof *names);
        unsigned char *types = (unsigned char *)realloc(d->types, ncap);
        if (names) d->names = names;
        if (types) d->types = types;
        if (!names || !types) return false;
        d->cap = ncap;
    }
    copy = (char *)malloc(strlen(name) + 1);
    if (!copy) return false;
    memcpy(copy, name, strlen(name) + 1);
    d->names[d->count] = copy;
    d->types[d->count] = type;
    d->count++;
    return true;
}

static inline XcdeDirent *xcde_readdir(XcdeDir *d) {
    if (!d || d->pos >= d->count) return NULL;
    strncpy(d->ent.d_name, d->names[d->pos], sizeof d->ent.d_name - 1);
    d->ent.d_name[sizeof d->ent.d_name - 1] = 0;
    d->ent.d_type = d->types[d->pos];
    d->pos++;
    return &d->ent;
}
static inline void xcde_rewinddir(XcdeDir *d) { if (d) d->pos = 0; }
static inline long xcde_telldir(XcdeDir *d) { return d ? (long)d->pos : -1; }
static inline void xcde_seekdir(XcdeDir *d, long pos) { if (d && pos >= 0 && (size_t)pos <= d->count) d->pos = (size_t)pos; }

#ifdef _WIN32
static inline XcdeDir *xcde_opendir(const char *path) {
    wchar_t wpath[XCDE_PATH_W];
    WIN32_FIND_DATAW data;
    HANDLE find;
    XcdeDir *d;
    size_t n;
    if (!path) { errno = EINVAL; return NULL; }
    if (xcde_utf8_to_wide(path, wpath, XCDE_PATH_W) < 0) { errno = ENAMETOOLONG; return NULL; }
    n = wcslen(wpath);
    if (!n || n + 2 >= XCDE_PATH_W) { errno = ENAMETOOLONG; return NULL; }
    if (wpath[n - 1] != L'\\' && wpath[n - 1] != L'/') wpath[n++] = L'\\';
    wpath[n++] = L'*';
    wpath[n] = 0;
    find = FindFirstFileW(wpath, &data);
    if (find == INVALID_HANDLE_VALUE) { xcde_dir_errno_from_win32(GetLastError()); return NULL; }
    d = (XcdeDir *)calloc(1, sizeof *d);
    if (!d) { FindClose(find); errno = ENOMEM; return NULL; }
    for (;;) {
        char name[XCDE_DIR_NAME_MAX];
        if (wcscmp(data.cFileName, L".") && wcscmp(data.cFileName, L"..") &&
            xcde_utf8_from_wide(data.cFileName, name, sizeof name) >= 0 &&
            !xcde_dir_add(d, name, (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG)) {
            xcde_closedir(d);
            FindClose(find);
            errno = ENOMEM;
            return NULL;
        }
        if (!FindNextFileW(find, &data)) {
            DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_FILES) {
                xcde_closedir(d);
                FindClose(find);
                xcde_dir_errno_from_win32(error);
                return NULL;
            }
            break;
        }
    }
    FindClose(find);
    return d;
}
#else
static inline XcdeDir *xcde_opendir(const char *path) {
    DIR *dir;
    XcdeDir *d;
    struct dirent *e;
    if (!path) { errno = EINVAL; return NULL; }
    dir = opendir(path);
    if (!dir) return NULL;
    d = (XcdeDir *)calloc(1, sizeof *d);
    if (!d) { closedir(dir); errno = ENOMEM; return NULL; }
    for (;;) {
        errno = 0;  // (readdir reports the end with NULL and leaves errno alone; a real error must not look complete)
        e = readdir(dir);
        if (!e) break;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!xcde_dir_add(d, e->d_name, e->d_type)) { closedir(dir); xcde_closedir(d); errno = ENOMEM; return NULL; }
    }
    if (errno) {
        int err = errno;
        closedir(dir);
        xcde_closedir(d);
        errno = err;
        return NULL;  // (a partial listing must not be reported as the whole folder)
    }
    closedir(dir);
    return d;
}
#endif
