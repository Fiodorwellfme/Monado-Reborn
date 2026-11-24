"""GitHub #218 (T278): the game's own Windows paths and directory listings are UTF-8/UTF-16, not ANSI.

Source contracts for the runtime half of the fix. The behaviour (a relative folder enumerated from inside a non-ASCII
working folder, the relaunch, the minidump helper, the log file) is exercised by tests/native/test_utf8_paths.c and,
for the game itself, by the Wine route in docs/evidence/2026-11-23-issue-218/summary.md.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def text(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def code_only(source):
    """Comments and string/char literals blanked: the checks are about calls, not prose."""
    out, i, n = [], 0, len(source)
    while i < n:
        c = source[i]
        if c == "/" and source[i + 1:i + 2] == "/":
            while i < n and source[i] != "\n":
                i += 1
        elif c == "/" and source[i + 1:i + 2] == "*":
            i += 2
            while i + 1 < n and source[i:i + 2] != "*/":
                i += 1
            i += 2
            out.append(" ")
        elif c in "\"'":
            quote = c
            i += 1
            while i < n:
                if source[i] == "\\":
                    i += 2
                    continue
                if source[i] == quote:
                    i += 1
                    break
                i += 1
            out.append('""')
        else:
            out.append(c)
            i += 1
    return "".join(out)


class RuntimeUtf8Paths(unittest.TestCase):
    def assertHas(self, needle, haystack, what):
        self.assertTrue(needle in haystack, what)

    def assertHasNot(self, needle, haystack, what):
        self.assertFalse(needle in haystack, what)

    def test_the_crt_directory_listing_is_replaced_where_paths_can_have_accents(self):
        # opendir() absolutizes with GetFullPathNameA: in a non-ASCII working folder it returns no entries, so the game
        # could not read targets/romfs, mods, saves or reports (GitHub #218).
        for rel, include in (("runtime/services/fs.c", "../platform/dir_utf8.h"),
                             ("runtime/services/romfs_virtual.c", "../platform/dir_utf8.h"),
                             ("runtime/nvn/vulkan/precompile.c", "../../platform/dir_utf8.h"),
                             ("runtime/paths.c", "platform/dir_utf8.h")):
            code = code_only(text(rel))
            self.assertHas(f'#include "{include}"', text(rel), rel)
            for call in ("opendir", "readdir", "closedir", "telldir", "seekdir", "rewinddir"):
                self.assertIsNone(re.search(r"(?<![\w.])" + call + r"\(", code), f"{rel}: {call}")
            self.assertHas("xcde_opendir(", code, rel)

    def test_the_directory_shim_enumerates_with_findfirstfilew(self):
        shim = text("runtime/platform/dir_utf8.h")
        self.assertHas("FindFirstFileW(", shim, "wide enumeration")
        self.assertHas("xcde_utf8_from_wide(", shim, "names as UTF-8")
        self.assertHas("xcde_dir_errno_from_win32(", shim, "errno like opendir")
        self.assertHas("ERROR_DIRECTORY", shim, "a file passed as a folder")
        self.assertHas("static inline long xcde_telldir(", shim, "telldir")
        self.assertHas("static inline void xcde_seekdir(", shim, "seekdir")
        self.assertHas("FILE_ATTRIBUTE_DIRECTORY", shim, "d_type from the attributes")

    def test_the_runtime_path_helpers_are_wide_on_windows(self):
        header = text("runtime/host/utf8_path.h")
        for wide in ("wstat(", "_wchdir(", "_wgetcwd(", "_wmkdir(", "GetEnvironmentVariableW(", "LoadLibraryW(",
                     "_wfopen(", "_wremove(", "_wrename(", "_wopen(", "MoveFileExW(", "CreateFileW(",
                     "GetFileInformationByHandle("):
            self.assertHas(wide, header, wide)

    def test_windows_layer_uses_the_wide_apis(self):
        code = code_only(text("runtime/platform/windows.c"))
        for ansi in ("GetModuleFileNameA", "MoveFileExA", "CreateProcessA", "SetCurrentDirectoryA"):
            self.assertHasNot(ansi, code, ansi)
        self.assertHas("GetModuleFileNameW(", code, "module path")
        self.assertHas("_wfreopen(", code, "lasrecomp.log")
        self.assertHas("SymInitializeW(", code, "PDB folder")
        self.assertHas("int plat_argv_utf8(", code, "argv")
        self.assertHas("CommandLineToArgvW(", code, "wide command line")
        self.assertHas("if (failed)", code, "keep the complete argv if a conversion fails")

    def test_relaunch_and_install_folder_are_wide(self):
        code = code_only(text("runtime/host/main_boot.c"))
        for ansi in ("GetModuleFileNameA", "CreateProcessA", "GetCommandLineA", "SetEnvironmentVariableA",
                     "GetEnvironmentVariableA", "SetCurrentDirectoryA"):
            self.assertHasNot(ansi, code, ansi)
        self.assertHas("CreateProcessW(", code, "relaunch")
        self.assertHas("GetCommandLineW()", code, "same command line")
        self.assertHas("plat_argv_utf8(", code, "argv from the wide command line")
        self.assertHas('"XCDE_TEST_RESERVE_FAIL"', text("runtime/host/main_boot.c"), "relaunch test hook")

    def test_crash_minidump_helper_is_started_wide(self):
        code = code_only(text("runtime/diag/crashdump.c"))
        for ansi in ("CreateFileA", "CreateProcessA", "GetModuleFileNameA"):
            self.assertHasNot(ansi, code, ansi)
        self.assertHas("CreateProcessW(", code, "minidump helper")
        self.assertHas("CreateFileW(", code, "dump file")

    def test_game_library_settings_and_fsr4_read_utf8_paths(self):
        self.assertHas("xcde_utf8_load_library(", code_only(text("runtime/gamelib.c")), "gamelib")
        self.assertHas("xcde_utf8_getenv(", code_only(text("runtime/gamelib.c")), "LARECOMP_* paths")
        self.assertHas("xcde_utf8_fopen(ini_path()", code_only(text("runtime/host/settings_ini.c")), "xcde.ini")
        self.assertHas("xcde_utf8_getenv(", code_only(text("runtime/host/settings_ini.c")), "XCDE_INI")
        fsr4 = code_only(text("runtime/nvn/vulkan/fsr4.c"))
        self.assertHas("xcde_utf8_load_library(", fsr4, "FSR 4 component")
        self.assertHasNot("GetModuleFileNameA", fsr4, "FSR 4 beside-exe path")

    def test_absolute_dump_paths_are_opened_wide(self):
        # --game <absolute dump>: argv is UTF-8 now, and the loader, the romfs folder test, the romfs descriptors and the
        # virtual romfs scan must not fall back to the ANSI CRT either.
        # T280: the loader, the shader scan and the romfs edits read through runtime/services/game_files.c (a folder or
        # a .nup package), which opens and tests paths through the wrappers.
        self.assertHas("gf_read(", code_only(text("runtime/loader.c")), "exefs loading")
        game_files = code_only(text("runtime/services/game_files.c"))
        self.assertHas("xcde_utf8_open(", game_files, "game file reads")
        self.assertHas("xcde_utf8_file_info(", game_files, "64-bit game file metadata")
        fs = code_only(text("runtime/services/fs.c"))
        self.assertHas("xcde_utf8_stat(g_romfs_dir", fs, "romfs folder test")
        self.assertHas("xcde_utf8_open(", fs, "romfs/bin descriptors")
        self.assertHas("xcde_utf8_open(", code_only(text("runtime/services/romfs_fd_cache.h")), "romfs descriptor cache")
        self.assertHas("xcde_utf8_stat(", code_only(text("runtime/services/romfs_virtual.c")), "romfs scan")
        # The precompiler resolves romfs/<file> through game_files and reads the returned source: an absolute non-ASCII
        # --precompile root must scan them without falling back to the ANSI CRT.
        pre = code_only(text("runtime/nvn/vulkan/precompile.c"))
        self.assertHas("xcde_utf8_file_info(p", pre, "64-bit precompile scan metadata")
        self.assertHas("gf_source(path", pre, "precompile shader source")
        self.assertHas("gf_pread(&src", pre, "precompile shader read")
        self.assertHas('xcde_utf8_getenv("XCDE_PRECOMPILE_LIST"', text("runtime/nvn/vulkan/precompile.c"), "precompile list")

    def test_native_file_info_covers_a_sparse_file_above_2_gib(self):
        test = text("tests/native/test_utf8_paths.c")
        self.assertHas("FSCTL_SET_SPARSE", test, "Windows sparse file")
        self.assertHas("3ull << 30", test, "large logical size")
        self.assertHas("xcde_utf8_file_info(sparse", test, "64-bit metadata assertion")

    def test_save_tree_operations_use_the_utf8_wrappers(self):
        # LARECOMP_SAVE_DIR is UTF-8 too: mkdir/rmdir/rename/remove in the save/journal tree must not go through the ANSI CRT.
        fs = code_only(text("runtime/services/fs.c"))
        for call in ("mkdir", "rmdir", "unlink", "rename", "remove"):
            self.assertIsNone(re.search(r"(?<![\w.])" + call + r"\(", fs), f"services/fs.c: {call}")
        self.assertHas("xcde_utf8_mkdir(", fs, "save tree mkdir")
        self.assertHas("xcde_utf8_rmdir(", fs, "save tree rmdir")
        self.assertHas("xcde_utf8_remove(", fs, "save tree unlink")
        self.assertHas("xcde_utf8_rename(", fs, "save tree rename")

    def test_diagnostic_file_paths_are_utf8_on_windows(self):
        for rel in ("runtime/diag/sampler.c", "runtime/diag/diag.c", "runtime/nvn/vulkan/precompile.c"):
            code = code_only(text(rel))
            self.assertHas("xcde_utf8_getenv(", code, rel)
            self.assertHas("xcde_utf8_fopen(", code, rel)
        self.assertHas("xcde_utf8_fopen(", code_only(text("runtime/hle/party.c")), "XCDE_TOUR")
        self.assertHas("xcde_utf8_fopen(", code_only(text("runtime/platform/windows.c")), "XCDE_DUMP_HEAP")

    def test_native_test_covers_the_relative_listing_in_a_non_ascii_folder(self):
        test = text("tests/native/test_utf8_paths.c")
        self.assertHas("xcde_utf8_chdir(dir)", test, "chdir into the João folder")
        self.assertHas("xcde_opendir(\".\")", test, "relative listing")
        self.assertHas("crt_listed", test, "the CRT listing reported for the record")


if __name__ == "__main__":
    unittest.main()
