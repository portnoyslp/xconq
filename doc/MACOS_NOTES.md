# Native macOS notes for sdlconq

*Branch: `native-macos`. Summarizes what changed to get `sdlconq` building
and running natively on macOS without XQuartz, plus real TrueType font
support. This is a working-notes doc for this branch, not part of the
upstream `MODERNIZATION-PLAN.md`.*

## 1. Dropped the XQuartz/X11 dependency

`sdl/CMakeLists.txt` unconditionally linked `X11::Xext X11::Xmu X11::X11`,
and the top-level `CMakeLists.txt` gated the whole `sdl` subdirectory on
`X11_FOUND` — both carried forward unmodified through the SDL1.2→2→3
migrations. Neither was ever load-bearing: no code under `sdl/` calls any
Xlib/Xmu/Xext API (confirmed by `grep` and by `otool -L` on the built
binary, which links only `libSDL3` and system libs). Removed both; XQuartz
is no longer required to build or run `sdlconq` on macOS.

## 2. Fixed a NULL-`dside` segfault

Uncovered while testing the above: `launch_game()` in `sdl/sdlmain.cc`
called `place_legends(dside)` unconditionally. When no side ends up wanting
a display (e.g. no player configured), `dside` is `NULL`, and
`place_legends`'s `side != dside` guard doesn't catch `NULL != NULL`, so it
fell through into `terrain_seen_at()` and dereferenced a NULL `Side*`.
Guarded the call so `init_all_displays()`'s existing "must have at least one
display" check gets to run and exit cleanly instead.

## 3. Made the default player actually request a display on macOS

`side_wants_display()` gates on `Player.displayname` being non-NULL.
`sdl/sdlunix.cc`'s `add_default_player()` set that from `getenv("DISPLAY")`
— meaningless on macOS, which has no `$DISPLAY`. Added an `APPLE` macro
(`CMakeLists.txt` → `kernel/acdefs.h.in`, using CMake's builtin `APPLE` var,
same convention as the existing `UNIX` macro) and used it to set a
placeholder `displayname` on macOS, mirroring how `sdlwin32.cc` already
does this for the (currently unbuilt) Windows port (`"WinSDL"`).

## 4. Real TrueType font rendering (SDL3_ttf)

`sdlconq` previously drew all text from a single fixed bitmap glyph sheet
(`images/font.bmp`), loaded once at startup with hardcoded pixel-grid math
in `draw_string()` (`sdl/sdlscreen.cc`) — no way to change font, size, or
family. Added optional `SDL3_ttf` support:

- **Dependency**: `find_package(SDL3_ttf)` in `CMakeLists.txt`, feeding a
  `HAVE_SDL3_TTF` macro (`kernel/acdefs.h.in`). Purely additive — if
  `SDL3_ttf` isn't found, `sdlconq` still builds and falls back to the
  original bitmap renderer.
