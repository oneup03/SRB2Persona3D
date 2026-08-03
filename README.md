# Sonic Robo Blast 2
[![latest release](https://badgen.net/github/release/STJr/SRB2/stable)](https://github.com/STJr/SRB2/releases/latest)

[![Build status](https://ci.appveyor.com/api/projects/status/399d4hcw9yy7hg2y?svg=true)](https://ci.appveyor.com/project/STJr/srb2)
[![Build status](https://travis-ci.org/STJr/SRB2.svg?branch=master)](https://travis-ci.org/STJr/SRB2)
[![CircleCI](https://circleci.com/gh/STJr/SRB2/tree/master.svg?style=svg)](https://circleci.com/gh/STJr/SRB2/tree/master)

[Sonic Robo Blast 2](https://srb2.org/) is a 3D Sonic the Hedgehog fangame based on a modified version of [Doom Legacy](http://doomlegacy.sourceforge.net/).

## Dependencies
- NASM (x86 builds only)
- SDL2 (Linux/OS X only)
- SDL2-Mixer (Linux/OS X only)
- libupnp (Linux/OS X only)
- libgme (Linux/OS X only)
- libopenmpt (Linux/OS X only)

## Compiling

### Windows (MinGW-w64, 64-bit)

Requires MinGW-w64 and `make`, run from git-bash. `build.sh` adds the default
chocolatey MinGW location (`C:\ProgramData\mingw64\mingw64\bin`) to `PATH`
automatically if it's there.

```bash
./build.sh                # Release, then stage deploy/Release
./build.sh --clean        # make clean first
./build.sh --debug        # DEBUGMODE=1, staged into deploy/Debug
./build.sh --no-deploy    # build only
```

`build.sh` builds bundled zlib/libpng, compiles `bin/srb2win64.exe` via
`src/Makefile` (`MINGW64=1 SDL=1`), and stages a directly runnable game folder
under `deploy/<Release|Debug>`: `SRB2Persona.exe`, the SDL2/audio runtime DLLs
from `libs/`, and the game data from `assets/installer`.

Two things the script handles that are easy to trip over:

- The bundled `libz64.a` doesn't exist and `libpng64.a` is stale (it references
  `__iob_func`, which current mingw-w64 no longer exports), so both are rebuilt
  from the sources in `libs/` on every run.
- `CPPFLAGS=-std=gnu17` — this codebase predates the C23 `()`-means-`(void)`
  change that became GCC's default in 14+.

The same script cross-compiles from Linux with `gcc-mingw-w64-x86-64-win32`,
which is what CI does.

In VS Code, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>B</kbd> runs
"Build SRB2 Persona (Release)"; Clean, Debug, and Run tasks are also wired up.

The `src/sdl/Srb2SDL-vc10.vcxproj` MSVC project still compiles (the source is
MSVC-clean), but the resulting binary renders incorrectly and is **not
supported** — use the MinGW build. Note that an MSVC build also creates an
`objs/` directory, which makes `src/Makefile` refuse to run until you delete it.

### Game data

The v1.3.6 data files (`srb2.pk3`, `SRB2P-*.pk3` / `.wad`, `patch.pk3`) are
tracked in `assets/installer/`, so a fresh clone builds into a playable game
with no extra downloads.

### CI

`.github/workflows/build.yml` runs the same `build.sh` on `ubuntu-latest`
(cross-compiling with MinGW-w64), zips `deploy/Release`, and attaches it to a
rolling `latest` pre-release on every push to `srb2p_22`. Pushing a tag
publishes a permanent release.

For upstream's build instructions see
[SRB2 Wiki/Source code compiling](http://wiki.srb2.org/wiki/Source_code_compiling)

## Disclaimer
Sonic Team Junior is in no way affiliated with SEGA or Sonic Team. We do not claim ownership of any of SEGA's intellectual property used in SRB2.
