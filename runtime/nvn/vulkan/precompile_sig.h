// [XCDE] T282: the signature of the inputs of the precompiled shader cache. `--precompile-status` (precompile.c)
// compares it with the `sig` line of cache/precompile_done.txt: a build, environment-option or game-file change that
// can alter the precompiled shaders makes them stale, anything else keeps them. See precompile_sig.c.
#pragma once
#include <stdbool.h>

// One path limit for the signature walk and the precompile listing: a path that fits one must fit the other, or a
// current stamp could never be written for it.
#define XCDE_PRECOMPILE_PATH_MAX 2048

// Whether a romfs name is one of the shader containers the precompile scans. An unreadable entry with such a name (or an
// unreadable directory, which could contain one) makes the walk incomplete; any other unreadable entry cannot change
// the shader cache and is skipped, so an unrelated dangling mod link does not disable shader precompilation.
bool vk_precompile_is_shader_container(const char *name);

// Whether an entry whose metadata could not be read (err is the metadata error, d_type its directory entry type) is
// provably irrelevant and can be skipped. Only a dangling link / missing component (ENOENT or ENOTDIR) with a known
// non-directory type and a non-container name is safe: EACCES, other errors and DT_UNKNOWN stay fatal, because the entry
// could be an inaccessible directory that holds shader containers.
bool vk_precompile_entry_skippable(const char *name, unsigned char d_type, int err);

// Writes 16 lowercase hex digits + NUL. false: the game files are not readable (nothing can be said about the cache).
// The launcher and gamebuild's setup step must run this with the same environment the game/precompile will get
// (xcde_env.ini included); the signature covers those options.
// The effective xcde.ini GPU policy changes which pipeline artifacts the precompile warms, so the runtime passes the
// values it loaded (settings.h's GPU_GPL_* / GPU_PROFILE_*). vk_precompile_sig is the auto/auto convenience wrapper.
enum { XCDE_SIG_GPU_GPL_AUTO = -1, XCDE_SIG_GPU_PROFILE_AUTO = 0 };
bool vk_precompile_sig_policy(const char *root, int gpu_gpl, int gpu_profile, char out[17]);
bool vk_precompile_sig(const char *root, char out[17]);
