// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1999-2024 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_stereo.h
/// \brief Stereoscopic 3D rendering: SbS, TaB, Anaglyph, Interlaced.

#ifndef __R_STEREO__
#define __R_STEREO__

#include "command.h"
#include "doomtype.h"
#include "d_player.h"

typedef enum
{
	STEREO_OFF = 0,
	STEREO_SBS,                 // 1 - Side-by-Side (left half / right half)
	STEREO_TAB,                 // 2 - Top-and-Bottom (top half / bottom half)
	STEREO_ANAGLYPH,            // 3 - Red-cyan anaglyph (color-mask split)
	STEREO_ROW_INTERLACED,      // 4 - Row-interleaved (TaB internal + display-res shader composite)
	STEREO_LEIASR,              // 5 - SbS rendered then woven for Leia/SR autostereoscopic display
	STEREO_COLUMN_INTERLACED,   // 6 - Column-interleaved (SbS internal + shader composite by column parity)
	STEREO_CHECKERBOARD,        // 7 - Checkerboard 3D (SbS internal + shader composite by (col+row) parity)
	NUM_STEREO_MODES
} stereomode_t;

// Eye identifier values used as the per-pass offset sign.
#define STEREO_EYE_MONO   ( 0)
#define STEREO_EYE_LEFT   (-1)
#define STEREO_EYE_RIGHT  (+1)

extern consvar_t cv_stereomode;
extern consvar_t cv_stereoipd;
extern consvar_t cv_stereofoclen;
extern consvar_t cv_stereoswap;
extern consvar_t cv_stereohuddepth;
extern consvar_t cv_stereocrosshairdepth;

// Register all stereo CVARs with the console. Call from R_RegisterEngineStuff.
void R_RegisterStereoVars(void);

// Returns true if stereo rendering is currently active (mode != Off and the
// active renderer supports it). The render loop uses this to decide whether
// to do a single-pass or two-pass render.
boolean R_StereoActive(void);

// Current stereo display mode (Off when stereo is disabled or unsupported).
stereomode_t R_StereoMode(void);

// How many eye passes to render this frame: 1 when mono, 2 when stereo.
int R_StereoNumEyes(void);

// Returns the eye sign (LEFT/RIGHT) for pass index 0..R_StereoNumEyes()-1,
// honoring cv_stereoswap. For mono, pass 0 returns STEREO_EYE_MONO.
SINT8 R_StereoEyeForPass(int pass);

// Per-pass setup/teardown. Begin updates the global eye state used by
// HWR_SetupView and crosshair parallax; End restores GL state.
void R_BeginStereoEye(SINT8 eye);
void R_EndStereoEye(void);

// Accessors used by HWR_SetupView to populate FTransform.
SINT8  R_GetCurrentEye(void);                  // -1, 0, +1 — perspective eye
                                              // (HUD/crosshair shifts and the off-axis
                                              // frustum follow this; honors "Swap Eyes")
float R_GetStereoIOD(void);                   // signed eye separation for current eye
float R_GetStereoFocal(void);                 // convergence-plane distance

// Placement eye for the active pass — the original sign passed to
// R_BeginStereoEye, before "Swap Eyes" inverts the perspective. Use this
// (not R_GetCurrentEye) when re-applying SetStereoMode mid-pass so the
// viewport / color mask / stencil region returns to the same physical-
// screen region the eye loop assigned to this pass.
SINT8  R_GetCurrentPlacementEye(void);

// HUD parallax helpers. Returns the X-pixel offset to apply to chrome HUD
// elements during the current eye pass. Mono returns 0.
INT32 R_GetStereoHUDShift(void);

// Crosshair parallax. Per-frame raycast result is cached; this returns the
// X-pixel offset to apply to the crosshair during the current eye pass.
INT32 R_GetStereoCrosshairShift(void);

// (Kept as a no-op stub — the dynamic crosshair raycast was replaced by a
// separate user-adjustable cv_stereocrosshairdepth CVAR.)
void R_UpdateStereoCrosshairTrace(player_t *player);

// True when the current backbuffer contents were rendered through the stereo
// eye loop (i.e. already SbS / per-eye). False after a non-D_Display draw
// (loading screen, console flush during init, etc.). Set by D_Display at the
// end of its eye loop; OglSdlFinishUpdate uses it to decide whether to apply
// a "stereo presentation" pass to mono content.
extern boolean R_BackbufferIsStereo(void);
void R_SetBackbufferIsStereo(boolean is_stereo);

// Bracket the crosshair draw so the HUD-shift helper switches over to the
// dynamic-depth (ray-traced) parallax instead of the flat chrome-HUD depth.
// Idempotent and cheap when stereo is off.
void R_BeginCrosshairHUDDraw(void);
void R_EndCrosshairHUDDraw(void);

// Compute the GL viewport rect (bottom-up Y) for the given (mode, eye,
// player) combination, accounting for splitscreen. Layout choices:
//   SbS / LeiaSR + splitscreen: 2x2 quadrants — left col = L eye, right
//     col = R eye, top row = P1, bottom row = P2.
//   TaB + splitscreen: 4 horizontal stripes — top half = L eye (P1 above
//     P2), bottom half = R eye (P1 above P2). Stereo glasses see both
//     players' L views in the top half, both R views in the bottom half.
//   Anaglyph / Interlaced + splitscreen: per-player full-width half (the
//     per-pixel mask handles eye separation within each player's region).
//   Single-player: SbS=eye half horizontally, TaB=eye half vertically,
//     Anaglyph/Interlaced=full screen.
// player_idx is 0 (P1), 1 (P2), or -1 (force single-player layout — eye
// half of full screen, ignoring actual splitscreen state; used for the
// top-of-eye-loop overlay setup and the post-render HUD pass).
// x/y/w/h are output (GL coords, bottom-up Y).
void R_StereoComputePlayerEyeRect(stereomode_t mode, SINT8 eye, int player_idx,
                                  INT32 *x, INT32 *y, INT32 *w, INT32 *h);

// Run drawfn once per stereo eye, with the matching SetStereoMode +
// BeginStereoEye state already applied. Mirrors the per-eye loop in
// D_Display so transient screens drawn outside the main display path
// (loading screens, T-junction-solving progress, etc.) end up in stereo
// instead of as a single mono draw stretched across both eye halves.
// Mono callers see a single drawfn() invocation. drawfn must be re-entrant
// since stereo runs it twice; pure rendering functions (CON_Drawer,
// ST_preLevelTitleCardDrawer) are safe — anything that mutates game state
// per call is not.
void R_DrawAcrossStereoEyes(void (*drawfn)(void));

#endif
