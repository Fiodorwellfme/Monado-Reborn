"""Incremental shader precompile (T282): the build fingerprint and the contracts that keep the precompiled shader cache
across rebuilds that cannot change the shaders. No game data is needed.

Before T282 cache/precompile_done.txt was stamped with the game executable's size and time, so every build precompiled
all shaders again. Now the stamp carries a signature of the inputs (translator/SPIR-V compiler sources, the SPIR-V
options and the game files); the runtime answers `--precompile-status`, and the launcher and RecompLauncher's builder
ask it instead of comparing the executable.
"""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'build'))
import shader_sig  # noqa: E402


class ShaderSig(unittest.TestCase):
    def test_fingerprint_is_16_lowercase_hex(self):
        value = shader_sig.fingerprint()
        self.assertEqual(len(value), 16)
        self.assertTrue(all(c in '0123456789abcdef' for c in value))

    def test_covers_the_translator_compiler_and_precompile_sources(self):
        names = shader_sig.sources()
        for name in ('runtime/nvn/maxwell/translate.c', 'runtime/nvn/vulkan/shaders.c',
                     'runtime/nvn/vulkan/precompile.c', 'runtime/nvn/objects.c',
                     'runtime/cache_validation.h', 'runtime/services/game_files.c', 'runtime/services/game_files.h',
                     'runtime/host/utf8_path.h', 'runtime/platform/dir_utf8.h',
                     'runtime/nvn/vulkan/bloom_prefilter.h', 'runtime/nvn/vulkan/bloom.h',
                     'runtime/nvn/vulkan/gtao.h', 'tools/build/shader_sig.py'):
            self.assertIn(name, names)

    def test_the_script_prints_the_same_fingerprint(self):
        done = subprocess.run([sys.executable, str(ROOT / 'tools' / 'build' / 'shader_sig.py')], cwd=ROOT,
                              capture_output=True, text=True, check=True)
        self.assertEqual(done.stdout.strip(), shader_sig.fingerprint())

    def test_the_header_defines_the_macro_for_the_build(self):
        header = shader_sig.header()
        self.assertIn('do not edit', header)
        self.assertIn(f'#define XCDE_SHADER_SIG "{shader_sig.fingerprint()}"', header)

    def test_sources_change_the_fingerprint(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            inputs = {
                'runtime/nvn/maxwell/translate.c': 'translator',
                'runtime/nvn/vulkan/shaders.c': 'compiler',
                'runtime/nvn/vulkan/shaders.h': 'header',
                'runtime/nvn/vulkan/vk.h': 'vulkan',
                'runtime/nvn/vulkan/precompile.c': 'guesses',
                'runtime/nvn/objects.c': 'content hash',
                'runtime/cache_validation.h': 'acceptance',
                'runtime/services/game_files.c': 'game files',
                'runtime/services/game_files.h': 'game files header',
                'runtime/host/utf8_path.h': '64-bit UTF-8 metadata',
                'runtime/platform/dir_utf8.h': 'wide directory listing',
                'runtime/nvn/vulkan/bloom_prefilter.h': 'bloom prefilter GLSL',
                'runtime/nvn/vulkan/bloom.h': 'bloom GLSL',
                'runtime/nvn/vulkan/gtao.h': 'GTAO GLSL',
                'tools/build/shader_sig.py': 'fingerprint script',
            }
            for rel, text in inputs.items():
                path = root / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding='utf-8')
            first = shader_sig.fingerprint(root)
            self.assertEqual(first, shader_sig.fingerprint(root))
            for rel, text in inputs.items():
                with self.subTest(source=rel):
                    path = root / rel
                    path.write_text(text + ', fixed', encoding='utf-8')
                    self.assertNotEqual(first, shader_sig.fingerprint(root))
                    path.write_text(text, encoding='utf-8')
                    self.assertEqual(first, shader_sig.fingerprint(root))
            added = root / 'runtime/nvn/maxwell' / 'new_helper.h'
            added.write_text('helper', encoding='utf-8')
            changed = shader_sig.fingerprint(root)
            added.write_text('helper, more', encoding='utf-8')
            self.assertNotEqual(changed, shader_sig.fingerprint(root))
            # [XCDE] T282 review: the translator folder is covered recursively (a new subfolder is an input too).
            nested = root / 'runtime/nvn/maxwell' / 'rewrite' / 'pass.c'
            nested.parent.mkdir(parents=True)
            nested.write_text('pass', encoding='utf-8')
            changed = shader_sig.fingerprint(root)
            nested.write_text('pass, fixed', encoding='utf-8')
            self.assertNotEqual(changed, shader_sig.fingerprint(root))


