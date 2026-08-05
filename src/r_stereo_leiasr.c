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
/// \file  r_stereo_leiasr.c
/// \brief LeiaSR shim DLL loader - pure C, runtime-bound via LoadLibrary.
///
/// Sits in front of leiasr_shim.dll (built separately with MSVC; see
/// leiasr_shim/ at repo root) so the main MinGW64 game never references SR
/// SDK symbols at link time. The shim exports a flat C ABI:
///
///     int  srk_init(void *hwnd);          // 1 = ready, 0 = unavailable
///     void srk_weave(unsigned tex, int w, int h);
///     int  srk_lens(int enable);          // optional; see below
///     void srk_shutdown(void);
///
/// The first three (init/weave/shutdown) are required; a shim missing any of
/// them is treated as no shim at all. srk_lens arrived later, and an older
/// leiasr_shim.dll sitting next to a newer build is a thing that happens, so
/// it is resolved optionally: a shim without it is a shim that cannot express
/// the lens preference, which is exactly how a panel with a fixed lens
/// behaves anyway.
///
/// Any failure along the way leaves us in a "not available" state and the
/// LeiaSR stereo mode falls back to SbS.

#include "r_stereo_leiasr.h"
#include "console.h"
#include "i_system.h"
#include "i_video.h"   // I_GetWindowHandle

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// Shim entrypoint signatures (must match leiasr_shim/leiasr_shim.cpp exports).
typedef int  (*PFN_srk_init)(void *hwnd);
typedef void (*PFN_srk_weave)(unsigned int tex_id, int width, int height);
typedef int  (*PFN_srk_lens)(int enable);
typedef void (*PFN_srk_shutdown)(void);

// Two-stage state machine, and the split is deliberate.
//
// LOADED means leiasr_shim.dll is in the process and its exports resolved.
// That costs a LoadLibrary and three GetProcAddress calls and touches no SR
// API whatsoever, so it is safe to do the moment anyone asks.
//
// LIVE means srk_init ran and the SR runtime brought a weaver up. That is the
// expensive, visible half: some builds of the SR service briefly re-parent and
// resize the host window while the context comes up, and doing that behind the
// title screen is a black flash and a minimise-restore cycle for no reason. So
// it is deferred to the first actual weave, by which point there is already a
// frame on screen to be interrupted -- a much better moment to interrupt.
enum
{
	LEIASR_UNINITIALIZED = 0,
	LEIASR_LOADED,         // Shim resolved; check `failed` for the outcome.
	LEIASR_SHUT_DOWN       // R_LeiaSR_Shutdown ran; no further calls.
};

static int                state      = LEIASR_UNINITIALIZED;
static boolean            failed     = false; // shim absent, or bring-up lost
static boolean            live       = false; // srk_init reported a weaver
static boolean            brought_up = false; // ...and we have tried, once
static boolean            lens_on    = false; // what we last asked the lens to do
#ifdef _WIN32
static HMODULE            shim       = NULL;
#else
static void              *shim       = NULL;
#endif
static PFN_srk_init       p_init     = NULL;
static PFN_srk_weave      p_weave    = NULL;
static PFN_srk_lens       p_lens     = NULL;
static PFN_srk_shutdown   p_shutdown = NULL;

void R_LeiaSR_Init(void)
{
	if (state != LEIASR_UNINITIALIZED)
		return;

	state = LEIASR_LOADED;

#ifdef _WIN32
	// LoadLibrary is silent on failure (it just returns NULL) so missing
	// DLL == graceful fall-back, no message to the user. The shim delay-loads
	// every SR DLL it links against, so this succeeds even on a machine with
	// no SR runtime installed at all - that case surfaces later, as srk_init
	// returning 0.
	shim = LoadLibraryA("leiasr_shim.dll");
	if (!shim)
	{
		failed = true;
		return;
	}

	p_init     = (PFN_srk_init)    (void *)GetProcAddress(shim, "srk_init");
	p_weave    = (PFN_srk_weave)   (void *)GetProcAddress(shim, "srk_weave");
	p_shutdown = (PFN_srk_shutdown)(void *)GetProcAddress(shim, "srk_shutdown");
	// Optional, and absence is not an error: see the header comment.
	p_lens     = (PFN_srk_lens)    (void *)GetProcAddress(shim, "srk_lens");

	if (!p_init || !p_weave || !p_shutdown)
	{
		// Shim DLL present but wrong version / missing exports. Print to the
		// console so a developer notices, but don't make this fatal - users
		// without an SR display are happy with the SbS fall-back.
		CONS_Alert(CONS_WARNING, "leiasr_shim.dll loaded but is missing one of "
			"srk_init/srk_weave/srk_shutdown; LeiaSR disabled.\n");
		FreeLibrary(shim);
		shim = NULL;
		p_init = NULL; p_weave = NULL; p_lens = NULL; p_shutdown = NULL;
		failed = true;
		return;
	}
#else
	// Simulated Reality is a Windows runtime and the shim is a Win32 DLL, so
	// everywhere else there is nothing to load and LeiaSR mode is SbS.
	failed = true;
#endif
}

