"""RecompLauncher support (T279): AppRun's --game / --prepare modes, run against stand-in programs, the builder's
progress lines, and source contracts for the Big Picture fullscreen and the launcher's update check (no game data)."""
import contextlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'gamebuild'))
# The lifter needs unicorn and these tests never lift (as in test_gamebuild_progress).
_stub = 'gen' not in sys.modules
if _stub:
    sys.modules['gen'] = types.ModuleType('gen')
import gamebuild as gb  # noqa: E402
if _stub:
    del sys.modules['gen']

# A stand-in that prints its own name, its arguments and the folder it runs in, one per line.
STAND_IN = ('#!/bin/sh\necho "$(basename "$0")"\nfor a in "$@"; do echo "arg $a"; done\n'
            'echo "game ${MONADO_GAME_EXE:-}"\necho "cwd $(pwd -P)"\n')


@unittest.skipUnless(os.name == 'posix' and shutil.which('sh'), 'AppRun is a POSIX shell script')
class AppRunModes(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.appdir = base / 'install'
        for rel in ('usr/bin/monado', 'usr/bin/monado-launcher', 'usr/share/monado/gamebuild/monado-gamebuild'):
            program = self.appdir / rel
            program.parent.mkdir(parents=True, exist_ok=True)
            program.write_text(STAND_IN, encoding='utf-8')
            program.chmod(0o755)
        shutil.copy2(ROOT / 'packaging/linux/AppRun', self.appdir / 'AppRun')
        self.dump = base / 'dump'
        (self.dump / 'exefs').mkdir(parents=True)
        (self.dump / 'exefs/main').write_bytes(b'')
        (self.dump / 'romfs').mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def run_apprun(self, *args):
        env = {'PATH': os.environ.get('PATH', '/usr/bin:/bin'), 'HOME': self.tmp.name,
               'MONADO_DATA_ROOT': str(self.appdir)}
        done = subprocess.run(['sh', str(self.appdir / 'AppRun'), *args], env=env, text=True, capture_output=True)
        return done.returncode, done.stdout.splitlines()

    def test_game_plays_the_dump_directly(self):
        code, lines = self.run_apprun('--game', str(self.dump))
        self.assertEqual(code, 0)
        self.assertEqual(lines[:3], ['monado', 'arg --game', f'arg {self.dump.resolve()}'])

    def test_game_runs_in_the_data_folder(self):
        _, lines = self.run_apprun('--game', str(self.dump))
        self.assertEqual(lines[-1], f'cwd {self.appdir.resolve()}')

    def test_game_leaves_no_link_to_the_dump_in_the_install_folder(self):
        self.run_apprun('--game', str(self.dump))
        self.assertFalse(os.path.lexists(self.appdir / 'targets'))

    def test_game_without_a_dump_stops_with_code_2(self):
        code, _ = self.run_apprun('--game')
        self.assertEqual(code, 2)

    def test_prepare_runs_the_builder_with_its_arguments(self):
        code, lines = self.run_apprun('--prepare', '--root', str(self.dump), '--out', 'x')
        self.assertEqual(code, 0)
        self.assertEqual(lines[:5], ['monado-gamebuild', 'arg --root', f'arg {self.dump}', 'arg --out', 'arg x'])

    def test_prepare_names_the_game_for_the_shader_precompile(self):
        _, lines = self.run_apprun('--prepare', '--precompile')
        self.assertIn(f'game {self.appdir}/usr/bin/monado', lines)

    def test_play_still_links_the_dump(self):
        self.run_apprun('--play', str(self.dump))
        self.assertEqual((self.appdir / 'targets').resolve(), self.dump.resolve())


class BuilderProgressLines(unittest.TestCase):
    def report(self, env, *args):
        printed = io.StringIO()
        with tempfile.TemporaryDirectory() as out, mock.patch.dict(os.environ, env, clear=False), \
                contextlib.redirect_stdout(printed):
            gb.report(Path(out), *args)
        return printed.getvalue().splitlines()

    def test_recomplauncher_gets_a_progress_line(self):
        gb.set_shares(8)
        lines = self.report({'RECOMPLAUNCHER': '1'}, 'compile', 3, 10, 'Compiling main/x.c', 0.5)
        expected = f'progress {gb.overall("compile", 0.5):.4f} Compiling the game code: 3 of 10 files'
        self.assertEqual(lines, [expected])

    def test_own_launcher_runs_stay_quiet(self):
        lines = self.report({'RECOMPLAUNCHER': ''}, 'compile', 3, 10, 'Compiling main/x.c', 0.5)
        self.assertEqual(lines, [])

    def test_link_step_shows_its_own_text(self):
        self.assertEqual(gb.launcher_step_text('link', 1, 1, 'Already built'), 'Already built')

    def test_prepare_switch_is_accepted(self):
        with tempfile.TemporaryDirectory() as d:
            log = Path(d) / 'gamebuild.log'
            log.write_text('[00:00:01 +1s] compile main/a.c: exit 0 after 2.5s\n', encoding='utf-8')
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(gb.main(['--prepare', '--write-costs', str(log), str(Path(d) / 'cost.json')]), 0)


# A stand-in game for `--precompile <dump>`: writes the progress file twice and the done stamp, as precompile.c does.
# T281 format: <phase> <overall done> <overall total> <this pass done> <this pass total>.
GAME_PRECOMPILES = ('#!/bin/sh\n[ "$1" = --precompile-status ] && exit 1\n'
                    '[ "$1" = --precompile ] || exit 9\n'
                    'echo "scan 300 1000 5 10" > cache/precompile_progress.txt\nsleep 1\n'
                    'echo "pipelines 900 1000 3 3" > cache/precompile_progress.txt\nsleep 1\n'
                    'echo "programs 3 built 3" > cache/precompile_done.txt\n')
GAME_FAILS = '#!/bin/sh\nexit 3\n'
GAME_PRECOMPILE_FAILS = '#!/bin/sh\n[ "$1" = --precompile-status ] && exit 1\nexit 3\n'


class ShaderPrecompile(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        gb._launcher_build_share[0] = gb.PRECOMPILE_BUILD_SHARE

    def tearDown(self):
        gb._launcher_build_share[0] = 1.0
        self.tmp.cleanup()

    def game(self, script):
        exe = self.root / 'monado'
        exe.write_text(script, encoding='utf-8')
        exe.chmod(0o755)
        return exe

    def status_current(self):
        """[XCDE] T282: a stand-in whose `--precompile-status <dump>` says the cache is current (exit 0), as the runtime
        does when its signature matches cache/precompile_done.txt; anything else fails the check."""
        return ('#!/bin/sh\n[ "$1" = --precompile-status ] && [ "$2" = "' + str(self.root / 'dump') + '" ] && exit 0\n'
                'exit 1\n')

    def run_precompile_output(self, exe):
        env = {'RECOMPLAUNCHER': '1', 'MONADO_DATA_ROOT': str(self.root), 'MONADO_GAME_EXE': str(exe or '')}
        printed = io.StringIO()
        with mock.patch.dict(os.environ, env, clear=False), contextlib.redirect_stdout(printed):
            gb.precompile_shaders(self.root / 'dump')
        return printed.getvalue()

    def run_precompile(self, exe):
        return [line for line in self.run_precompile_output(exe).splitlines() if line.startswith('progress ')]

    def test_scan_line_is_the_first_part_of_the_shaders(self):
        share, text = gb.precompile_step('scan 300 1000 5 10')
        self.assertAlmostEqual(share, 0.3)
        self.assertEqual(text, 'Preparing the shaders: scanning the game files, 5 of 10')

    def test_pipelines_pass_counts_towards_the_same_bar(self):
        share, text = gb.precompile_step('pipelines 900 1000 3 3')
        self.assertAlmostEqual(share, 0.9)
        self.assertEqual(text, 'Preparing the shaders: whole optimized pipelines, 3 of 3')

    def test_seed_pass_has_its_own_count(self):
        share, text = gb.precompile_step('seed 600 1000 400 900')
        self.assertAlmostEqual(share, 0.6)
        self.assertEqual(text, 'Preparing the shaders: known variants (seed), 400 of 900')

    def test_the_closing_done_line_never_moves_the_bar_back(self):
        self.assertIsNone(gb.precompile_step('done 1000 1000 0 0'))

    def test_unreadable_progress_is_skipped(self):
        self.assertIsNone(gb.precompile_step('scan 1 0 0 0'))
        self.assertIsNone(gb.precompile_step('half a line'))

    @unittest.skipUnless(os.name == 'posix', 'the stand-in game is a shell script')
    def test_precompile_status_decides_whether_a_run_happens(self):
        # T282: the builder asks the runtime (`--precompile-status <dump>`), not the old `exe <size> <mtime>` stamp.
        self.assertTrue(gb.precompile_is_current(self.root, self.game(self.status_current()), self.root / 'dump'))
        self.assertFalse(gb.precompile_is_current(self.root, self.game(GAME_FAILS), self.root / 'dump'))

    @unittest.skipUnless(os.name == 'posix', 'the stand-in game is a shell script')
    def test_a_current_precompile_is_not_started(self):
        lines = self.run_precompile(self.game(self.status_current()))
        self.assertEqual(lines, ['progress 1.0000 Shaders already prepared'])
        self.assertFalse((self.root / 'cache/precompile_progress.txt').exists())

    def test_a_missing_game_executable_is_not_current(self):
        self.assertFalse(gb.precompile_is_current(self.root, self.root / 'no-such-game', self.root / 'dump'))

    @unittest.skipUnless(os.name == 'posix', 'the stand-in game is a shell script')
    def test_the_runtime_exit_codes_are_preserved(self):
        # [XCDE] T282 review: 0 current, 1 stale, 2 unreadable; anything else / a timeout / an OSError is "cannot ask".
        def status_with(code):
            return self.game('#!/bin/sh\n[ "$1" = --precompile-status ] && exit ' + str(code) + '\nexit 9\n')
        self.assertEqual(gb.precompile_status(self.root, status_with(0), self.root / 'dump'), gb.PC_STATUS_CURRENT)
        self.assertEqual(gb.precompile_status(self.root, status_with(1), self.root / 'dump'), gb.PC_STATUS_STALE)
        self.assertEqual(gb.precompile_status(self.root, status_with(2), self.root / 'dump'), gb.PC_STATUS_UNREADABLE)
        self.assertEqual(gb.precompile_status(self.root, status_with(7), self.root / 'dump'), -1)
        self.assertEqual(gb.precompile_status(self.root, self.root / 'no-such-game', self.root / 'dump'), -1)

    def test_a_hung_status_probe_is_bounded(self):
        # [XCDE] T282 review: subprocess.run has a timeout; a hung child answers "cannot ask", it must not hang setup.
        with mock.patch('subprocess.run', side_effect=subprocess.TimeoutExpired(['monado'], gb.PC_STATUS_TIMEOUT_S)):
            self.assertEqual(gb.precompile_status(self.root, self.root / 'monado', self.root / 'dump'), -1)

    @unittest.skipUnless(os.name == 'posix', 'the stand-in game is a shell script')
    def test_unreadable_game_files_do_not_start_a_doomed_run(self):
        # [XCDE] T282 review: exit 2 means the game files cannot be read; starting --precompile would exit with the same
        # code and report only a generic failure.
        exe = self.game('#!/bin/sh\n[ "$1" = --precompile-status ] && exit 2\nexit 3\n')
        lines = self.run_precompile(exe)
        self.assertEqual(lines, ['progress 1.0000 Shaders compile while you play'])
        self.assertFalse((self.root / 'cache/precompile_progress.txt').exists())

    def test_an_unverifiable_status_skips_the_full_precompile(self):
        # A timeout, spawn failure or unexpected status exit is not a stale cache: do not launch an unbounded full pass.
        exe = self.game('')
        with mock.patch.object(gb, 'precompile_status', return_value=-1), \
                mock.patch.object(gb.subprocess, 'Popen') as popen:
            output = self.run_precompile_output(exe)
        popen.assert_not_called()
        self.assertIn('could not verify the shader cache', output)
        self.assertIn('the shader precompile was skipped', output)
        self.assertIn('progress 1.0000 Shaders compile while you play', output)
        self.assertFalse((self.root / 'cache/precompile_progress.txt').exists())

    def test_a_full_precompile_spawn_error_is_non_fatal(self):
        exe = self.game('')
        with mock.patch.object(gb, 'precompile_status', return_value=gb.PC_STATUS_STALE), \
                mock.patch.object(gb.subprocess, 'Popen', side_effect=OSError('blocked by security policy')):
            output = self.run_precompile_output(exe)
        self.assertIn('could not start the shader precompile', output)
        self.assertIn('blocked by security policy', output)
        self.assertIn('the game compiles shaders as it reaches them', output)
        self.assertIn('progress 1.0000 Shaders compile while you play', output)

    def test_frozen_builders_library_path_is_given_back(self):
        env = {'LD_LIBRARY_PATH': '/frozen/_internal', 'LD_LIBRARY_PATH_ORIG': '/app/usr/lib'}
        with mock.patch.dict(os.environ, env, clear=False):
            self.assertEqual(gb.child_environment()['LD_LIBRARY_PATH'], '/app/usr/lib')

    def test_no_game_skips_without_failing(self):
        lines = self.run_precompile(None)
        self.assertEqual(lines, ['progress 1.0000 Shaders compile while you play'])

    @unittest.skipUnless(os.name == 'posix', 'the stand-in game is a shell script')
    def test_precompile_reports_its_progress_after_the_build(self):
        lines = self.run_precompile(self.game(GAME_PRECOMPILES))
        values = [float(line.split()[1]) for line in lines]
        self.assertEqual(values, sorted(values))
        self.assertEqual(values[0], gb.PRECOMPILE_BUILD_SHARE)
        self.assertEqual(lines[-1], 'progress 1.0000 Shaders ready')
        self.assertIn('whole optimized pipelines', ' '.join(lines))

    @unittest.skipUnless(os.name == 'posix', 'the stand-in game is a shell script')
    def test_a_failed_precompile_still_finishes_the_setup(self):
        lines = self.run_precompile(self.game(GAME_PRECOMPILE_FAILS))
        self.assertEqual(lines[-1], 'progress 1.0000 Shaders compile while you play')

    def test_precompile_switch_is_accepted(self):
        with tempfile.TemporaryDirectory() as d:
            log = Path(d) / 'gamebuild.log'
            log.write_text('[00:00:01 +1s] compile main/a.c: exit 0 after 2.5s\n', encoding='utf-8')
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(gb.main(['--precompile', '--write-costs', str(log), str(Path(d) / 'cost.json')]), 0)


class SourceContracts(unittest.TestCase):
    def test_big_picture_opens_fullscreen(self):
        device = (ROOT / 'runtime/nvn/vulkan/device.c').read_text(encoding='utf-8')
        self.assertIn('getenv("RECOMPLAUNCHER_MODE")', device)
        self.assertIn('bool forced_fs = gamescope_fs || bigpicture_fs;', device)

    def test_big_picture_keeps_the_saved_setting(self):
        device = (ROOT / 'runtime/nvn/vulkan/device.c').read_text(encoding='utf-8')
        self.assertNotIn('g_settings.fullscreen = true', device)

    def test_no_update_banner_under_recomplauncher(self):
        launcher = (ROOT / 'tools/launcher/launcher.cpp').read_text(encoding='utf-8')
        self.assertIn('upd_is_release_build() && !from_recomplauncher) upd_start(false);', launcher)

    def test_appimage_carries_the_manifest(self):
        packager = (ROOT / 'tools/package/make_appimage.py').read_text(encoding='utf-8')
        self.assertIn("shutil.copy2(ROOT / 'recomplauncher.json', appdir / 'recomplauncher.json')", packager)

    def test_a_user_stop_does_not_poison_the_shader_skip_list(self):
        # T281: pc_stop is the user's Omitir/Detener, not a driver crash: it must delete the in-flight record that the
        # crash plumbing turns into the permanent skip list, and the explicit "Precompile shaders" button retries it.
        # [XCDE] T282 review: the button passes XCDE_PRECOMPILE_FORCE instead of the launcher deleting the files, and the
        # runtime clears the crash/in-flight lists itself when forced (a failed spawn cannot lose the done stamp).
        launcher = (ROOT / 'tools/launcher/launcher.cpp').read_text(encoding='utf-8')
        stop = launcher[launcher.index('static void pc_stop()'):launcher.index('static void pc_poll()')]
        self.assertIn('precompile_inflight.bin', stop)
        self.assertIn('xcde_utf8_remove', stop)
        start = launcher[launcher.index('static bool pc_start(bool then_play, bool fresh = false)'):
                         launcher.index('static void pc_stop()')]
        self.assertIn('PcForce force(fresh)', start)
        self.assertNotIn('precompile_done.txt', start)
        self.assertIn('pc_start(false, true)', launcher)
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        forced_start = precompile.index('if (forced) {', precompile.index('int vk_precompile_main('))
        forced = precompile[forced_start:precompile.index('inflight_open();', forced_start)]
        self.assertIn('precompile_skip.txt', forced)
        self.assertIn('precompile_inflight.bin', forced)

    def test_unknown_precompile_status_is_distinct_and_backed_off(self):
        launcher = (ROOT / 'tools/launcher/launcher.cpp').read_text(encoding='utf-8')
        state = launcher[launcher.index('enum { PC_STATUS_CURRENT'):
                         launcher.index('struct PcForce {')]
        self.assertIn('PC_STATUS_UNREADABLE, PC_STATUS_UNKNOWN', state)
        self.assertIn('PC_UNKNOWN_RETRY_MS = 5000', state)
        self.assertIn('g_pc_current = status;', state)
        self.assertNotIn('status == PC_STATUS_CURRENT ? 1', state)  # (UNKNOWN must never become CURRENT)
        self.assertIn('force && g_pc_current != PC_STATUS_CURRENT', state)
        self.assertIn('now < g_pc_retry_at', state)
        self.assertIn('g_pc_retry_at = status == PC_STATUS_UNKNOWN', state)
        self.assertIn('return pc_status(force) == PC_STATUS_CURRENT;', state)
        probe = launcher[launcher.index('static int pc_status_now() {'):launcher.index('static bool pc_running();')]
        self.assertIn('code == 2 ? PC_STATUS_UNREADABLE : PC_STATUS_UNKNOWN', probe)
        self.assertIn('if (!exists(GAME_EXE)) return PC_STATUS_UNKNOWN;', probe)

    def test_home_and_footer_report_unverified_shaders_honestly(self):
        launcher = (ROOT / 'tools/launcher/launcher.cpp').read_text(encoding='utf-8')
        home = launcher[launcher.index('static void tab_home() {'):launcher.index('static void make_report();')]
        self.assertIn('int pc = pc_status();', home)
        self.assertIn('pc == PC_STATUS_UNKNOWN', home)
        self.assertIn('pc == PC_STATUS_UNREADABLE', home)
        self.assertIn('No se pudo verificar la caché de shaders', home)
        self.assertIn('Could not verify the shader cache', launcher)
        self.assertNotIn('SDL_GetTicks()-checked>500', home)  # (the backoff belongs in pc_status, not a second poller)
        self.assertIn('int frame_pc_status = pc_status();', launcher)
        self.assertIn('pc_idle_text(frame_pc_status)', launcher)

    def test_play_retries_status_but_only_precompiles_a_verified_stale_cache(self):
        launcher = (ROOT / 'tools/launcher/launcher.cpp').read_text(encoding='utf-8')
        play = launcher[launcher.index('if (ImGui::Button(T("JUGAR")'):
                     launcher.index('ImGui::PopStyleColor(3);', launcher.index('if (ImGui::Button(T("JUGAR")'))]
        self.assertIn('pc_status(true) == PC_STATUS_STALE', play)
        self.assertNotIn('!pc_done()', play)
        poll = launcher[launcher.index('static void pc_poll() {'):launcher.index('// ------------------------------------------------------------------ game code')]
        self.assertIn('status == PC_STATUS_STALE && exists("cache/precompile_inflight.bin")', poll)
        self.assertIn('if (code == 0 && (status == PC_STATUS_UNKNOWN || status == PC_STATUS_UNREADABLE))', poll)

    def test_pc_start_reports_failure_without_overwriting_its_error(self):
        launcher = (ROOT / 'tools/launcher/launcher.cpp').read_text(encoding='utf-8')
        start = launcher[launcher.index('static bool pc_start(bool then_play, bool fresh = false)'):
                         launcher.index('static void pc_stop()')]
        self.assertIn('DWORD spawn_err = 0;', start)
        self.assertIn('int spawn_err = 0;', start)
        self.assertIn('spawn_err = GetLastError(); }', start)  # (inside the PcForce scope, before its destructor)
        self.assertIn('spawn_err = errno; }', start)
        self.assertLess(start.index('ok = CreateProcessW'), start.index('spawn_err = GetLastError();'))
        self.assertLess(start.index('spawn_err = GetLastError();'),
                        start.index('if (h != INVALID_HANDLE_VALUE) CloseHandle(h);'))
        self.assertLess(start.index('pid = spawn_logged'), start.index('spawn_err = errno;'))
        self.assertLess(start.index('spawn_err = errno;'), start.index('if (!pid)'))
        self.assertIn('if (then_play) g_pc_play_now = true;', start)
        self.assertIn('return false;', start)
        self.assertIn('return true;', start)
        self.assertIn('if (pc_start(false, true) && retry)', launcher)
        self.assertIn('if (pc_start(then_play))', launcher)

    def test_the_shader_precompile_bar_covers_the_four_passes(self):
        # T281: the progress file is "<phase> <overall done> <overall total> <this pass done> <this pass total>" over
        # scan/build/seed/pipelines, weighted by the previous run's measured pass times; the launcher shows the pass,
        # its count and the time left, and the builder reads the same fraction.
        launcher = (ROOT / 'tools/launcher/launcher.cpp').read_text(encoding='utf-8')
        self.assertIn('Fase %d de 4', launcher)
        self.assertIn('quedan ~%d min', launcher)
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        for phase in ('"scan"', '"build"', '"seed"', '"pipelines"'):
            self.assertIn(phase, precompile)
        self.assertIn('precompile_times.txt', precompile)
        builder = (ROOT / 'tools/gamebuild/gamebuild.py').read_text(encoding='utf-8')
        self.assertIn('overall_done, overall_total, pass_done, pass_total', builder)


if __name__ == '__main__':
    unittest.main()