class RuntimeContracts(unittest.TestCase):
    def test_the_done_stamp_records_the_inputs_not_the_executable(self):
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        self.assertIn('fprintf(f, "sig %s%c", sig, 10)', precompile)
        self.assertNotIn('stamp_exe', precompile)
        self.assertNotIn('exe %lld %lld', precompile)

    def test_the_status_answer_does_not_scan_or_open_vulkan(self):
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        start = precompile.index('int vk_precompile_status(const char *root) {')
        block = precompile[start:precompile.index('static int start_threads(', start)]
        self.assertIn('vk_precompile_sig(root, sig)', block)
        for expensive in ('ui_window_init', 'gf_init', 'list_dir', 'scan_file', 'pthread_create'):
            self.assertNotIn(expensive, block)

    def test_a_current_cache_stops_before_any_work(self):
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        start = precompile.index('int vk_precompile_main(const char *root) {')
        body = precompile[start:precompile.index('gf_init(root', start)]  # (the check and the force switch)
        self.assertIn('precompile_current(sig)', body)
        self.assertIn('XCDE_PRECOMPILE_FORCE', body)
        # [review] the stamp is removed only once the root was validated: a wrong root must not throw it away.
        remove_done = precompile.index('remove_precompile_file("cache/precompile_done.txt")', start)
        self.assertLess(precompile.index('gf_init(root', start), remove_done)
        self.assertLess(precompile.index('ui_window_init();', start), remove_done)

    def test_the_found_shaders_input_masks_are_initialized(self):
        # found[] comes from realloc: a shader that no container named had a garbage int_in/named, so the build made
        # random "named input" variants of it - a different set every run (19,878 / 7,685 / 6,959 stages compiled in
        # three runs) and a SPIR-V cache that never converged.
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        add_shader = precompile[precompile.index('static uint32_t add_shader('):precompile.index('static void add_prog(')]
        self.assertIn('f->int_in = 0; f->named = false;', add_shader)

    def test_the_build_starts_after_the_scan_so_the_cache_converges(self):
        # The named-input masks (found[].int_in/named) are merged by the scanners while the scan runs: a build that
        # starts during it copies a partial mask, so each run translated a different set of variants.
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        body = precompile[precompile.index('int vk_precompile_main(const char *root) {'):]
        scan_joined = body.index('for (int i = 0; i < nscan_started; i++) pthread_join(sc[i], NULL);')
        worker_started = body.index('start_threads(th, nthreads, worker, "build")')
        self.assertLess(scan_joined, worker_started)
        self.assertNotIn('nworkers0', body)

    def test_cache_sizes_use_the_shared_64_bit_metadata_helper(self):
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        helper = precompile[precompile.index('static bool cache_file_size('):precompile.index('static bool cache_file_nonempty(')]
        self.assertIn('XcdeFileInfo fi;', helper)
        self.assertIn('xcde_utf8_file_info(path, &fi)', helper)
        self.assertIn('!fi.isreg', helper)  # a directory or special file named spirv.pack is not a cache pack
        for obsolete in ('CreateFileW', 'GetFileSizeEx', 'GetFileInformationByHandle', 'FILE_ATTRIBUTE_DIRECTORY',
                         'struct stat', 'S_ISREG'):
            self.assertNotIn(obsolete, helper)
        nonempty = precompile[precompile.index('static bool cache_file_nonempty('):precompile.index('static bool cache_has_loose_spv(')]
        self.assertIn('size > 0', nonempty)
        current = precompile[precompile.index('static bool precompile_current('):precompile.index('static bool remove_precompile_file(')]
        self.assertNotIn('stat("cache/spirv', current)

    def test_forced_cleanup_is_checked_and_ordered_after_initialization(self):
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        start = precompile.index('int vk_precompile_main(')
        root_ready = precompile.index('gf_init(root', start)
        vulkan_ready = precompile.index('ui_window_init();', start)
        done = precompile.index('remove_precompile_file("cache/precompile_done.txt")', start)
        forced = precompile.index('if (forced) {', done)
        inflight = precompile.index('remove_precompile_file("cache/precompile_inflight.bin")', forced)
        skip = precompile.index('remove_precompile_file("cache/precompile_skip.txt")', forced)
        opened = precompile.index('inflight_open();', forced)
        self.assertLess(root_ready, vulkan_ready)
        self.assertLess(vulkan_ready, done)  # a failed device init keeps the valid stamp too
        self.assertLess(done, forced)
        self.assertLess(inflight, skip)  # a failed in-flight delete must not discard the skip list first
        self.assertLess(skip, opened)
        remover = precompile[precompile.index('static bool remove_precompile_file('):precompile.index('static bool write_sig_stamp(')]
        self.assertIn('xcde_utf8_remove(path)', remover)
        self.assertIn('errno == ENOENT', remover)
        self.assertIn('return false', remover)

    def test_scan_budgets_bound_sources_and_decompressed_blocks_separately(self):
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        budget = precompile[precompile.index('static void scan_budget_enter('):precompile.index('static void scan_failure(')]
        self.assertIn('int inflight, slot_inflight, slots;', precompile)
        self.assertIn('(slot_limited && b->slot_inflight >= b->slots)', budget)
        self.assertNotIn('(slot_limited && b->inflight >= b->slots)', budget)
        self.assertIn('if (slot_limited) b->slot_inflight++;', budget)
        self.assertIn('if (slot_limited) b->slot_inflight--;', budget)
        self.assertIn('b->inflight > 0', budget)  # always admit one oversized item, so waiting cannot deadlock
        self.assertIn('b->bytes > b->limit - size', budget)
        scan = precompile[precompile.index('static void scan_file('):precompile.index('// files that hold shaders')]
        source_enter = scan.index('scan_budget_enter(&source_budget, src.size, src.size > (64ull << 20))')
        self.assertLess(source_enter, scan.index('gf_pread(&src, d, src.size, 0)'))
        self.assertEqual(scan.count('scan_budget_leave(&source_budget, src.size, src.size > (64ull << 20))'), 3)
        self.assertIn('scan_budget_leave(&xbc1_budget, dsz, true)', scan)
        self.assertLess(scan.index('scan_budget_enter(&xbc1_budget, dsz, true)'), scan.index('malloc(dsz)'))
        main = precompile[precompile.index('int vk_precompile_main('):]
        self.assertIn('scanner_cap_for_ram(ram)', main)
        self.assertNotIn('nscanners > source_budget.slots', main)  # small files retain their I/O overlap on 8 GB

    def test_incidental_xbc1_magic_is_not_a_hard_failure(self):
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        header = precompile[precompile.index('static bool xbc1_header('):precompile.index('static void scan_file(')]
        self.assertIn('rd32(d + i + 4) != 1', header)
        self.assertIn('c < 0x20 || c == 0x7f', header)
        scan = precompile[precompile.index('static void scan_file('):precompile.index('// files that hold shaders')]
        self.assertIn('!xbc1_header(d, i)', scan)
        self.assertIn('scan_failure(path, why)', scan)  # recognized blocks with bad bounds/zlib still fail the run

    def test_listing_failures_are_scan_failures_and_use_64_bit_metadata(self):
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        listing = precompile[precompile.index('static void list_package('):precompile.index('static int path_rank(')]
        self.assertIn('scan_failure(dir, "package directory is missing or unreadable")', listing)
        self.assertIn('if (!gf_source(p, &src)) scan_failure(p, "source metadata is missing or unreadable")', listing)
        self.assertIn('if (!dd)', listing)
        self.assertIn('scan_failure(dir, why)', listing)  # opendir/enumeration failure
        self.assertIn('if (!xcde_utf8_file_info(p, &fi))', listing)
        self.assertIn('scan_failure(p, "metadata could not be read")', listing)  # stat/metadata failure
        self.assertIn('if (fi.isdir) list_dir(p)', listing)
        self.assertIn('else if (fi.isreg && wanted(e->d_name)) add_path(p, fi.size)', listing)
        for obsolete in ('xcde_utf8_stat(', 'struct stat', 'S_ISDIR', 'S_ISREG'):
            self.assertNotIn(obsolete, listing)
        failure = precompile[precompile.index('static void scan_failure('):precompile.index('// The scan visits')]
        self.assertIn('atomic_fetch_add(&n_scan_failed, 1)', failure)
        main = precompile[precompile.index('int vk_precompile_main('):]
        self.assertIn('if (scan_failed || n_failed)', main)  # a partial listing cannot leave a current stamp

    def test_variant_files_must_match_the_exec_record_abi_before_jobs_are_created(self):
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        load = precompile[precompile.index('static void load_seed_file('):precompile.index('static void *var_worker(')]
        accessor = load.index('vk_precompile_variant_size()')
        checked = load.index('if (hdr[1] != rec_size)')
        allocated = load.index('malloc(rec_size)')
        first_job = load.index('map_put(&var_done, h, 1)')
        self.assertLess(accessor, checked)
        self.assertLess(checked, allocated)
        self.assertLess(allocated, first_job)
        self.assertIn('skipping incompatible variant file %s: record size %u, expected %zu', load)
        self.assertIn('fclose(f);', load[checked:allocated])
        self.assertIn('fread(rec, rec_size, 1, f)', load)
        self.assertNotIn('hdr[1] < 32 + 2 * sizeof(ShaderKey)', load)
        seed = precompile[precompile.index('static void load_seed(void)'):
                          precompile.index('static void load_seed_file(const char *path, Map *done_)')]
        self.assertIn('data/variants_seed.bin', seed)
        self.assertIn('cache/variants.bin', seed)
        exec_code = (ROOT / 'runtime/nvn/vulkan/exec.c').read_text(encoding='utf-8')
        variant = exec_code[exec_code.index('typedef struct {\n    uint64_t code_hash[3];'):
                            exec_code.index('static Variant *var_db;')]
        self.assertIn('size_t vk_precompile_variant_size(void) { return sizeof(Variant); }', variant)

    def test_thread_phases_check_creation_and_join_only_started_threads(self):
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        self.assertEqual(precompile.count('pthread_create('), 1)
        helper = precompile[precompile.index('static int start_threads('):precompile.index('int vk_precompile_main(')]
        self.assertIn('int err = pthread_create', helper)
        self.assertIn('if (!started)', helper)
        self.assertIn('_exit(2)', helper)
        main = precompile[precompile.index('int vk_precompile_main('):]
        self.assertNotIn('pthread_create(', main)
        for phase in ('scanner, "scan"', 'worker, "build"', 'seed_worker, "seed"', 'var_worker, "pipelines"'):
            self.assertIn(phase, main)
        for started in ('nscan_started', 'nbuild_started', 'nseed_started', 'npipe_started'):
            self.assertIn(f'i < {started}', main)

    def test_only_scan_and_base_program_failures_block_the_stamp(self):
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        build = precompile[precompile.index('static void build_('):precompile.index('// [XCDE] T211: the shipped variant seed')]
        self.assertEqual(build.count('atomic_fetch_add(&n_built, 1)'), 1)  # base program only, not every variant
        self.assertEqual(build.count('atomic_fetch_add(&n_failed, 1)'), 1)  # base program only
        self.assertIn('atomic_fetch_add(&n_optional_failed, 1)', build)
        self.assertIn('atomic_fetch_add(&n_extra, 1)', build)
        self.assertIn('atomic_fetch_add(&n_named_int, 1)', build)
        seed = precompile[precompile.index('static void *seed_worker('):precompile.index('static bool stalled(')]
        self.assertIn('&n_seed_failed', seed)
        self.assertNotIn('&n_failed', seed)
        pipes = precompile[precompile.index('static void *var_worker('):precompile.index('static void *seed_worker(')]
        self.assertIn('&n_var_failed', pipes)
        self.assertNotIn('&n_failed', pipes)
        main = precompile[precompile.index('int vk_precompile_main('):]
        finish = main.index('pass_save_times();')
        failed = main.index('if (scan_failed || n_failed)', finish)
        stamped = main.index('write_done_stamp(sig, secs)', failed)
        self.assertLess(failed, stamped)
        self.assertIn('incomplete required work', main[failed:stamped])
        self.assertIn('no current cache stamp was written', main[failed:stamped])
        self.assertIn('_exit(1)', main[failed:stamped])

    def test_optional_failures_warn_but_do_not_block_a_current_stamp(self):
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        main = precompile[precompile.index('int vk_precompile_main('):]
        finish = main.index('pass_save_times();')
        failed = main.index('if (scan_failed || n_failed)', finish)
        condition = main[failed:main.index('{', failed)]
        for optional in ('n_optional_failed', 'seed_failed', 'n_var_failed', 'optional_failed'):
            self.assertNotIn(optional, condition)
        warning = main.index('[precompile] warning: %llu optional builds failed')
        self.assertLess(warning, failed)
        self.assertIn('does not by itself invalidate the cache stamp', main[warning:failed])
        self.assertLess(failed, main.index('write_done_stamp(sig, secs)', failed))
        stamp = precompile[precompile.index('static bool write_done_stamp('):precompile.index('// `xcde --precompile-status')]
        for field in ('base_built', 'base_failed', 'optional_built', 'optional_failed'):
            self.assertIn(field, stamp)
        self.assertIn('base programs built', main)  # the summary uses the same required/optional distinction
        self.assertIn('optional variants built', main)

    def test_cache_completion_is_mode_aware(self):
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        current = precompile[precompile.index('static bool precompile_current('):precompile.index('static bool remove_precompile_file(')]
        self.assertIn('spirv_persistence_disabled()', current)
        self.assertIn('spirv_loose_mode()', current)
        self.assertIn('cache/precompile_loose_done.txt', current)
        self.assertIn('cache_has_loose_spv()', current)
        self.assertIn('cache/spirv.pack', current)
        self.assertIn('cache/spirv.idx', current)
        main = precompile[precompile.index('int vk_precompile_main('):]
        self.assertIn('remove_precompile_file("cache/precompile_loose_done.txt")', main)
        disabled = main.rindex('if (spirv_persistence_disabled()) {')
        stamped = main.index('write_done_stamp(sig, secs)', disabled)
        self.assertLess(disabled, stamped)
        self.assertIn('_exit(0)', main[disabled:stamped])
        self.assertIn('write_sig_stamp("cache/precompile_loose_done.txt", sig)', main[stamped:])

    def test_persistence_disabled_status_is_not_applicable(self):
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        start = precompile.index('int vk_precompile_status(const char *root) {')
        block = precompile[start:precompile.index('static int start_threads(', start)]
        self.assertIn('if (spirv_persistence_disabled())', block[:block.index('char sig[17]')])
        self.assertIn('precompilation is not applicable', block)
        self.assertIn('return 3;', block[:block.index('char sig[17]')])

    def test_the_signature_unit_takes_the_build_header_or_a_native_define(self):
        signature = (ROOT / 'runtime/nvn/vulkan/precompile_sig.c').read_text(encoding='utf-8')
        self.assertIn('#include "shader_sig.h"', signature)
        self.assertIn('#ifndef XCDE_SHADER_SIG', signature)
        self.assertIn('.nup', signature)  # (a package is one file: its size and time)
        self.assertIn('cache/variants.bin', signature)  # (documented as deliberately not covered)

    def test_the_signature_allowlist_covers_artifact_inputs(self):
        signature = (ROOT / 'runtime/nvn/vulkan/precompile_sig.c').read_text(encoding='utf-8')
        start = signature.index('static const char *const k_sig_env[] = {')
        allowlist = signature[start:signature.index('NULL};', start)]
        for name in ('LARECOMP_VK_OPT', 'XCDE_FS_OPT', 'XCDE_SPIRV_LOOSE', 'LARECOMP_VK_NO_SPIRV_CACHE',
                     'XCDE_STATIC_STACKS', 'XCDE_RSC_PIXEL', 'XCDE_D16_REF', 'XCDE_SCALED_GATHER',
                     'LARECOMP_NO_DISCARD', 'XCDE_HW_FP16', 'XCDE_EARLY_FS', 'XCDE_STRUCTURED_CFG',
                     'LARECOMP_SAVE_MSL', 'XCDE_BLOOM_PREFILTER', 'XCDE_BLOOM_FILTER', 'XCDE_GTAO',
                     'XCDE_CHAR_LIGHT', 'XCDE_TMAA_TEXEL', 'XCDE_GLSL_DIR', 'LARECOMP_VK_GPL', 'XCDE_VK_GPL',
                     'XCDE_PUSH_DESC', 'XCDE_CANON', 'XCDE_GPL_RETAIN', 'LARECOMP_VK_D24', 'XCDE_PRECOMPILE_LIBS',
                     'XCDE_PRECOMPILE_PIPELINES'):
            self.assertIn(f'"{name}"', allowlist)
        # These are deliberately not cache inputs: runtime-only pipeline selection, run control, or diagnostics.
        for name in ('XCDE_GPL_OPTIMIZE', 'XCDE_GPL_DET', 'XCDE_PREWARM_WHOLE', 'XCDE_PREBUILD_LIBS',
                     'XCDE_PRECOMPILE_FORCE', 'XCDE_PRECOMPILE_THREADS', 'XCDE_TRACE_TRANSLATE'):
            self.assertNotIn(f'"{name}"', allowlist)
        self.assertIn('Runtime-only pipeline selection', signature)  # (the exclusions are documented at the allowlist)
        self.assertIn('mix_glsl_dir(', signature)
        self.assertIn('env_present("XCDE_GLSL_DIR")', signature)  # override bytes, not only the directory path

    def test_the_status_mode_is_wired_into_the_entry_point(self):
        main = (ROOT / 'runtime/host/main_boot.c').read_text(encoding='utf-8')
        self.assertIn('"--precompile-status"', main)
        self.assertIn('vk_precompile_status', main)

    def test_review_fixes_are_in_the_runtime(self):
        # [XCDE] T282 review contracts.
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        self.assertIn('nscanners > 64', precompile)  # (sc[] is 64: >128-CPU machines must not overflow it)
        self.assertIn('cache/spirv.idx', precompile)  # (a current cache needs the pack's index too)
        forced = precompile[precompile.index('if (forced) {', precompile.index('int vk_precompile_main(')):
                          precompile.index('inflight_open();', precompile.index('int vk_precompile_main('))]
        self.assertIn('cache/precompile_skip.txt', forced)
        self.assertIn('cache/precompile_inflight.bin', forced)
        signature = (ROOT / 'runtime/nvn/vulkan/precompile_sig.c').read_text(encoding='utf-8')
        for name in ('XCDE_RSC_PIXEL', 'XCDE_HW_FP16', 'XCDE_PRECOMPILE_PIPELINES', 'XCDE_PRECOMPILE_LIBS'):
            self.assertIn(name, signature)
        self.assertIn('env_present(', signature)  # (a set-but-empty option is not an unset one)
        self.assertIn('is_package_file(root)', signature)  # (a plain file is not a game root)
        self.assertIn('XCDE_PRECOMPILE_ARTIFACTS_REV', signature)
        self.assertTrue((ROOT / 'runtime/nvn/vulkan/precompile_rev.h').is_file())


