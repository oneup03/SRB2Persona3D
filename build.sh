#!/usr/bin/env bash
#
# build.sh - Build SRB2 Persona (64-bit, MinGW-w64 + SDL2/OpenGL)
#
# This is the supported build. The fork was developed against MinGW/GCC and
# ships MinGW-built runtime DLLs; the MSVC project (src/sdl/Srb2SDL-vc10.vcxproj)
# still compiles but renders incorrectly, so use this one.
#
# Works natively on Windows under git-bash with MinGW-w64, and when
# cross-compiling from Linux (gcc-mingw-w64-x86-64-win32). It rebuilds the
# bundled libpng/zlib static libs, builds bin/srb2win64.exe via src/Makefile,
# and stages a runnable game folder in deploy/<Release|Debug>.
#
# Usage:
#   ./build.sh [--clean] [--debug] [--no-deploy] [--jobs N]
#
# Options:
#   --clean       run "make clean" for the Mingw64 target before building
#   --debug       DEBUGMODE=1 build (output lands in bin/debug/)
#   --no-deploy   build only; don't stage the deploy folder
#   --jobs N      parallel make jobs (default: number of CPUs)
#
# Environment overrides:
#   PREFIX    toolchain prefix (default: x86_64-w64-mingw32)
#   JOBS      parallel jobs (default: nproc)
#   DEPLOYDIR staging folder (default: deploy/Release, or deploy/Debug)
#
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

# Convenience: on Windows, add the default choco MinGW-w64 location if present.
[ -d /c/ProgramData/mingw64/mingw64/bin ] && PATH="/c/ProgramData/mingw64/mingw64/bin:$PATH"

PREFIX="${PREFIX:-x86_64-w64-mingw32}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
DO_CLEAN=0
DO_DEPLOY=1
DEBUG=0

while [ $# -gt 0 ]; do
	case "$1" in
		--clean)      DO_CLEAN=1 ;;
		--debug)      DEBUG=1 ;;
		--no-deploy)  DO_DEPLOY=0 ;;
		--jobs)       JOBS="$2"; shift ;;
		--jobs=*)     JOBS="${1#*=}" ;;
		-h|--help)    sed -n '2,/^$/p' "$0" | sed 's/^# \?//'; exit 0 ;;
		*) echo "build.sh: unknown option: $1" >&2; exit 1 ;;
	esac
	shift
done

# Resolve toolchain binaries: prefer the prefixed names (Linux cross-compiler),
# fall back to unprefixed (a native Windows MinGW-w64 install ships those).
pick() { if command -v "${PREFIX}-$1" >/dev/null 2>&1; then echo "${PREFIX}-$1"; else echo "$1"; fi; }
CC=$(pick gcc)
AR=$(pick ar)
RANLIB=$(pick ranlib)
WINDRES=$(pick windres)
OBJCOPY=$(pick objcopy)
STRIP=$(pick strip)

command -v "$CC" >/dev/null || { echo "build.sh: '$CC' not found in PATH" >&2; exit 1; }
echo ">> toolchain : $("$CC" --version | head -1)"
echo ">> jobs      : $JOBS"

# 1+2. Rebuild the bundled zlib and libpng static libs.
#
# The committed libz64.a/libpng64.a were linked against an old MinGW runtime
# and reference symbols (__iob_func, _vsnprintf) that current mingw-w64 import
# libraries no longer expose, so linking against them fails.
#
# They're tracked files, so we build fresh copies into libs/.build64 (ignored)
# and point the link step at those via ZLIB_LDFLAGS/PNG_LDFLAGS rather than
# overwriting the repo's copies and leaving a permanently dirty tree.
BUILDLIB="$PWD/libs/.build64"
mkdir -p "$BUILDLIB"

echo ">> building libs/.build64/libz64.a"
ZLIB_SRCS="adler32 compress crc32 deflate gzclose gzlib gzread gzwrite \
           infback inffast inflate inftrees trees uncompr zutil"
