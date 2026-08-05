# Build the LeiaSR shim DLL (leiasr_shim.dll) - an MSVC-built bridge between
# the MinGW64-built SRB2 Persona engine and the LeiaSR Simulated Reality
# OpenGL weaver. The engine loads this DLL at runtime via LoadLibrary; absence
# is graceful (LeiaSR mode falls back to plain Side-by-Side).
#
# This is deliberately NOT wired into src/Makefile: the game is GCC and this
# is MSVC. build.sh drives it, or build it by hand:
#
#   git submodule update --init libs/SR-lib
#   cmake -S leiasr_shim -B leiasr_shim/build -A x64
#   cmake --build leiasr_shim/build --config Release
#
# Output lands in build/Release/leiasr_shim.dll and gets staged next to
# SRB2Persona.exe by build.sh's DLL-staging block.

cmake_minimum_required(VERSION 3.21)
project(leiasr_shim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT. The engine is a MinGW build and ships no MSVC runtime, so a
# /MD shim would drag VCRUNTIME140(_1).dll in as a hard dependency and fail
# to load on machines without the VC++ redistributable -- silently costing
# LeiaSR support. /MT makes the shim a genuine drop-in single file.
#
# Set BEFORE add_subdirectory so SR-lib's own static target inherits it; a /MT
# shim linking an /MD SR.cpp is two CRTs in one module, and a link error at
# best.
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------- the SDK
#
# The SR SDK is vendored through the libs/SR-lib submodule (bo3b/SR-lib, the
# api_expansion branch), which ships a CMake package whose entire purpose is
# that consumers stop rewriting the same forty lines. It locates the nested
# per-architecture SDK tree itself -- globbing SR-SDK-* so a version bump does
# not break us, and picking x64/x32 off the pointer size -- names every import
# library, adds the third_party include trees, and owns the delay-load list.
#
# This file used to do all of that by hand, and the reason it stopped is not
# tidiness. The hand-written list omitted SimulatedRealityFaceTrackers.lib,
# which SR-lib now needs for its polled head/eye tracking, so it cannot build
# the current SR.cpp at all -- the link fails with unresolved externals across
# HeadTracker, HeadPoseTracker, EyeTracker and their streams. A dependency
# list maintained in the consumer is a list that goes stale the first time the
# dependency grows, and it takes the whole LeiaSR path down with it when it
# does.
#
# It also carried a wrong belief worth recording, since it reads plausibly:
# that the x64 tree ships OpenCV only as modular libraries and has no
# opencv_world343. It has one, and the SR runtime installs opencv_world343.dll
# and nothing else from OpenCV -- so opencv_world343 is the only OpenCV name
# that can ever resolve at runtime, and delay-loading opencv_core343 named a
# DLL that is never present. In practice this shim imports no OpenCV symbol
# either way, so nothing was visibly broken; the wrong name simply sat there
# waiting for a future SDK to inline one more cv::String destructor.
set(SRLIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../libs/SR-lib"
    CACHE PATH "bo3b/SR-lib checkout root")

if(NOT EXISTS "${SRLIB_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "SR-lib not found at ${SRLIB_ROOT}, or it is checked out on a branch "
        "without the CMake package. Try:\n"
        "  git submodule update --init libs/SR-lib\n"
        "and check that .gitmodules still says `branch = api_expansion`.")
endif()

# Out-of-tree source dir, so the binary dir is not optional.
add_subdirectory("${SRLIB_ROOT}" "${CMAKE_BINARY_DIR}/SR-lib")

if(NOT SRLIB_FOUND)
    message(FATAL_ERROR
        "SR-lib found but reported no usable SDK (SRLIB_FOUND is false). It "
        "prints the reason above -- usually a submodule checked out without "
        "its SR-SDK-* payload, or a non-Windows host.")
endif()

# ------------------------------------------------------------- the target
add_library(leiasr_shim SHARED leiasr_shim.cpp)

# SRLib::SR is the SDK plus SR.cpp's SRInterface* wrapper, which is what
# leiasr_shim.cpp is written against: CreateSRInterfaceOGL does the context
# setup, weaver creation, latency and late-latching configuration, and it
# derives the input texture's size and sRGB-ness from the GL texture object
# itself. It also gets the ORDER right -- SRContext::create, then
# CreateGLWeaver, and only THEN context->initialize(). Reversed, every call
# still returns success and the weave still runs; it simply stops tracking the
# viewer's eyes, which on a screenshot is invisible and on a real panel is the
# whole feature.
target_link_libraries(leiasr_shim PRIVATE SRLib::SR opengl32)

# extern "C" __declspec(dllexport) is enough for the flat C ABI we want, so no
# .def file: the exported names are exactly srk_init / srk_weave / srk_lens /
# srk_shutdown, which is what r_stereo_leiasr.c GetProcAddresses.
set_target_properties(leiasr_shim PROPERTIES
    OUTPUT_NAME "leiasr_shim"
    PREFIX ""
)

if(MSVC)
    target_compile_options(leiasr_shim PRIVATE /W3 /wd4251 /wd4275)

    # Delay-load every SR / DimencoWeaving / OpenCV runtime DLL the shim
    # imports, directly or transitively, so leiasr_shim.dll itself still loads
    # on a machine with no SR runtime installed. SR-lib's header calls this
    # out: it is a static library with the SR import libs merged in, so
    # without delay-loading the consumer gets a hard load-time dependency and
    # the process dies before any of our probing runs. Delay-load cannot be
    # baked into a static lib, which is why this is applied here, to the final
    # module, rather than inside SR-lib.
    #
    # leiasr_shim.cpp then converts a delay-load failure into a clean
    # "unavailable" return (LoadLibraryW preflight plus SEH), which the engine
    # reads as "fall back to SbS".
    srlib_apply_delayload(leiasr_shim)
endif()
