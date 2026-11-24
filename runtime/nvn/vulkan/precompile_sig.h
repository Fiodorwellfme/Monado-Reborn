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

// Writes 16 lowercase hex digits + NUL. false: the game files are not readable (nothing can be said about the cache).
// The launcher and gamebuild's setup step must run this with the same environment the game/precompile will get
// (xcde_env.ini included); the signature covers those options.
bool vk_precompile_sig(const char *root, char out[17]);
