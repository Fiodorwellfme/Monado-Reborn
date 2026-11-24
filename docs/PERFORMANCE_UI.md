# Performance and the new interface — T259–T261

This continuation adds internal optimizations, shared profiles and a graphite/green interface. It does not change the
game's original files. The evidence for this machine is recorded in
`docs/evidence/2026-11-04-performance-ui/summary.md`; it does not certify other GPUs or the whole game.

## Usage

Run `Play Monado.bat`. In **Home**, choose quality and FPS target separately. For the Ryzen 7 7730U, the proposed
starting point is **Potato + 30 FPS**. It is a target, not a performance guarantee.

| Profile | Internal resolution | Settings applied |
|---|---|---|
| Quality | Matches the window (Native AA) | Auto upscaler (Native), no SMAA, Ultra grass (3x), 2048 px shadow maps, Full shadow mask, Ultra prop LOD (2x), 16x AF, original blur/effects |
| Balanced | Matches the window | Project default baseline: Auto upscaler (Quality), High grass (1.7x), Full shadow mask, 16x AF, original shadows/LOD/blur/effects |
| Handheld | 540–720 pixel height (dynamic) | FSR 3 (FSR 1 fallback), Original grass (1x), Original shadow mask, 8x AF, SSR/god rays off, original shadows/AO/bloom/DOF/blur |
| Performance | 540–720 pixel height (dynamic) | FSR 3 (FSR 1 fallback), no SMAA/DOF/blur, Original grass (1x), Original shadow mask, Low prop LOD (0.5x), cloud shadows/SSR/god rays off |
| Potato | 360–540 pixel height (dynamic) | Lowest possible: FSR 1, no SMAA/DOF/blur/TAA, Low grass (0.5x), Low prop LOD (0.5x), shadows and all optional effects off |
| Custom | Manual | Keeps the individual changes and their limits |

The ranges keep the aspect ratio and never exceed the output. The game's own menu/HUD resolution can also lose
sharpness. Changing restart-only options (game blur, shadow maps/mask, prop LOD, auto upscaler) requires a restart to
apply the full profile; live options (resolution, grass distance, effects) apply immediately in F1.
Changing quality does not change the requested FPS, GPU, volume, controls or saves.

Ambient occlusion, reflections, bloom, god rays and cloud shadows are Original / Off / On options of the launcher
and of F1 (`runtime/graphics_options.h`), applied live on the per-view copy of the render configuration (without
editing `lib_nx.ini`): verified with deterministic captures and
`XCDE_GRAPHICS_LIVE=<frame>:<key>=<value>,...` (2026-11-17; bloom Off: ~17 fewer render passes per frame). The old
experimental checkboxes (`effects_off`) are converted on load into the Off option.

## Internal optimization

- Vulkan descriptors: cache bounded to 64 entries, full identity of layout/bindings/buffers/ranges/views/samplers
  and submit sequence. It keeps preparation, uploads and transitions even on a hit. It avoids allocation/update and
  identical binds; it is invalidated when the command buffer or external state changes, or on restart.
- Indices: reusable CPU scratch when expanding quads/fans, with no `malloc/free` per draw and no reads of GPU
  write-combined memory; it initializes the copied padding.
- Textures: index by address, keeping the same compatibility criteria, precedence order and owner transfer. GPU
  destruction remains deferred.
- RomFS: cache of 256 descriptors with four ways and replacement of the least recently free entry. Active reader
  references stay protected by mutex; when all ways are busy, a private descriptor is used.
- Immutable GPU properties: queried when selecting the device. The menu queries budgets at most every 500 ms.
- Presentation: repeats use only the remaining budget of the native frame. They do not add a refresh when the frame
  is already late.
- Dynamic resolution: asynchronous GPU timing, sum of main commands and uploads, timestamp wrap handling and sample
  age. Compilations/loads must not reduce quality. Timestamps are written with dynamic resolution or profiling, not
  in the normal Balanced profile.
- Compilation: the default budget stops skipping pending draws. A variant not preloaded can cause a hitch, but it
  does not disappear temporarily to meet an arbitrary budget.

Global windowed addressing, the control-flow translator, the queue model and the renderer have not been rewritten
with bindless/multithreading. They are later work with independent measurement and acceptance. Full staging remains
a costly compatibility alternative; it is not presented as an optimization.

## Configuration and diagnostics

`gpu_preset = balanced|performance` is kept; `potato|custom` are added. New keys: `dynamic_min_height`,
`dynamic_max_height` (0 keeps the window-relative behavior), `effects_off` (mask of five effects; since 2026-11-17
it is converted on load into the Off option of `ambient_occlusion`, `reflections`, `bloom`, `godrays` and
`cloud_shadows`, and no longer has its own checkboxes) and `low_memory`. Loading an old INI keeps its individual
settings: applying a profile is an explicit action. Launcher and menu use `runtime/quality_policy.h`.

`Play Monado (performance metrics).bat` writes `build/native_metrics.csv`. It enables detailed profiling, so
comparisons must use the same mode. It records native FPS/intervals, GPU time, CPU/waits/presentation, descriptor
calls and staging bytes; the time categories can overlap and must not be summed. The writer periodically flushes the
buffer and on scheduled captures; a forced close can lose its last chunk.

```powershell
.venv/Scripts/python -X utf8 tools/diag/performance_report.py build/native_metrics.csv --start-frame 700 --output build/performance.json
```

For a test with less instrumentation, `XCDE_NATIVE_INTERVALS=<csv>` records only native intervals and a
GPU/driver/configuration identity. `tools/diag/benchmark_native.py` alternates reference/candidate, uses separate saves
and does not replace existing evidence. Both executables must include that observer; the local reference one
contains only that measurement addition on top of the original sources.

```powershell
.venv/Scripts/python -X utf8 tools/diag/benchmark_native.py --reference build/xcde_reference.exe --candidate build/xcde_optimized.exe --ini build/qa/balanced.ini --present --output build/ab-new
```

Diagnostic overrides: `XCDE_DESCRIPTOR_CACHE=0` disables set reuse; `XCDE_GPU_TIMING=0` disables GPU queries;
`XCDE_REPEAT_PRESENT=0` disables repeats; `XCDE_PIPE_WAIT_MS=<n>` explicitly restores the old budget that can skip
draws. They are not general recommendations for playing.

## Interface and compatibility

Launcher and F1 share colors, radii and profile controls. Home gathers GPU and main settings; GPU/Vulkan and
technical data stay in advanced/Diagnostics panels. Spanish/English, keyboard/gamepad, pending changes, graphics
restore and the existing options are kept. At small effective widths the labels are placed above the combos; panels
scroll for large text. The launcher reduces activity when idle and wakes up on events.

The default build path remains `Build Monado.bat` with Zig/MSYS2. In this session MSYS2's Clang installed inside
`build` was used, because Zig failed handling files/temporaries. The generator and the game objects remain in
excluded folders. Recompile the full runtime after this `Settings`/`VkG` change; do not mix objects from the previous
version with the new ones.

AMD Vega/RDNA, Intel, discrete and hybrid laptops require per-device/driver acceptance. The 16×16 Vulkan checks do
not verify game shaders. Battle, Future Connected, temporal speed at different FPS and long sessions remain separate
acceptance tests.

The next improvement must be decided with repeatable field and battle routes: compare GPU, CPU/waits, memory and
p95/p99 before expanding the renderer. The local evidence includes the recommended check order. For the interface,
keep Home with few decisions and leave GPU/Vulkan and experimental cuts in advanced options; adding more visible
controls or animations does not help this goal.
