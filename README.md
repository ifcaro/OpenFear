# OpenFear

An open-source, multiplatform reimplementation of **Atmosfear** (1995, EMG Publishing / A
Couple 'A Cowboys Pty Limited) — a multiplayer board game originally released for Windows
95/98.

This is an unofficial fan project, not affiliated with or endorsed by the original publishers.
It reimplements the game's engine from scratch in modern C++17; it does not include, bundle,
or redistribute any of the original game's copyrighted assets (graphics, video, audio, or the
printed manual). **You need your own legally-owned copy of the original game** (CD-ROM or an
already-installed copy) to play — OpenFear reads assets from a folder you point it at.

## Status

The full gameplay loop is ported and builds clean: intro → player/character setup → the board
game itself (movement, all 16 encounter types, the Duelo trio, Blackjack, and the other
embedded minigames) → post-game sequence. 115 automated tests cover pure logic and a headless
harness (no real window/audio device needed). The full manual playtest checklist
(`docs/PLAYTEST_PLAN.md`) has passed end to end.

## Platforms

| Platform | Status |
|---|---|
| Windows (SDL3) | Full build + test loop, the primary development target. |
| PS2 (homebrew, PS2Dev/SDL3) | Boots from `mass:` device (folder install); audio and render (PAL/NTSC overscan) confirmed on real hardware. A loose `.iso` on `mass:` works but is slow (FAT seek cost on a large single file) - a folder install is the fast path. CD/DVD boot and OPL/HDD not yet tested. |
| Web (Emscripten) | Builds and runs end-to-end in a real browser, including audio/video playback; asset folder picked via `showDirectoryPicker`. |
| Linux / macOS | Code audited for portability blockers (none found) but not yet built. |

## Building (Windows)

Requires Visual Studio, [vcpkg](https://github.com/microsoft/vcpkg) (for SDL3),
and CMake (bundled with Visual Studio — see below, it's usually not on `PATH`).

```
cmake.exe -S . -B build -G "Visual Studio 18 2026" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake

cmake.exe --build build --target openfear --config Debug
```

If `cmake` isn't on your `PATH`, use the one bundled with Visual Studio, typically at:
```
C:/Program Files/Microsoft Visual Studio/<version>/<edition>/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe
```

## Running

OpenFear needs to find your Atmosfear asset folder (the installed game, or the raw CD - both
work, raw CD layout is translated on the fly). Discovery order: `--assets <path>` CLI flag →
`OPENFEAR_ASSETS` environment variable → saved config from a previous run → CD/DVD drive scan →
`assets/` next to the executable. Only `--install` writes the saved config - a one-off
`--assets`/`OPENFEAR_ASSETS` override, or even a CD-scan hit, is never remembered on its own.

If nothing is found, run `openfear --install [path]` once: it finds your CD/ISO/folder the same
way and copies it into `assets/` next to the executable (or, if that's not writable, your
per-user config folder) - no path needed. Pass a path (`openfear --install D:\MyGames\OpenFear`)
to install somewhere else instead.

## Testing

```
cmake.exe --build build --target openfear-tests --config Debug
build/bin/Debug/openfear-tests.exe
```

Run from the repo root — most tests build a synthetic MAPFILE in memory (no real game data
needed), but the dedicated MAPFILE suite reads your real, installed `assets/MAPFILE` directly.
115 tests, no external test framework — see `apps/tests/`.

## Code style

`.clang-format` at the repo root defines the style, enforced in CI
(`.github/workflows/format-check.yml`) on every push/PR — not required locally to build. Apply
it with `cmake --build build --target format`, or check without changing anything via
`--target format-check`. Optional local pre-commit hook: `git config core.hooksPath scripts/git-hooks`.

## License

MIT — see `LICENSE`. Third-party dependencies (SDL3, libcinepak, libindeo3,
etc.) are all permissively licensed (MIT/zlib/BSD).

The original *Atmosfear* game and its assets are copyrighted by their respective owners (EMG
Publishing / A Couple 'A Cowboys Pty Limited, 1995) and are **not** part of
this repository or this license.
