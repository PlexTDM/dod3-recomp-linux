# Debugging and measuring

Everything here is an environment variable (or a `dod3.ini` key, same names)
and a line in the log. The game writes `dod3.log` beside the executable when it
has no console; the previous run is `dod3.prev.log`.

## Hands-off runs

- `tools/live_bench.sh <tag> <seconds> [VAR=val ...]`: a run of the chapter 1
  battle with nobody at the controls (`tools/autoplay.sh` starts a new game,
  skips the cutscenes until the battle HUD is on screen, then fights), and a
  summary of its frame times (`tools/bench_summary.py`). Frame rates from a run
  a person played in are not comparable. Run-to-run variance is 5-15%, so
  compare pairs, or use `DOD3_AB`. `AUTOPLAY_CUTSCENE=1` instead skips the
  chapter's opening movie and presses nothing more: the game idles at its
  starting spot while the area streams in, then stands still, the same every
  run (`FROM=120` for the steady part); the closest thing to a deterministic
  benchmark of the main thread, for comparing two builds of the lifted code.
  The hooked collision test's cycles per call (`DOD3_HOT_LOG=1`) is the other
  low-noise signal for such changes.
- `PAD_NO_HOST=1` (`tools/live_bench.sh` sets it): no controller and no key of
  the host's reaches the game. Controllers are read whether or not the game's
  window is in front, so one lying on the desk, or in use for something else,
  steers a run that was meant to be hands-off: a menu cursor a step off its
  place is a different run.
