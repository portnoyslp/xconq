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

## Verified locally (this Mac, no XQuartz installed)

- `otool -L build/sdl/sdlconq` shows only `libSDL3`/`libSDL3_ttf`/system
  libs — no X11.
- Full build (curses + SDL) succeeds; quick ctest lane passes 558/559 (the
  one failure, `check-consistency-cmd`, is pre-existing and unrelated —
  a docs/source drift check).
- `sdlconq` reaches its normal running game loop under lldb with no crash;
  default font resolves to Menlo, `-font`/`-fontsize` override it, and a
  bad `-font` path falls back to the bitmap font with a warning instead of
  erroring.