(
	cd libs/zlib
	rm -f ./*.o
	for f in $ZLIB_SRCS; do "$CC" -c -O3 -DNO_VIZ "$f.c" -o "$f.o"; done
	rm -f "$BUILDLIB/libz64.a"
	"$AR" rcs "$BUILDLIB/libz64.a" ./*.o
	"$RANLIB" "$BUILDLIB/libz64.a"
	rm -f ./*.o
)

echo ">> building libs/.build64/libpng64.a"
PNG_SRCS="png pngerror pngget pngmem pngpread pngread pngrio pngrtran \
          pngrutil pngset pngtrans pngwio pngwrite pngwtran pngwutil"
(
	cd libs/libpng-src
	rm -f ./*.o
	for f in $PNG_SRCS; do "$CC" -c -O3 -I../zlib "$f.c" -o "$f.o"; done
	rm -f "$BUILDLIB/libpng64.a"
	"$AR" rcs "$BUILDLIB/libpng64.a" ./*.o
	"$RANLIB" "$BUILDLIB/libpng64.a"
	rm -f ./*.o
)

# 3. Build the executable.
MAKEFLAGS_EXTRA=()
[ "$DEBUG" = 1 ] && MAKEFLAGS_EXTRA+=(DEBUGMODE=1)

if [ "$DO_CLEAN" = 1 ]; then
	echo ">> make clean"
	make -C src MINGW64=1 SDL=1 "${MAKEFLAGS_EXTRA[@]}" clean >/dev/null 2>&1 || true
fi

echo ">> building srb2win64.exe"
# -std=gnu17: this codebase predates the C23 "() means (void)" change that
# became the default in GCC 14+. Passed via CPPFLAGS (not OPTS=) so the
# Makefile's own OPTS+= accumulations still apply.
export CPPFLAGS="-std=gnu17"
# ZLIB_LDFLAGS/PNG_LDFLAGS point the linker at the archives rebuilt above
# instead of win32.mk's defaults (../libs/zlib/win32, ../libs/libpng-src/
# projects). Paths are relative to src/, where make runs.
make -C src -j"$JOBS" \
	MINGW64=1 SDL=1 "${MAKEFLAGS_EXTRA[@]}" \
	ZLIB_LDFLAGS="-L../libs/.build64 -lz64" \
	PNG_LDFLAGS="-L../libs/.build64 -lpng64" \
	CC="$CC" WINDRES="$WINDRES" OBJCOPY="$OBJCOPY" STRIP="$STRIP"

if [ "$DEBUG" = 1 ]; then
	OUT=bin/debug
	DEPLOYDIR="${DEPLOYDIR:-deploy/Debug}"
else
	OUT=bin
	DEPLOYDIR="${DEPLOYDIR:-deploy/Release}"
fi
[ -f "$OUT/srb2win64.exe" ] || { echo "build.sh: expected $OUT/srb2win64.exe, not found" >&2; exit 1; }
echo ">> built     : $OUT/srb2win64.exe"

[ "$DO_DEPLOY" = 1 ] || exit 0

# 4. Stage a directly runnable game folder: exe + runtime DLLs + game data.
echo ">> deploying to $DEPLOYDIR"
mkdir -p "$DEPLOYDIR"

# Clear out the files we manage before restaging. Without this, DLLs left over
# from a different toolchain linger and can be loaded in preference to ours
# (e.g. an MSVC SDL2_mixer.dll alongside our MinGW SDL2_mixer_ext.dll). User
# state -- config, addons/, logs/, luafiles/ -- is deliberately left alone.
rm -f "$DEPLOYDIR"/*.dll "$DEPLOYDIR/SRB2Persona.exe"

cp -f "$OUT/srb2win64.exe" "$DEPLOYDIR/SRB2Persona.exe"

# Runtime DLLs. These are the MinGW builds shipped in libs/ -- the same set
# the official v1.3.6 installer bundled.
DLLS=(
	libs/SDL2/x86_64-w64-mingw32/bin/SDL2.dll
	libs/SDLMixerX/x86_64-w64-mingw32/bin/*.dll
	libs/libopenmpt/bin/x86_64/mingw/libopenmpt.dll
	libs/dll-binaries/x86_64/libgme.dll
	libs/dll-binaries/x86_64/exchndl.dll
	libs/dll-binaries/x86_64/mgwhelp.dll
	libs/curl/lib64/libcurl-x64.dll
)
cp -f "${DLLS[@]}" "$DEPLOYDIR/"

# Game data (assets/installer is the SRB2_ASSET_DIRECTORY the CMake build
# expects, and where the v1.3.6 data files are tracked).
if compgen -G "assets/installer/*.pk3" >/dev/null; then
	cp -f assets/installer/*.pk3 assets/installer/*.wad assets/installer/*.txt "$DEPLOYDIR/"
else
	echo "   WARN: no game data in assets/installer -- the deployed build will not launch"
fi

echo ">> deployed  : $(find "$DEPLOYDIR" -maxdepth 1 -type f | wc -l) files in $DEPLOYDIR"
echo ">> run it    : $DEPLOYDIR/SRB2Persona.exe"
echo ">> done."