- `DOD3_AB=<switch>[,<seconds>]`: an A/B test inside one run. It flips one
  run-time switch every few seconds, counts the frames in each window and
  reports the paired difference with its standard error. Switches: `stores`,
  `icall`, `labelwake`, `querysync`, `jcpar`, `bufpool`, `kickbusy`, `msdsp`,
  `texwatch`, `patchhook`, `lzfmemo`, `vcwatch`, `snapincr`, `aniso`,
  `collnative`, `fifofast` (the walker's bulk constant/program uploads),
  `engslow` (the draw engine's old per-draw paths; on means slow),
  `icallflat`, `usleepspin`, `pumpsleep`, `kickhr`, `hleindex`, `texreads`
  (only the texture units a fragment program samples bound), `none` (the
  control, which must report no difference). `DOD3_AB_FROM=<s>` skips the boot
  and menus (default 70).
- `tools/replay_regress.sh`: the rendering regression. Replays RSX captures
  through the engine headlessly and compares the frames with a reference set
  (`tools/compare_frames.py`). The captures and references are made from the
  game and are not in the repository: record captures with `RSX_CAPTURE`
  (30 frames at a problem the player reaches) and keep a known-good replay as
  the reference.
- `rsx_replay <capture> --bench <n>`: the walker's benchmark. The capture is
  decompressed into memory and played n times in one process, the first pass
  warming the caches; each pass prints the engine's CPU per present (wall
  time, thread CPU, and the replay's own page copies taken out). The same every
  run, so two builds compare without a game (alternate them a few times; the
  machine's load still moves it a few percent). In the bench the replay stands
  in for the game's write-watch, from the capture's page records;
  `RSX_REPLAY_WATCH=0/1` overrides.

## Frames

- `PS3RECOMP_FRAME_GRAB=<prefix>`: creating `<prefix>.req` makes the game
  write the next presented frame to `<prefix>.ppm` (what autoplay's HUD check
  uses). `PS3RECOMP_FRAME_DUMP=<path>` with `_EVERY` and `_SEQ` dumps frames on
  a schedule; `RSX_DUMP_FULL=1` at the internal resolution.
  `tools/ppm2png.py` converts a frame.
- `RSX_FRAMETIME=0` turns off the `[frametime]` line every 5 s (fps, mean and
  worst frame, frames over 34 ms).
- `RSX_PRESENT_PACE_LOG=1`: with a frame-rate cap the renderer holds each
  finished frame for a moment so that the presents are evenly spaced
  (`rsx_present_pace.h`), and this says every 2 s how even: the spread of the
  frames' arrivals and of their presents, the share of each more than 15% off
  the one before, the wait, and the frames that came too late for it. `=2` a
  line per frame. `RSX_PRESENT_PACE=0` turns the pacing off.
- `DOD3_FPS_ADAPT_LOG=1`: the cap `DOD3_FPS=adaptive` has chosen, every 2 s,
  and the frame time it chose it from. `DOD3_FPS_ADAPT=<quantile>,<margin>,
<seconds>` (0.95,1.04,1.5) tunes it: the share of the frames that must be
  made in time, the spare time asked of them, and the stretch looked at.
- `RSX_SCALE_MIN=<pixels>`: render targets under that size either way keep the
  game's own size at any `RSX_SCALE` (200 here: the 322x182 blur chains of the
  light shafts and the bloom, which show their taps when scaled).
- `RSX_HITCH_LOG=<ms>` lists frames over that, with the walker's GPU-blocked
  time and what it uploaded in the frame (textures: creating, decoding and
  staging them; vertex buffers; pipelines); with `DOD3_PROF` also the sampled
  stacks of the frame; `DOD3_TRACE_HITCH=1` adds the events of the frame
  before it (guest sleeps, drains, label writes, flips, ring recycles) as
  `[trace]` lines.
- `DOD3_STUTTER_MS=<n>`: per slow frame, `[stutter-rsx]` (textures, vertex
  conversions, pipelines built) and `[stutter]` (guest threads).
- `RSX_DRAW_STATS=1`, `RSX_SNAP_STATS=1` (snapshots), `RSX_SYNC_STATS=1` (the
  walker's GPU waits), `RSX_PIPE_LOG=1` (pipelines built),
  `RSX_SURF_DUMP_DIR=<dir>` (every surface). `RSX_VK_VALIDATION=1` enables the
  Vulkan validation layer.

## Profiling

- `DOD3_STALL_MS=<n>`: when no frame has been presented for n ms, every
  thread's stack, at most `DOD3_STALL_MAX` times (12), into
  `DOD3_STALL_SAMPLE=<dir>` or the log; `tools/stack_summary.py` condenses its
  output.
- `PPU_WAITPROF=1`: time in syscalls by thread and guest call site, every 5 s,
  with the fence poll's statistics; `=2` adds the most frequent calls (who
  sends or signals how often: which thread answers a wait).
- `PS3_TIME_LOG=1`: the date and the time zone the game was told, once each
  (the first `sys_time_get_current_time` and `sys_time_get_timezone`); the game
  stamps its saves with them.
- `PPU_THREAD_SLOTS=<n>`: the thread table's first n slots only (it has 64).
  The game makes threads all through a session, two for the physics of every
  level and one for each save, load and movie, and a slot that does not come
  back stops it at the next level; a run that completes on 24 shows that they
  all do. Every `sys_ppu_thread_create` line carries its `tid`, and a full
  table says so.
- `DOD3_HOT_LOG=1`: calls per second and cycles per call of the hooked hot
  functions (the collision test).
- `DOD3_GC_LOG=1` times every garbage collection over 1 ms (`=2` adds the
  sampled stacks); `DOD3_GC_STATS=1` the cycles in the two per-object virtual
  calls.
- `SPU_TASK_STATS=1`: per SPU task image, tasks a second and their pickup,
  load, turnaround and run times. `FS_READ_TIME=1`: every file read's time.
- A crash prints the exception and a symbolised stack (`[crash]` lines);
  `DOD3_NO_CRASH_HANDLER=1` leaves it to the system.

## Audio

`AUDIO_GAPS=1` counts audio blocks the game wrote late; `AUDIO_RATE=1` the gaps
per 5 s window; `AUDIO_WRITEPOS=1` histograms where the game writes relative to
the read index.

## The native paths and their checks

| Switch                                                                                    |                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| ----------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `DOD3_GC_NATIVE=0 / 1 / check`                                                            | the lifted reachability pass, the native one (default), or both compared                                                                                                                                                                                                                                                                                                                                                                                                      |
| `DOD3_COLLISION_NATIVE=0 / 1 / check`                                                     | the lifted collision test, the native one (default; `src/patches/collision_native.cpp`, generated from the lifted body by `tools/lift_native.py`), or both on every call with the nine words they write and the result compared (the first differences printed, a count every 100,000 calls; a call that differs is run through the lifted one again, and the count says how many differences that second run does not show, which are the rerun's and not the native code's) |
| `DOD3_GC_PAR=<n>`                                                                         | worker threads for the collector's mark phase (8; 0 serial); `DOD3_GC_DEFER=0` collects where the game asks instead of in the limiter's sleep; `DOD3_GC_PREFETCH=0`                                                                                                                                                                                                                                                                                                           |
| `DOD3_SPU_NATIVE=0`                                                                       | the SPU hooks off; `DOD3_SPU_NATIVE_CHECK=1` runs each hooked stretch twice and compares every register and the local store                                                                                                                                                                                                                                                                                                                                                   |
| `DOD3_SPU_PATCH_HOOK=0`, `DOD3_LZF_MEMO=0` (`DOD3_LZF_MEMO_LOG=1`), `DOD3_MSDSP_NATIVE=0` | one path at a time                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| `DOD3_MP3_NATIVE=0`                                                                       | the original SPU MP3 decoder instead of ours, for comparison (not shipped); `DOD3_MP3_CHECK=1` runs both                                                                                                                                                                                                                                                                                                                                                                      |
| `DOD3_ZLIB_NATIVE=0`                                                                      | the SPU zlib task instead of the native inflate                                                                                                                                                                                                                                                                                                                                                                                                                               |
| `DOD3_MENU_PATCH=0`                                                                       | the game's own Settings menu, unpatched                                                                                                                                                                                                                                                                                                                                                                                                                                       |

## Runtime fallbacks

Each turns a change back off, to bisect a regression: `PPU_SLOW_STORES=1` (the
old store path), `PPU_ICALL_SLOW=1` (no indirect-call cache), `LWM_KERNEL=1`
(kernel lwmutex), `PS3_LWCOND_SLEEP=1`, `GCM_POS_FLIP=0`, `RSX_TEX_WATCH=0`
(hash textures every frame), `RSX_VCACHE=0` / `RSX_VCACHE_CHECK=1` /
`RSX_VC_WATCH_CHECK=1`, `RSX_BUF_POOL=0`, `RSX_TEX_HEAPS=0` (a committed
resource per texture), `RSX_VB_PAGES=0` (a buffer per cached mesh),
`DOD3_RENDER_TICK_HZ=40` (the title's idle tick: movies on a 25 ms grid),
`DOD3_LOAD_IDLE=0` (the loader only in its own 5 ms of a frame;
`DOD3_LOAD_IDLE_LOG=1` says what it did in the limiter's sleep),
`RSX_SNAP_INCR=0` (full snapshot copies), `SPU_TASK_POOL=0` (a thread per
task), `SPURS_TASKSET_SERIAL=01AA7700` (PhysX one task at a time),
`RSX_ASYNC_SHADERS=0` (build shaders on the walker), `RSX_QUERY_NOSYNC=1`,
`DOD3_PACE=vblank`, `DOD3_VBLANK_MULT=<n>` / `DOD3_VBLANK_HZ=<hz>`,
`DOD3_FIFO_SLEEP_MS=<n>`, `DOD3_FAST_POLL_LR=<hex>` (0 for none),
`PS3RECOMP_RSX_ENGINE=vtable` (the toolkit's old RSX path), `RSX_ENGINE_SLOW=1`
(the draw engine's per-draw paths before its cache indexes and single
fragment-program pass), `RSX_VCACHE_PROMOTE=0` (no buffer of its own for a
reused vertex-cache entry under 64 KB), `RSX_BIND_UNREAD=1` (every texture unit
that is on bound, sampled or not).

`PS3_VERBOSE=1` turns the runtime's per-event log back on (it is off here: two
lines per SPURS job at 2,700 jobs a second).

## The lift's switches

The lifter reads a handful of switches from its environment, each turning one
part of the registers-in-locals form off (`ps3recomp-soyjxck/docs/PPU_RECOMP.md`
lists them: `PPU_NV_DIRTY`, `PPU_NV_LIVE`, `PPU_NV_FLAGS`, `PPU_NV_VRALL`,
`PPU_FRAME_ENTRY`, `PPU_VM_RUNS`, `PPU_DRAIN_ELIDE`, `PPU_VM_BASE_LOCAL`). To
find the part a fault is in, lift with one of them into a folder of its own and
configure a build folder for it (`-DRECOMP_DIR=<folder>`).

`PPU_NV_VERIFY=1` lifts the self-checking form: a register a write-back leaves
out is compared with the register file or, where nothing should read it,
replaced there by a marker (`BAD0...`), and every load and store of a run
checks the address it was handed. The checks cost the game's own threads about
a third more CPU. A clean log has no `nv-verify` line; marker values in the
scratch registers of a register dump are expected.

`SPU_COH_PAGES=1` (when the game runs) names each 64 KiB page in which an SPU
reserves a line for the first time: stores to such a page take the runtime's
function from then on.

## The installer, the settings menu and the input

- `DOD3_INSTALL_BASE=<dir>`: install into and run from another folder.
- `DOD3_SETUP_ADD=<file>|<file>...`, `DOD3_SETUP_AUTO=<n>` (press the primary
  button of the next n pages), `DOD3_SETUP_GRAB=<prefix>` (save every page as
  `<prefix>_<n>.ppm`, at the page's 40th frame or `DOD3_SETUP_GRAB_AT=<frame>`):
  the setup driven without a hand on it.
- `DOD3_MENU_LANGUAGE=es|en`: the Settings pages in Spanish or English whatever
  is installed. `DOD3_MODS_LOG=1`: every path the mods serve, not only the
  first 48.
- `PS3_VFS_OVERLAY=<dir>` with `DOD3_SHA_OVERRIDE=<package>=<sha1>`: run with a
  menu patch made by `tools/menu_patch.py <dir>` without rebuilding its header.
  `python tools/ue3 dis <package> <Class> [Function]` disassembles any script;
  `python tools/ue3 diff <dir a> <dir b>` compares two builds'.
- `DOD3_FOCUS_FILE=<path>`: "out of focus" is the existence of that file, for
  testing `DOD3_UNFOCUSED`.
- `DOD3_LOG_FILE=1` writes `dod3.log` even with a console.
- `DOD3_CAM_LOG=1`: a line a frame with the view's rotation, the camera stick's
  value, the mouse's share and the frame time (the script reports the view
  through the settings bridge). `DOD3_CAM_TEST_FILE=<path>` adds the two
  numbers in the file to the camera stick's values, read 50 times a second: how
  the camera's response was measured (`src/input/camera.cpp`).
  `DOD3_KBM_LOG=1`: a line whenever what the keys and mouse buttons hold as a
  pad changes.
- `PS3RECOMP_ESCAPE_QUITS=1`: Esc closes the window instead of pressing the
  pad's back button (Circle). With a window there is no other way for a key to
  quit the game; close the window or use Alt+F4.
- `DOD3_PROMPTS_CYCLE=<seconds>`: step through every set of button prompts
  (`src/patches/prompts.cpp`), to look at the atlases on any screen that shows
  buttons. `python tools/ue3 info <package> 99999` lists a package's exports,
  its textures and menu layouts among them, when one more needs finding.