// Bring the weaver up, once. Deferred out of R_LeiaSR_Init for the reason
// documented on the state enum; called from the first weave that wants it.
static boolean R_LeiaSR_BringUp(void)
{
#ifdef _WIN32
	void *hwnd;

	if (brought_up)
		return live;
	if (state != LEIASR_LOADED || failed || !p_init)
		return false;

	// If SDL hasn't created the window yet (shouldn't happen at our call
	// site, but defensive), stay un-brought-up and try again next frame
	// rather than burning the one attempt we get.
	hwnd = I_GetWindowHandle();
	if (!hwnd)
		return false;

	brought_up = true;

	// srk_init wraps the SR context and weaver creation in a LoadLibraryW
	// preflight plus SEH and returns 0 on any failure; the most common cause
	// is "no SR display connected", which is expected on non-Leia hardware.
	if (!p_init(hwnd))
	{
		failed = true;
		return false;
	}

	live = true;
	CONS_Printf("LeiaSR weaver initialized.\n");
	return true;
#else
	return false;
#endif
}

boolean R_LeiaSR_Available(void)
{
	// Lazy first-use load keeps the loader independent of any specific
	// startup ordering - the present path is welcome to ask whether LeiaSR
	// is up before anyone has explicitly initialized it.
	//
	// Note this answers "could plausibly weave", not "is weaving": before the
	// first weave the SR runtime has deliberately not been touched yet. It
	// has to work this way round, because the present path uses this answer
	// to decide whether to call R_LeiaSR_Weave at all, and the weave is what
	// triggers the bring-up. A bring-up that then fails flips `failed`, so
	// this goes false from the next frame on and the SbS fall-back takes over
	// having cost one wasted screen capture.
	if (state == LEIASR_UNINITIALIZED)
		R_LeiaSR_Init();

	return !failed && (live || !brought_up);
}

void R_LeiaSR_Weave(unsigned int tex_id, int width, int height)
{
	if (!R_LeiaSR_BringUp())
		return;

	p_weave(tex_id, width, height);
}

void R_LeiaSR_SetLens(boolean on)
{
	// A preference, not a command: the SR service arbitrates the lens across
	// every connected application and it stays down while anybody still wants
	// it. Worth asking even so, because the failure it prevents is one the
	// player cannot diagnose - a panel left lenticular after the stereo mode
	// is switched off is a soft, faintly doubled desktop with nothing on
	// screen to connect it to.
	//
	// Nothing to ask with before the weaver exists, and nothing to ask on a
	// shim that predates the export. Both are silent: the first self-corrects
	// on the next frame, the second is indistinguishable from a panel whose
	// lens does not move.
	if (!live || !p_lens)
		return;
	if (on == lens_on)
		return;

	p_lens(on ? 1 : 0);
	lens_on = on;
}

void R_LeiaSR_Shutdown(void)
{
#ifdef _WIN32
	if (state == LEIASR_SHUT_DOWN)
		return;

	// Hand the lens back before the weaver goes; after srk_shutdown there is
	// no context left to ask with.
	R_LeiaSR_SetLens(false);

	if (live && p_shutdown)
		p_shutdown();

	if (shim)
	{
		FreeLibrary(shim);
		shim = NULL;
	}

	p_init = NULL; p_weave = NULL; p_lens = NULL; p_shutdown = NULL;
	live = false;
	failed = true;
	state = LEIASR_SHUT_DOWN;
#endif
}