- **Default font**: macOS defaults to the system Menlo
  (`/System/Library/Fonts/Menlo.ttc`, gated on `APPLE`, same pattern as
  §3); other platforms probe a short list of common distro monospace font
  paths (`sdl/sdlunix.cc`'s `resolve_default_font()`). If nothing is found,
  falls back to `font.bmp` — same as if `SDL3_ttf` weren't installed at
  all.
- **CLI flags**: `-font <path.ttf>` / `-fontsize <points>`, parsed directly
  in `sdlunix.cc`'s `main()` (SDL-only; the shared kernel `cmdline.cc`
  option parser doesn't know about them and would reject them as
  unrecognized, so they're stripped out of `argv` before it runs).
- **Rendering**: `draw_string()` gained a `TTF_RenderText_Blended`-based
  path (`draw_string_ttf()` in `sdl/sdlscreen.cc`), matching the bitmap
  path's `\n`/`\t` handling and line spacing so callers don't need to
  care which renderer is active. Text renders in white (`{255,255,255,255}`),
  matching `font.bmp`'s original appearance.
- **Fallback everywhere**: missing `SDL3_ttf` at configure time, a font
  that fails to open, or `TTF_Init()` failing all fall back to the bitmap
  renderer with a warning rather than a hard error — `font.bmp` is the
  guaranteed-working path.

Local development: `brew install sdl3_ttf` (Linux: build-from-source in CI,
see `.github/workflows/c-cpp.yml`, since `libsdl3-ttf-dev` doesn't exist as
a distro package yet either).

## 5. TODO: what a real native macOS UI would need

Investigated the two deleted historical UIs (both removed in `ae1bed8`,
source only in git history at `ae1bed8~1`) for feature/functionality
reference: the Xt/Xaw client (`x11/`) and the Tcl/Tk client (`tcltk/`).

**Important correction up front**: neither historical UI was ever actually
*native* on macOS. Xt/Xaw is raw X11, full stop. The Tcl/Tk client's Mac
support (`tcltk/tkmac.c`) targeted **classic pre-OS X Mac Toolbox** APIs
(`FSSpec`, `HGetVol`, classic AppleEvents) and its own `#ifdef MACOSX`
branch is explicitly commented "not yet enabled (or tested)"; every
`tcl_platform(platform) == "macintosh"` branch in `tkconq.tcl` (Apple menu,
Cmd-key accelerators) is dead code on real macOS, where Tk reports platform
`"unix"`. So: useful below as a list of *what functionality existed*, not
as a reference for what a native Mac version looked like — that's still
entirely unbuilt.

Current `sdlconq` state, for comparison: no menu of any kind (pure keyboard
command dispatch via `sdl/sdlcmd.cc`'s `do_*` functions), and a surprising
amount of UI-adjacent logic carried over from the Tcl/Tk port is present in
source but **compiled out** (`#if 0`) rather than reimplemented for SDL —
`interpret_variants()`, `check_network()`'s stage-transition/chat feedback,
`schedule_movie()`/`play_movies()`, and `ui_update_state()` (the
preferences *writer* — see §4 above; reading already works, writing never
has) are all disabled function bodies in `sdl/sdlmain.cc`. `close_displays()`
is an empty no-op. `popup_game_dialog()` is a literal stub that draws a
random-colored rectangle. `unit_research_dialog()` is empty. There is no
`.app` bundle, `Info.plist`, or `.icns` icon anywhere in the tree — `sdlconq`
is a bare Unix executable today, not a packaged Mac app.

### Design principle: sdlconq is the shared future UI, not just a Mac vehicle

`MODERNIZATION-PLAN.md` §9 (PROMPT 9.2, lines ~1130-1164) already treats
`sdl/` as "the long-term evolution" of Xconq's UI — the sole graphical
client now that Tcl/Tk is gone — and states its intent for it as a
cross-platform "sleeker, faster replacement," not a macOS-only effort.
The upstream CI matrix builds `sdlconq` on Linux too
(`.github/workflows/c-cpp.yml`). So: **most of the TODO items below are
not macOS-specific** — they're general SDL-port completion work this
macOS-focused investigation happened to surface. Keep that distinction
explicit rather than accidentally baking Mac-only assumptions into shared
code paths.

The pattern already established in §3/§4 above is the model to follow:
platform-specific behavior isolated behind a narrow `#ifdef APPLE` (or
equivalent) at the exact point of divergence — the default font path, the
`displayname` placeholder — with everything else (the TTF rendering path,
the `-font`/`-fontsize` flags, the fallback logic) written once and shared
across platforms. Each item below is tagged **(shared)** or
**(macOS-specific)** accordingly; a few are "shared design, per-platform
implementation" — worth deciding deliberately rather than defaulting to
a native-only approach.

Roughly in build order:

- [x] **App packaging** — **(macOS-specific)**. *(done — see §6 below.)*
  ~~Wrap `sdlconq` as a real `.app` bundle (`MACOSX_BUNDLE` in
  `sdl/CMakeLists.txt`, an `Info.plist`, an `.icns` icon). Foundational for
  a Mac release — everything else feels more like "a real Mac app" once
  this exists (Dock icon, `Cmd-Q` quit semantics, double-click launch
  instead of terminal-only). A Linux release would need its own, unrelated
  packaging work (`.desktop` file, AppImage/Flatpak, etc.) — not reusable
  from this item, just an analogous one.~~
- [ ] **Menu bar** — **(shared design, per-platform implementation)**.
  Nothing to port 1:1 — Xt/Xaw never had a real menu bar (see the closeup
  item below), and Tk's menu bar was never Mac-native. But Tk's menu
  *categories* are a reasonable cross-platform feature checklist: File,
  Edit, Find, Orders, More Orders, Side, View, Windows, Help (`tkconq.tcl`,
  menu-setup block from ~line 2654). Several of those were themselves
  permanently `-state disabled` stubs in Tk too (New/Open Game, Print,
  Cut/Copy/Paste, Closeup, City Dialog) — don't treat the old menu as a
  fully-working spec, just a starting checklist. Worth deciding explicitly
  between an SDL-rendered in-app menu (fully shared code, works identically
  on Linux) versus a native `NSMenu` via a thin Cocoa shim (better Mac
  citizenship, but platform-specific code with no Linux equivalent) —
  don't default into the native-only path without weighing that tradeoff
  against the Linux-parity goal.
- [ ] **Preferences dialog + fixing persistence** — **(shared)**. Tk's
  `popup_preferences_dialog` (`tkconq.tcl:4656-5115`) is the best reference
  available: a tabbed panel (topic listbox) covering fonts, map display,
  files, network, and imagery, with live-apply (`ok_preferences`, line
  5116, pushes font family/size to every open widget immediately). This is
  exactly the UX the still-dead `default_font_family`/`default_font_size`
  write path should drive — `ui_update_state()` in `sdl/sdlmain.cc`
  (currently `#if 0`'d, see §4) is where that plumbing already half-exists
  and just needs a real caller and a real dialog in front of it. None of
  this — the prefs file format, the dialog contents, the font-apply logic
  — is macOS-specific; only the bundled-default-font *value* is (§4's
  `APPLE`-gated Menlo path), and that's already isolated correctly.
- [x] **New-game / setup dialog** — **(shared)**. *(done, first pass —
  see §7 below; picture previews and interactive player assignment
  deferred, tracked as new follow-up items.)* ~~replacing the
  `popup_game_dialog()` stub. Both old UIs did this for real: Xt/Xaw's
  `popup_game()` (`x11/xinit.c:~1040-1330` — game list with blurbs,
  per-game variant widgets, instructions pane, full player-assignment
  table) and Tk's `create_newgame_window`/`popup_variants_dialog`/
  `popup_player_dialog` are both solid references. This also needs
  `interpret_variants()` (`sdl/sdlmain.cc`, currently `#if 0`'d) actually
  reimplemented — right now there's no way to configure game options
  (world size, real-time, economy/supply toggles, etc.) before starting a
  game at all, on any platform. Pure game-setup logic, no macOS
  dependency.~~
- [ ] **New-game dialog: module preview pictures** — **(shared)**, follow-up
  to the item above. `Module.picturename` (`kernel/module.h`) is available
  but unused by the new dialog — it's text-only (title + blurb) for now.
  Loading and displaying per-game preview images is a reasonable next
  enhancement, not required for the dialog to be useful.
- [ ] **New-game dialog: interactive player-assignment table** —
  **(shared)**, follow-up to the item above. The dialog picks the module
  and variants only; side/player type (human/AI/none) still comes entirely
  from the existing default-player/AI auto-assignment
  (`set_players_from_options()` in `kernel/cmdline.cc`, unchanged and still
  working). Both historical UIs treated this as a large, separate
  subsystem (Xt/Xaw's full player-assignment table in `popup_game()`,
  Tk's `popup_player_dialog`) — worth doing properly rather than folding
  into the first pass.
- [ ] **Unit/side closeup windows** — **(shared)**. Both old UIs had these
  as a real subsystem — Xt/Xaw's `x11/xcloseup.c` (3000+ lines:
  `UnitCloseup`, `SideCloseup`, `CloseupSummary` types) is the more
  complete reference; Tk had menu entries for these but they were
  permanently disabled stubs. Current `sdlconq` has some inline info-panel
  rendering (`sdl/sdlscreen.cc`'s panel-drawing code) but nothing
  resembling a real popup closeup window. Nothing about this is
  platform-specific.
- [ ] **Help viewer** — **(shared)**. Both old UIs had a genuine
  hypertext-style browser with topic navigation — Xt/Xaw's `x11/xhelp.c`
  (`create_help`, `popup_help`/`popdown_help`) and Tk's
  `popup_help_dialog` (`tkconq.tcl:5454`, hierarchical topic tree via the
  one actually-used BWidget, `Tree`). Nothing exists in `sdlconq` today
  beyond terminal text (`print_instructions()`).
- [ ] **About/info box** — **(mostly shared; only branding/OS-version
  detail would differ per platform)**. Absent in both old UIs too (Xt/Xaw
  only ever printed version/license to the terminal; Tk's "About Xconq…"
  menu item had no `-command` at all) — not a regression, but still worth
  adding properly for a polished app rather than perpetuating the gap.
- [ ] **Sound** — **(shared)**, low priority. Neither historical UI had
  real audio, only a system beep (`XBell()` in Xt/Xaw, Tcl `bell` in Tk);
  `sdlconq`'s `beep()` (`sdl/sdlmain.cc`) matches that precedent already
  (just a `printf`). Not a regression worth prioritizing on any platform.
- [ ] **Movies/cutscenes** — **(shared)**, low priority. `schedule_movie()`/
  `play_movies()` are `#if 0`'d in `sdl/sdlmain.cc`; unclear either old UI
  ever finished this either (an unused `movie_sound` enum value in both
  suggests it was aspirational there too). Probably fine to leave disabled
  indefinitely unless a specific game module needs it.
- [ ] **Multiplayer UI feedback** — **(shared)**, **deprioritized further
  as of `master`'s `27d5807`**. `check_network()` in `sdl/sdlmain.cc` is
  entirely `#if 0`'d — stage-transition dialogs and "player has quit" chat
  messages are dead. Joining/hosting a game today is CLI-arg only either
  way (`-x`/`option_game_to_join`/`option_game_to_host`); even the Xt/Xaw
  man page's own BUGS section flagged this as unfinished upstream, so
  there's no complete old reference to lean on here — Tk's
  `popup_chat`/`join_game`/`host_game` (`tkconq.tcl:~1708-1747`) is the
  more complete (if still minimal) example. This used to be tagged plain
  **(shared)**, but `ARCHITECTURE.md`/`MODERNIZATION-PLAN.md` §10 (merged
  into `master` after this branch started) call for the legacy lockstep
  protocol this UI layer sits on top of (`kernel/tp.cc`/`socket.cc`) to be
  **removed outright, not bridged**, once the new server-authoritative
  WebSocket/JSON protocol lands. Building real UI on top of a protocol
  that's slated for deletion is wasted effort — lowest priority of
  everything on this list, below sound and movies, until that
  rearchitecture direction is further along (or reversed).

## 6. Packaged sdlconq as a real, relocatable .app bundle

`sdlconq` was a bare Unix executable — no Dock icon, no double-click
launch, no `Cmd-Q`. Added `MACOSX_BUNDLE` packaging (`sdl/CMakeLists.txt`,
gated behind a new `XCONQ_MACOS_BUNDLE` option, default `ON` when `APPLE`)
producing `sdlconq.app` with a real `Info.plist` (`sdl/Info.plist.in`,
identifier `org.xconq.sdlconq`, version substituted from the top-level
`XCONQ_VERSION_MAIN`) and icon (`sdl/Xconq.icns`).

**Icon provenance**: searched all of git history, including the deleted
`tcltk/` client's own copy, for higher-resolution source art — nothing
beyond 48×48 exists anywhere; the same hex-map-and-city-skyline artwork
appears across every historical UI (`sdl/Xconq.ico`, `curses/Xconq.ico`,
the old `tcltk/Xconq.ico`). `sdl/Xconq.ico` is a multi-entry Windows icon
(15 entries: every combination of 16/32/48px at 1/4/8/24/32bpp) — the
first pass here missed that and extracted the 1bpp monochrome 48×48
entry, then hand-colored the two hexagon backgrounds green/blue as a
workaround. Redone properly: the file already has a genuine full-color
48×48 32bpp entry (green city hexagon, cyan ship hexagon, hand-drawn, not
a recolor), manually decoded from the ICO's raw BITMAPINFOHEADER data
(Pillow's ICO reader doesn't expose a way to pick a specific bpp variant
at a given size) and used as the `sdl/Xconq.icns` source instead —
supersedes the flood-fill hack. Generated via `sips`/`iconutil`
(nearest-neighbor upscale to keep the pixel art crisp rather than
blurring it) — looks correct at Dock/menu-bar size, soft at
Launchpad/Finder large-icon size, same caveat as before just with real
color now. `sdl/Xconq.ico` itself (the shared legacy asset
`curses/`/Windows resources still use) is untouched.

**Relocatability** (the part that makes this a real bundle, not just
chrome): rather than a wrapper-script trick, added a small
`#ifdef APPLE`-gated function to `sdl/sdlunix.cc`,
`resolve_bundle_library_path()`, called at the very top of `main()`. It
resolves `argv[0]`, checks for `/Contents/MacOS/` in the path, and — if
found and `XCONQLIB` isn't already set — points `XCONQLIB` at the bundle's
own `Contents/Resources/lib`. `kernel/init.cc`'s `init_library_path()`
already reads `XCONQLIB` from the environment, and `kernel/unix.cc`'s
`default_images_pathname()` already looks for `<libpath>/../images`
first — so setting that one env var was enough to redirect both `lib/`
and `images/` lookups with zero kernel changes. (Confirmed while
researching this: CLAUDE.md's mention of an `XCONQIMAGES` env var override
is stale — it's never read via `getenv()` anywhere, only used as a literal
directory-name component.) `lib/`/`images/` are copied — not symlinked —
into `Contents/Resources/` at build time via a `POST_BUILD` custom
command, so the bundle is genuinely self-contained.

No code signing — ad-hoc/unsigned is sufficient for local Gatekeeper-
permitted execution; Developer ID + notarization needs a paid Apple
Developer account and is a maintainer decision, out of scope here (same
treatment this branch already gave Windows revival in
`MODERNIZATION-PLAN.md`).

One workflow-visible side effect worth knowing: with `XCONQ_MACOS_BUNDLE`
on (the default on macOS), the built binary now lives at
`build/sdl/sdlconq.app/Contents/MacOS/sdlconq` instead of flat at
`build/sdl/sdlconq` — direct invocation for debugging (lldb, headless
smoke tests, etc.) needs the full bundle path, or use `open
build/sdl/sdlconq.app` for a real launch. Set `-DXCONQ_MACOS_BUNDLE=OFF`
to get the old flat-binary layout back without any code change.

## 7. Real New Game / setup dialog (first pass)

Running `sdlconq` with no arguments always silently loaded `STANDARD_GAME`
("standard") with default variants — no game picker, no variant
configuration, ever, on any platform. Two things compounded to cause
this: `popup_game_dialog()` (`sdl/sdlmain.cc`) was a literal stub that
drew a random-colored rectangle, and it was **never actually called** —
`sdlunix.cc`'s `main()` unconditionally did
`if (using_sdl) option_popup_new_game_dialog = FALSE;`, and `using_sdl` is
always `TRUE` for this UI. Removed that override.

Also fixed the control flow: calling `popup_game_dialog()` used to be a
dead end (the `if (option_popup_new_game_dialog) {...} else if
(option_game_to_join) {...} else {...}` chain meant nothing loaded a game
afterward). It's now a prefix step — `popup_game_dialog()` sets
`mainmodule` and applies chosen variants, then the existing join/host/solo
logic runs afterward exactly as before, just picking up those choices
instead of always defaulting.

**Architecture**: this dialog runs *before* any `Screen`/`Map`/game state
exists (`main()` calls it ahead of `load_all_modules()`), so it can't
reuse the in-game `Panel`/`SDLButton`/`ask_string` widgets — all of those
assume an active `Screen*`. It's a small, self-contained SDL event loop
(`SDL_WaitEvent`, drawing straight onto `mscreen` via `draw_string()`/
`SDL_FillSurfaceRect`/`SDL_UpdateWindowSurface`) with its own hand-rolled
click hit-testing — not a new general widget system, just this one
pre-game screen.

**What it does**: `collect_possible_games()` (`kernel/ui.cc`, reads
`lib/game.dir`, ~89 entries including the intro/standard modules —
already existed, was simply never called from `sdl/`) populates a
scrollable list. Selecting a game shows its blurb (word-wrapped to a
fixed character width — no real glyph-width measurement, "basic" scope)
and a toggle row per `Module.variants[]` entry (simple variants toggle
0/1; the special `world-size` variant gets a basic +10-per-click stepper
instead of independent width/height/circumference entry). Start applies
the choice via `mainmodule = <chosen>` and `net_set_variant_value()` per
variant — the exact same kernel entry points the `-g`/`-v` CLI flags
already use (`kernel/cmdline.cc`), so no kernel changes were needed at
all; the gap was entirely the missing SDL-side UI.

**Deferred** (tracked as new items in §5's list above): module preview
pictures, and an interactive player-assignment table — the existing
default-player/AI auto-assignment keeps working underneath unchanged.

The dialog's code (all the `gamedlg_*` helpers and `popup_game_dialog()`
itself) lives in its own file, `sdl/sdlgamedlg.cc`, not `sdlmain.cc` —
split out afterward since `sdlmain.cc` was already the largest file in
`sdl/` by a wide margin (3139 lines) and the dialog code was fully
self-contained (only touches things already exposed as proper externs:
`mscreen`, `mainmodule`, `possible_games`, etc.), matching the existing
per-concern file convention (`sdlcmd.cc`, `sdlscreen.cc`, `sdlmap.cc`,
`sdluact.cc`, ...).

Two fixes since the initial pass, both in `sdl/sdlgamedlg.cc`:

- **Cursor visibility**: `initial_ui_init()` hides the OS cursor in favor
  of the in-game custom cursor sprite, but that sprite is only ever drawn
  from the `Screen`-based render path (`sdlscreen.cc`) — nonexistent while
  this dialog is up, so the cursor was simply invisible. `popup_game_dialog()`
  now calls `SDL_ShowCursor()` on entry and `SDL_HideCursor()` before
  returning, handing back to the normal behavior once the real game starts.
- **Scrollbar**: the game list (89 entries) only had mouse-wheel scrolling
  with no visual indicator of position. SDL has no built-in widgets of any
  kind (confirmed — it's strictly windowing/graphics/input, true of
  `SDL_ttf`/`SDL_image`/`SDL_mixer` too), so this is hand-rolled the same
  way as everything else here: a track + proportional thumb drawn in
  `gamedlg_draw_list()`, click-to-jump and drag-to-scroll via
  `gamedlg_scroll_from_y()` (shared by both `SDL_EVENT_MOUSE_BUTTON_DOWN`
  and `SDL_EVENT_MOUSE_MOTION` with `SDL_BUTTON_LMASK` held), geometry
  centralized in `gamedlg_scrollbar_track()` so drawing and hit-testing
  can't drift apart.

## Verified locally (this Mac, no XQuartz installed)

- `otool -L build/sdl/sdlconq.app/Contents/MacOS/sdlconq` shows only
  `libSDL3`/`libSDL3_ttf`/system libs — no X11.
- Full build (curses + SDL) succeeds; quick ctest lane passes 558/559 (the
  one failure, `check-consistency-cmd`, is pre-existing and unrelated —
  a docs/source drift check).
- `sdlconq` reaches its normal running game loop under lldb with no crash;
  default font resolves to Menlo, `-font`/`-fontsize` override it, and a
  bad `-font` path falls back to the bitmap font with a warning instead of
  erroring.
- Bundle structure confirmed (`Contents/MacOS/sdlconq`,
  `Contents/Resources/{lib,images,Xconq.icns}`, `Contents/Info.plist`).
- **Relocatability actually verified, not assumed**: copied `sdlconq.app`
  to `/tmp` (far from the source checkout) and confirmed under lldb that
  `getenv("XCONQLIB")` resolved to the copied bundle's own
  `Contents/Resources/lib`, and the game ran correctly from there with no
  "could not find library" errors.
- **New Game dialog**: this session's environment has no reliable way to
  drive real mouse/window interaction (screenshot tooling didn't surface
  the actual window content, and `osascript`-based accessibility is
  denied), so the interactive click-through itself needs your own
  eyeballs/mouse. What *was* verified directly: `collect_possible_games()`
  populates 89 real entries under lldb; a breakpoint-driven call confirmed
  `mainmodule` gets set correctly and *persists* when pointed at a
  non-default module (`ww2s-eur-42`); and running the equivalent `-g
  ww2s-eur-42` CLI path (same `mainmodule` global, same kernel loading
  code) confirmed that specific module actually loads and reaches the
  normal running state rather than silently falling back to "standard".
  Compiles with zero new warnings. Please click through it once for real
  before considering this fully done.
- **Cursor fix + scrollbar**: cursor fix confirmed by you directly (was
  invisible, now visible). Scrollbar geometry/hit-testing verified via
  lldb by calling `gamedlg_scroll_from_y()`/`gamedlg_in_scrollbar_column()`
  with known inputs and checking against hand-computed expected values
  (e.g. `gamedlg_scroll_from_y()` at the track's top/middle/bottom/
  out-of-bounds y-coordinates all matched exactly; column detection
  correctly distinguished the scrollbar from the rest of the list) — same
  screenshot/accessibility limitations as above mean the actual
  click-and-drag feel still wants your own pass.
