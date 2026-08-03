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
///     void srk_shutdown(void);
///
/// All three are looked up via GetProcAddress; any failure leaves us in a
/// "not available" state and the LeiaSR stereo mode falls back to SbS.

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
typedef void (*PFN_srk_shutdown)(void);

// Lazy-init state machine. Init runs at most once; the outcome is cached
// for the rest of the process lifetime. Re-entry into Init returns the
// cached state instead of trying again.
enum
{
	LEIASR_UNINITIALIZED = 0,
	LEIASR_INIT_DONE,      // We tried; check `available` for the outcome.
	LEIASR_SHUT_DOWN       // R_LeiaSR_Shutdown ran; no further calls.
};

static int                state      = LEIASR_UNINITIALIZED;
static boolean            available  = false;
#ifdef _WIN32
static HMODULE            shim       = NULL;
#else
static void              *shim       = NULL;
#endif
static PFN_srk_init       p_init     = NULL;
static PFN_srk_weave      p_weave    = NULL;
static PFN_srk_shutdown   p_shutdown = NULL;

void R_LeiaSR_Init(void)
{
#ifdef _WIN32
	void *hwnd = NULL;

	if (state != LEIASR_UNINITIALIZED)
		return;

	state = LEIASR_INIT_DONE;

	// Pull the HWND first. If SDL hasn't created the window yet (shouldn't
	// happen at our call site, but defensive), bail out and try again later
	// by leaving state UNINITIALIZED.
	hwnd = I_GetWindowHandle();
	if (!hwnd)
	{
		state = LEIASR_UNINITIALIZED;
		return;
	}

	// LoadLibrary is silent on failure (it just returns NULL) so missing
	// DLL == graceful fall-back, no message to the user. Same goes for the
	// SR runtime DLLs the shim itself links against - if those aren't
	// installed, LoadLibrary("leiasr_shim.dll") will fail with code 126.
	shim = LoadLibraryA("leiasr_shim.dll");
	if (!shim)
		return;

	p_init     = (PFN_srk_init)    (void *)GetProcAddress(shim, "srk_init");
	p_weave    = (PFN_srk_weave)   (void *)GetProcAddress(shim, "srk_weave");
	p_shutdown = (PFN_srk_shutdown)(void *)GetProcAddress(shim, "srk_shutdown");

	if (!p_init || !p_weave || !p_shutdown)
	{
		// Shim DLL present but wrong version / missing exports. Print to the
		// console so a developer notices, but don't make this fatal - users
		// without an SR display are happy with the SbS fall-back.
		CONS_Alert(CONS_WARNING, "leiasr_shim.dll loaded but is missing one of "
			"srk_init/srk_weave/srk_shutdown; LeiaSR disabled.\n");
		FreeLibrary(shim);
		shim = NULL;
		p_init = NULL; p_weave = NULL; p_shutdown = NULL;
		return;
	}

	// Hand the HWND to the shim. Its init wraps SR::SRContext::create() +
	// SR::CreateGLWeaver(...) in try/catch and returns 0 on any failure;
	// the most common cause is "no SR display connected", which is
	// expected on non-Leia hardware.
	if (p_init(hwnd))
	{
		available = true;
		CONS_Printf("LeiaSR weaver initialized.\n");
	}
#endif
}

boolean R_LeiaSR_Available(void)
{
	// Lazy first-use init keeps the loader independent of any specific
	// startup ordering - the present path is welcome to ask whether
	// LeiaSR is up before anyone has explicitly initialized it.
	if (state == LEIASR_UNINITIALIZED)
		R_LeiaSR_Init();

	return available;
}

void R_LeiaSR_Weave(unsigned int tex_id, int width, int height)
{
	if (!available || !p_weave)
		return;

	p_weave(tex_id, width, height);
}

void R_LeiaSR_Shutdown(void)
{
#ifdef _WIN32
	if (state == LEIASR_SHUT_DOWN)
		return;

	if (available && p_shutdown)
		p_shutdown();

	if (shim)
	{
		FreeLibrary(shim);
		shim = NULL;
	}

	p_init = NULL; p_weave = NULL; p_shutdown = NULL;
	available = false;
	state = LEIASR_SHUT_DOWN;
#endif
}
