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
/// not present, R_LeiaSR_Available() returns false and the present path in
/// ogl_sdl.c simply leaves the Side-by-Side frame it already drew on screen.
/// No part of the engine links against the SR SDK.

#ifndef __R_STEREO_LEIASR__
#define __R_STEREO_LEIASR__

#include "doomdef.h"

// Load leiasr_shim.dll and resolve its exports. Idempotent, and cheap: it
// touches no SR API at all, so it is safe to call at any point after startup.
// Bringing the weaver itself up is deliberately NOT done here - see
// R_LeiaSR_Weave.
void R_LeiaSR_Init(void);

// Whether the LeiaSR present path is worth taking this frame: the shim
// resolved, and either the weaver is up or we have not tried yet.
//
// Deliberately not "is weaving". The SR runtime is not touched until the
// first weave (some builds of the SR service re-parent and resize the host
// window as their context comes up, which is worth keeping off the title
// screen), and the present path uses this answer to decide whether to call
// R_LeiaSR_Weave at all - so answering "no, not yet" would mean the bring-up
// never happens. A bring-up that fails makes this false from the next frame
// on, at a cost of one wasted screen capture.
boolean R_LeiaSR_Available(void);

// Hand a side-by-side texture to the SR weaver and have it composite the
// autostereo output into the currently-bound framebuffer at the current
// viewport. Brings the weaver up on the first call; a no-op once that has
// been tried and failed. The caller is responsible for binding the back
// buffer / setting the viewport to the full SDL window beforehand - the
// weaver writes to wherever GL is bound.
void R_LeiaSR_Weave(unsigned int tex_id, int width, int height);

// Ask for the switchable lens, on the panels that have one: lens down and the
// display is autostereoscopic, lens up and it is an ordinary sharp 2D
// monitor. Call every frame with "are we weaving" - the request is cached and
// only reaches the runtime when the answer changes.
//
// A preference rather than a command; the SR service arbitrates it across
// every application with an opinion. Silently does nothing before the weaver
// exists, on a panel with a fixed lens, or against a shim predating the
// export.
void R_LeiaSR_SetLens(boolean on);

// Drop the lens, then tear the weaver and SR context down. Called from I_Quit
// so the SR connection isn't left dangling for the next launch - and so the
// desktop is sharp again the moment the player quits.
void R_LeiaSR_Shutdown(void);

#endif // __R_STEREO_LEIASR__
