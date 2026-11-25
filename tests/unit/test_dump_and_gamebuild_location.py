"""GitHub #259: dump path persisted in xcde.ini (for direct/Steam Deck Game Mode launches) and
opt-in gamebuild folder inside the dump folder or next to a .nup/.nsp package."""
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
APPRUN = ROOT / "packaging/linux/AppRun"
LAUNCHER = (ROOT / "tools/launcher/launcher.cpp").read_text(encoding="utf-8")
SETTINGS_INI = (ROOT / "runtime/host/settings_ini.c").read_text(encoding="utf-8")
MAIN_BOOT = (ROOT / "runtime/host/main_boot.c").read_text(encoding="utf-8")
GAMELIB = (ROOT / "runtime/gamelib.c").read_text(encoding="utf-8")


@unittest.skipUnless(os.name == "posix" and shutil.which("sh"), "AppRun is a POSIX shell script")
class AppRunIniDump(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="monado-ini-dump-"))
        self.appdir = self.tmp / "AppDir"
        (self.appdir / "usr/bin").mkdir(parents=True)
        (self.appdir / "usr/share/monado").mkdir(parents=True)
        shutil.copy2(APPRUN, self.appdir / "AppRun")
        self.monado = self.appdir / "usr/bin/monado"
        self.monado.write_text("#!/bin/sh\nprintf 'MONADO:%s\\n' \"$*\"\n", encoding="utf-8")
        self.monado.chmod(self.monado.stat().st_mode | stat.S_IXUSR)
        self.data = self.tmp / "data"
        self.data.mkdir()

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def _run_apprun(self, *args):
        env = os.environ.copy()
        env["MONADO_DATA_ROOT"] = str(self.data)
        env.pop("MONADO_GAME_ROOT", None)
        env.pop("APPIMAGE", None)
        return subprocess.run(["sh", str(self.appdir / "AppRun"), *args], env=env, text=True, capture_output=True)

    def test_play_reads_game_dump_folder_from_xcde_ini_without_targets_symlink(self):
        dump = self.tmp / "sd_card" / "XCDE Dump"
        (dump / "exefs").mkdir(parents=True)
        (dump / "exefs/main").write_bytes(b"NSO0")
        (dump / "romfs").mkdir(parents=True)
        (self.data / "xcde.ini").write_text(f"fullscreen = 1\ngame_dump = {dump}\n", encoding="utf-8")

        done = self._run_apprun("--play")
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(done.stdout.strip(), f"MONADO:{dump}")
        self.assertFalse((self.data / "targets").exists())

    def test_play_reads_game_dump_nup_from_xcde_ini(self):
        nup = self.tmp / "sd_card" / "xcde.nup"
        nup.parent.mkdir(parents=True)
        nup.write_bytes(b"NUP\x01" + b"\x00" * 16)
        (self.data / "xcde.ini").write_text(f"game_dump = {nup}\n", encoding="utf-8")

        done = self._run_apprun("--play")
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(done.stdout.strip(), f"MONADO:{nup}")

    def test_play_falls_back_to_targets_when_no_game_dump_in_ini(self):
        dump = self.tmp / "linked_dump"
        (dump / "exefs").mkdir(parents=True)
        (dump / "exefs/main").write_bytes(b"NSO0")
        (dump / "romfs").mkdir(parents=True)
        os.symlink(dump, self.data / "targets")

        done = self._run_apprun("--play")
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(done.stdout.strip(), "MONADO:targets")


class DumpAndGamebuildContracts(unittest.TestCase):
    def test_settings_ini_roundtrips_game_dump_and_gamebuild_in_dump(self):
        self.assertIn('!strcmp(k, "game_dump")', SETTINGS_INI)
        self.assertIn('!strcmp(k, "gamebuild_in_dump")', SETTINGS_INI)
        self.assertIn('fprintf(f, "game_dump = %s\\n", g_game_dump);', SETTINGS_INI)
        self.assertIn('fprintf(f, "gamebuild_in_dump = %d\\n"', SETTINGS_INI)
        self.assertIn("bool settings_read_game_dump(", SETTINGS_INI)

    def test_main_boot_falls_back_to_saved_game_dump(self):
        self.assertIn("settings_read_game_dump(ini_dump, sizeof ini_dump)", MAIN_BOOT)

    def test_gamelib_checks_dump_gamebuild_and_package_sibling_gamebuild(self):
        self.assertIn('snprintf(out[n++], 1100, "%s/gamebuild/%s", game_dir, GAMELIB_NAME);', GAMELIB)
        self.assertIn('snprintf(out[n++], 1100, "%s/gamebuild/%s", parent, GAMELIB_NAME);', GAMELIB)

    def test_launcher_persists_dump_and_offers_gamebuild_in_dump_tick(self):
        self.assertIn('!strcmp(k, "game_dump")', LAUNCHER)
        self.assertIn('!strcmp(k, "gamebuild_in_dump")', LAUNCHER)
        self.assertIn("static std::string gc_out_dir()", LAUNCHER)
        self.assertIn("&g.gamebuild_in_dump", LAUNCHER)


if __name__ == "__main__":
    unittest.main()
