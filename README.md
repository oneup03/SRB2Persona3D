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

### Windows (MSVC)

Requires Visual Studio 2022 with the "Desktop development with C++" workload.
NASM is fetched automatically into `tools\nasm` if it isn't already on `PATH`.

```powershell
.\build.ps1                        # Release, then stage deploy\Release
.\build.ps1 -Configuration Debug   # Debug, then stage deploy\Debug
.\build.ps1 -Rebuild               # clean + rebuild
.\build.ps1 -NoDeploy              # build only
```

`build.ps1` builds `src\sdl\Srb2SDL-vc10.vcxproj` (Release|Win32) and then
stages a directly runnable game folder under `deploy\<Configuration>`: the
freshly built `SRB2Persona.exe`, the SDL2/audio runtime DLLs from `libs\`, and
the game data from `assets\installer`.

In VS Code the same script is wired to <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>B</kbd>
("Build SRB2 Persona (Release)"); there are also Debug, Rebuild, and
Build &amp; Run tasks.

Only `Win32` is supported. The `x64` configuration in the project file has
never been maintained and does not compile.

### Game data

The v1.3.6 data files (`srb2.pk3`, `SRB2P-*.pk3` / `.wad`, `patch.pk3`) are
tracked in `assets/installer/`, so a fresh clone builds into a playable game
with no extra downloads.

### CI

`.github/workflows/build.yml` runs the same `build.ps1` on `windows-latest`,
zips `deploy\Release`, and attaches it to a rolling `latest` pre-release on
every push to `srb2p_22`. Pushing a tag publishes a permanent release.

For upstream's build instructions see
[SRB2 Wiki/Source code compiling](http://wiki.srb2.org/wiki/Source_code_compiling)

## Disclaimer
Sonic Team Junior is in no way affiliated with SEGA or Sonic Team. We do not claim ownership of any of SEGA's intellectual property used in SRB2.