class BuilderAndLauncherContracts(unittest.TestCase):
    def test_the_launcher_asks_the_runtime(self):
        launcher = (ROOT / 'tools/launcher/launcher.cpp').read_text(encoding='utf-8')
        self.assertIn('--precompile-status', launcher)
        self.assertIn('pc_status_now()', launcher)
        self.assertNotIn('sscanf(line, "exe', launcher)

    def test_the_status_is_memoized_and_dropped_when_the_inputs_can_change(self):
        launcher = (ROOT / 'tools/launcher/launcher.cpp').read_text(encoding='utf-8')
        self.assertIn('if (g_pc_current < 0 || (force && g_pc_current != PC_STATUS_CURRENT)) {', launcher)
        self.assertIn('int status = pc_status_now();', launcher)
        # [XCDE] T282 review: a rebuilt executable drops the memo (the base code stat'ed it every call), and the probe
        # asks with the Diagnostics-tab environment the signature hashes.
        self.assertIn('static void pc_check_exe()', launcher)
        self.assertIn('SDL_GetPathInfo(path(GAME_EXE)', launcher)
        probe = launcher[launcher.index('static int pc_status_now() {'):launcher.index('static bool pc_running();')]
        self.assertIn('apply_env();', probe)
        poll = launcher[launcher.index('static void pc_poll() {'):launcher.index('// ------------------------------------------------------------------ game code')]
        self.assertIn('pc_forget();', poll)
        gc_start = launcher.index('static void gc_poll() {')
        gc = launcher[gc_start:launcher.index('\nstatic void ', gc_start + 10)]
        self.assertIn('pc_forget();', gc)
        dump = launcher[launcher.index('static void dump_poll() {'):launcher.index('static void launch() {')]
        self.assertIn('pc_forget();', dump)

    def test_the_explicit_button_forces_a_run(self):
        # [XCDE] T282 review: the button passes XCDE_PRECOMPILE_FORCE=1 to its child instead of deleting the done stamp,
        # so a spawn that fails cannot throw away a valid cache; the runtime clears the crash/in-flight lists itself.
        launcher = (ROOT / 'tools/launcher/launcher.cpp').read_text(encoding='utf-8')
        start = launcher[launcher.index('static bool pc_start(bool then_play, bool fresh = false) {'):
                         launcher.index('static void pc_stop() {')]
        self.assertIn('PcForce force(fresh)', start)
        self.assertNotIn('precompile_done.txt', start)
        self.assertIn('pc_start(false, true)', launcher)  # (the button; --shot uses it too)

    def test_the_builder_asks_the_runtime(self):
        builder = (ROOT / 'tools/gamebuild/gamebuild.py').read_text(encoding='utf-8')
        self.assertIn("'--precompile-status', str(dump)", builder)
        self.assertNotIn('f\'exe {stat.st_size}', builder)
        # [XCDE] T282 review: bounded wait, and the runtime's "unreadable" (2) does not start a doomed run.
        self.assertIn('timeout=PC_STATUS_TIMEOUT_S', builder)
        self.assertIn('PC_STATUS_UNREADABLE', builder)
        # PR #247: persistence-disabled (3) is known and non-fatal, not stale: skip the full pass without a stamp.
        self.assertIn('PC_STATUS_DISABLED', builder)
        self.assertIn('status == PC_STATUS_DISABLED', builder)
        self.assertIn('SPIR-V persistence is disabled (LARECOMP_VK_NO_SPIRV_CACHE)', builder)
        self.assertIn('the shader precompile was skipped', builder)


