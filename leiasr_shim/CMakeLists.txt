# Build the LeiaSR shim DLL (leiasr_shim.dll) - an MSVC-built bridge between
# the MinGW64-built SRB2 Persona engine and the LeiaSR Simulated Reality
# OpenGL weaver. The engine loads this DLL at runtime via LoadLibrary; absence
# is graceful (LeiaSR mode falls back to plain Side-by-Side).
#
# This is deliberately NOT wired into src/Makefile: the game is GCC and this
# is MSVC. build.sh drives it, or build it by hand:
#
#   cd leiasr_shim
#   cmake -B build -A x64
#   cmake --build build --config Release
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
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
cmake_policy(SET CMP0091 NEW)

# The SR SDK is vendored via the libs/SR-lib submodule (bo3b/SR-lib), which
# nests per-architecture SDK trees. Note this points at the x64 subdirectory,
# not the submodule root: that's where include/ and lib/cmake/ actually live.
set(SRLIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../libs/SR-lib"
    CACHE PATH "bo3b/SR-lib checkout root")
if(NOT DEFINED LEIASR_SDKROOT)
    set(LEIASR_SDKROOT "${SRLIB_ROOT}/SR-SDK-1.34.10/x64"
        CACHE PATH "Simulated Reality SDK root (architecture subdirectory)")
endif()

message(STATUS "leiasr_shim: SRLIB_ROOT     = ${SRLIB_ROOT}")
message(STATUS "leiasr_shim: LEIASR_SDKROOT = ${LEIASR_SDKROOT}")

if(NOT EXISTS "${SRLIB_ROOT}/SR.cpp")
    message(FATAL_ERROR
        "SR-lib not found at ${SRLIB_ROOT}. Did you forget "
        "`git submodule update --init libs/SR-lib`?")
endif()
if(NOT EXISTS "${LEIASR_SDKROOT}/lib/cmake")
    message(FATAL_ERROR
        "LEIASR_SDKROOT does not contain lib/cmake; expected the SDK layout "
        "from libs/SR-lib/SR-SDK-1.34.10/x64/.")
endif()

# Deliberately NOT find_package(simulatedreality)/find_package(srOpenGL).
# The SDK's own CMake configs declare SHARED IMPORTED targets whose import
# check demands bin/*.dll alongside the .lib, and SR-lib vendors only the
# import libraries -- the runtime DLLs come from the user's SR installation.
# find_package therefore fails with "the installation package was faulty".
# Since every SR DLL is delay-loaded below, the import libs are all we need
# at link time anyway.
set(SR_LIBDIR "${LEIASR_SDKROOT}/lib")
set(SR_LIBS
    "${SR_LIBDIR}/simulatedreality.lib"
    "${SR_LIBDIR}/SimulatedRealityCore.lib"
    "${SR_LIBDIR}/SimulatedRealityOpenGL.lib"
    "${SR_LIBDIR}/SimulatedRealityDirectX.lib"
    "${SR_LIBDIR}/SimulatedRealityDisplays.lib"
    "${SR_LIBDIR}/DimencoWeaving.lib"
    # SR's headers instantiate cvflann templates over cv::String, which pulls
    # in opencv_core at link time. The x64 SDK ships OpenCV as modular libs
    # (the single opencv_world343 exists only in the x32 tree).
    "${LEIASR_SDKROOT}/third_party/OpenCV/lib/x64/opencv_core343.lib"
)
foreach(_lib IN LISTS SR_LIBS)
    if(NOT EXISTS "${_lib}")
        message(FATAL_ERROR "Missing SR import library: ${_lib}")
    endif()
endforeach()

# SR.cpp is SR-lib's wrapper: it owns the SRContext lifetime and exposes the
# COM-ish CreateSRInterfaceOGL / SetInputTexture / Weave / Delete surface that
# leiasr_shim.cpp is written against. Compiling it in (rather than linking
# SR-lib's own static .lib) keeps this a single self-contained MSVC target.
add_library(leiasr_shim SHARED
    leiasr_shim.cpp
    "${SRLIB_ROOT}/SR.cpp"
)

# The SR headers include <opencv2/opencv.hpp> (sr/sense/core/transformation.h)
# and glog, so the vendored third_party include trees have to be on the path
# too -- they aren't pulled in implicitly without the SDK's CMake targets.
target_include_directories(leiasr_shim PRIVATE
    "${SRLIB_ROOT}"
    "${LEIASR_SDKROOT}/include"
    "${LEIASR_SDKROOT}/third_party/OpenCV/include"
    "${LEIASR_SDKROOT}/third_party/GLog/include"
    "${LEIASR_SDKROOT}/third_party/Leap/include"
    "${LEIASR_SDKROOT}/third_party/asio/include"
    "${LEIASR_SDKROOT}/third_party/rapidxml/include"
)

# SR.cpp calls LoadLibrary(L"...") with wide literals, so it needs the Unicode
# mapping of the TCHAR APIs. CMake defaults MSVC targets to MBCS, under which
# LoadLibrary resolves to LoadLibraryA and those calls fail to compile.
target_compile_definitions(leiasr_shim PRIVATE UNICODE _UNICODE)

target_link_libraries(leiasr_shim PRIVATE
    ${SR_LIBS}
    opengl32
)

# extern "C" __declspec(dllexport) is enough for the flat C ABI we want, so no
# .def file: the exported names are exactly srk_init / srk_weave /
# srk_shutdown, which is what r_stereo_leiasr.c GetProcAddresses.
set_target_properties(leiasr_shim PROPERTIES
    OUTPUT_NAME "leiasr_shim"
    PREFIX ""
)

if(MSVC)
    target_compile_options(leiasr_shim PRIVATE /W3 /wd4251 /wd4275)

    # Delay-load every SR / DimencoWeaving / third-party runtime DLL the shim
    # depends on, directly or transitively, so leiasr_shim.dll itself still
    # loads on a machine with no SR runtime installed. SR-lib's header calls
    # this out explicitly: it's a static library with the SR import libs
    # merged in, so without delay-loading the consumer gets a hard load-time
    # dependency and the process dies before any of our probing runs.
    #
    # leiasr_shim.cpp then converts a delay-load failure into a clean
    # "unavailable" return (LoadLibraryW preflight plus SEH), which the engine
    # reads as "fall back to SbS".
    target_link_options(leiasr_shim PRIVATE
        "/DELAYLOAD:SimulatedReality.dll"
        "/DELAYLOAD:SimulatedRealityCore.dll"
        "/DELAYLOAD:SimulatedRealityCameras.dll"
        "/DELAYLOAD:SimulatedRealityDirectX.dll"
        "/DELAYLOAD:SimulatedRealityDisplays.dll"
        "/DELAYLOAD:SimulatedRealityFaceTrackers.dll"
        "/DELAYLOAD:SimulatedRealityHandTrackers.dll"
        "/DELAYLOAD:SimulatedRealityOpenGL.dll"
        "/DELAYLOAD:SimulatedRealityUserModelers.dll"
        "/DELAYLOAD:DimencoWeaving.dll"
        "/DELAYLOAD:opencv_core343.dll"
        "/DELAYLOAD:opencv_world343.dll"
        "/DELAYLOAD:glog.dll"
        "/DELAYLOAD:LeapC.dll"
        # 4199 fires for /DELAYLOAD entries that turn out not to be in the
        # import table (e.g. an SR sub-DLL that's only transitive here).
        "/IGNORE:4199"
    )
    target_link_libraries(leiasr_shim PRIVATE delayimp.lib)
endif()
