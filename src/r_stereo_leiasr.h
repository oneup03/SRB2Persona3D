// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2018 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_stereo_leiasr.h
/// \brief LeiaSR autostereoscopic 3D - runtime DLL loader for the MSVC shim
///
/// The LeiaSR ("Simulated Reality") SDK ships as MSVC import libraries which
/// can't link into this MinGW64 build, and its high-level GLWeaver uses a C++
/// class with virtual inheritance, so we can't paper over it with a C wrapper
/// at link time either. Phase 5 of the stereo plan therefore goes through a
/// small MSVC-built shim DLL (leiasr_shim.dll) that exposes a flat C ABI; this
/// header is the engine-side, pure-C runtime loader for it.
///
/// If the shim DLL is absent, fails to load, or the SR runtime/hardware is
/// not present, R_LeiaSR_Available() returns false and the LeiaSR stereo mode
/// degrades to plain Side-by-Side via R_StereoMode() in r_stereo.c. No part
/// of the engine links against the SR SDK.

#ifndef __R_STEREO_LEIASR__
#define __R_STEREO_LEIASR__

#include "doomdef.h"

// Attempt to load leiasr_shim.dll and bring up the SR weaver against the
// game's window. Idempotent: returns immediately if init was already
// attempted (success or failure is cached). Safe to call before the GL
// context exists - the actual weaver creation happens lazily inside the
// shim's init function which I_GetWindowHandle() supplies the HWND to.
void R_LeiaSR_Init(void);

// True iff the shim DLL was loaded AND its init reported success (SR
// runtime alive, an SR display is connected, weaver created cleanly).
// Drives the R_StereoMode() fall-back from STEREO_LEIASR to STEREO_SBS
// when the runtime isn't there, and the present-path branch in ogl_sdl.c.
boolean R_LeiaSR_Available(void);

// Hand a side-by-side texture to the SR weaver and have it composite the
// autostereo output into the currently-bound framebuffer at the current
// viewport. No-op when R_LeiaSR_Available() is false. The caller is
// responsible for binding the back buffer / setting the viewport to the
// full SDL window beforehand - the weaver writes to wherever GL is bound.
void R_LeiaSR_Weave(unsigned int tex_id, int width, int height);

// Tear the weaver and SR context down. Called from I_Quit so the SR
// connection isn't left dangling for the next launch.
void R_LeiaSR_Shutdown(void);

#endif // __R_STEREO_LEIASR__