class BuildContracts(unittest.TestCase):
    def test_cmake_fingerprint_dependencies_cover_the_artifact_sources(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
        start = cmake.index('file(GLOB_RECURSE SHADER_SIG_SOURCES CONFIGURE_DEPENDS')
        dependencies = cmake[start:cmake.index('add_custom_command(OUTPUT "${GEN_INC}/shader_sig.h"', start)]
        for name in ('runtime/services/game_files.c', 'runtime/services/game_files.h',
                     'runtime/host/utf8_path.h', 'runtime/platform/dir_utf8.h',
                     'runtime/nvn/vulkan/bloom_prefilter.h', 'runtime/nvn/vulkan/bloom.h',
                     'runtime/nvn/vulkan/gtao.h'):
            self.assertIn(f'"${{ROOT}}/{name}"', dependencies)

    def test_every_build_path_generates_the_fingerprint_header(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
        self.assertIn('vulkan/precompile_sig', cmake)
        self.assertIn('shader_sig.py --out', cmake)
        for script in ('scripts/build_linux.sh', 'scripts/build_rt_gfx.sh'):
            build = (ROOT / script).read_text(encoding='utf-8')
            self.assertIn('shader_sig.py', build)
            self.assertIn('precompile_sig', build)
        # [XCDE] T282 review: a selective build_rt_gfx.sh unit list still refreshes the fingerprint unit.
        selective = (ROOT / 'scripts/build_rt_gfx.sh').read_text(encoding='utf-8')
        self.assertIn('[ "$f" = vulkan/precompile_sig ] || pick', selective)
        validation = (ROOT / 'tools/diag/validate_port.py').read_text(encoding='utf-8')
        self.assertIn('precompile_sig.c', validation)
        native = (ROOT / 'scripts/native_tests_linux.sh').read_text(encoding='utf-8')
        self.assertIn('test_precompile_sig', native)
        self.assertIn('runtime/nvn/vulkan/precompile_sig.c', native)
        # [XCDE] T282 review: the re-entrancy claim (parallel translation == serial output) has a native test too.
        self.assertIn('test_translate_threads', native)
        self.assertIn('test_translate_threads', validation)


if __name__ == '__main__':
    unittest.main()
