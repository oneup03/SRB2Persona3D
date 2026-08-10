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
// HWR_SetupView and the HUD parallax; End restores GL state.
void R_BeginStereoEye(SINT8 eye);
void R_EndStereoEye(void);

// Accessors used by HWR_SetupView to populate FTransform.
SINT8  R_GetCurrentEye(void);                  // -1, 0, +1 — perspective eye
                                              // (the HUD shift and the off-axis
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
//
// Depth-fraction sign convention, shared by cv_stereohuddepth and
// R_GetStereoWorldHUDOffset: 0 is the screen plane, NEGATIVE is further
// into the screen (-1.0 == optical infinity),
// POSITIVE is out toward the viewer. Lower is deeper.
INT32 R_GetStereoHUDShift(void);

// Convert a screen-pixel X offset into BASE (320-wide) fixed-point coords, the
// space almost everything above the HWR_ layer draws in. Shared by
// V_StereoHUDOffset and the world-depth helper below so the conversion only
// exists once.
fixed_t R_StereoBaseOffsetFromPixels(INT32 px);

// Parallax for world-anchored HUD elements: 2D patches whose screen position
// was derived by projecting a mobj's world position (battle targeting
// reticles, floating HP bars, weakness markers, damage numbers). Pass the
// distance from the eye to the object ALONG THE VIEW AXIS (i.e. radial
// distance times cos of the angle off-centre) and add the result to the
// element's x.
//
// The value is net of the flat chrome-HUD shift that V_Draw* applies on its
// own, so callers add it on top rather than replacing anything.
fixed_t R_GetStereoWorldHUDOffset(fixed_t viewdist);

// True while D_Display is inside its per-eye loop.
//
// NetUpdate honours this and returns immediately. It is called several times
// from deep inside HWR_RenderPlayerView to keep the network alive during a slow
// frame, and the eye loop runs that whole path once per eye -- so without this
// it fires about twice as often, and worse, it can run BETWEEN the two eye
// passes. NetUpdate isn't passive: Local_Maketic builds a ticcmd (advancing
// local aiming) and GetPackets applies inbound state. Either one landing
// mid-loop makes the two eyes render subtly different game states, which shows
// up as jitter or a camera that disagrees between eyes.
//
// Nothing is lost by deferring: NetUpdate derives realtics from wall-clock
// against a static gametime it only advances when it actually runs, so the
// skipped time is picked up by the next call rather than dropped.
boolean R_StereoRenderInProgress(void);
void R_SetStereoRenderInProgress(boolean in_progress);

// True when the current backbuffer contents were rendered through the stereo
// eye loop (i.e. already SbS / per-eye). False after a non-D_Display draw
// (loading screen, console flush during init, etc.). Set by D_Display at the
// end of its eye loop; OglSdlFinishUpdate uses it to decide whether to apply
// a "stereo presentation" pass to mono content.
extern boolean R_BackbufferIsStereo(void);
void R_SetBackbufferIsStereo(boolean is_stereo);

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
