// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1999-2024 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_stereo.c
/// \brief Stereoscopic 3D rendering: SbS, TaB, Anaglyph, Interlaced.
///
/// Mirrors the projection model used by the SRB2 3DS port: per-eye off-axis
/// asymmetric frustum with user-tunable IPD and convergence (focal) plane.
/// Eye state is stashed in module globals and consumed by HWR_SetupView when
/// it builds FTransform for each pass.

#include <math.h>

#include "r_stereo.h"

#include "command.h"
#include "console.h"
#include "doomdef.h"
#include "doomstat.h"
#include "d_player.h"
#include "i_video.h"
#include "p_local.h"
#include "p_maputl.h"
#include "r_defs.h"
#include "r_main.h"
#include "r_state.h"
#include "screen.h"
#include "tables.h"

#ifdef HWRENDER
#include "hardware/hw_main.h"   // HWR_SetStereoMode / HWR_ResetStereoMode
#endif

static CV_PossibleValue_t stereomode_cons_t[] = {
	{STEREO_OFF,               "Off"},
	{STEREO_SBS,               "SideBySide"},
	{STEREO_TAB,               "TopBottom"},
	{STEREO_ANAGLYPH,          "Anaglyph"},
	{STEREO_ROW_INTERLACED,    "RowInterlaced"},
	{STEREO_COLUMN_INTERLACED, "ColumnInterlaced"},
	{STEREO_CHECKERBOARD,      "Checkerboard"},
	{STEREO_LEIASR,            "LeiaSR"},
	{0, NULL}
};

// CVAR storage uses small whole-number "slider units" so the in-game slider
// shows friendly values; the runtime multiplies them up to recover the world
// scale. Choosing the multiplier per-CVAR (×100 for IPD, ×1000 for focal)
// keeps each slider's label range close to its useful tuning band.
//   stereoipd     slider value × 0.1   = world units  (slider 10 = 1.0 wu)
//   stereofoclen  slider value × 1.0   = world units  (slider 100 = 100.0 wu)
//   stereohuddepth slider value × 0.01 = -1..+1 fraction
//
// SRB2 world-unit scale: player ≈32 wu tall (~6 ft), characters ≈50–200 wu
// away, rooms 100s–1000s wu across. The asymptotic disparity is
// iod / (focal × tan(fov/2)), so depth differentiation requires iod that's
// non-trivial relative to the focal distance. A "true human" IPD at SRB2
// scale is ~1.2 wu; for game-style exaggerated stereo, 3–15 wu produces
// strong depth pop. Pair with focal set to typical viewing distance
// (50–500 wu) so closer objects pop forward and farther ones recede.
// Out-of-range values can still be typed at the console — the slider just
// visually clamps.
static CV_PossibleValue_t stereoipd_cons_t[]      = {{10,  "MIN"}, {400,  "MAX"}, {0, NULL}};   // 1.0–10.0 wu (default 6.0 wu sits at ~56%)
static CV_PossibleValue_t stereofoclen_cons_t[]   = {{50,  "MIN"}, {250,  "MAX"}, {0, NULL}};   // 50–150 wu (default 100 wu centered)
static CV_PossibleValue_t stereohuddepth_cons_t[]       = {{-100, "MIN"}, { 50, "MAX"}, {0, NULL}};   // -1.00..+0.50 fraction (HUD biased toward popping out by default)
static CV_PossibleValue_t stereocrosshairdepth_cons_t[] = {{-150, "MIN"}, {  0, "MAX"}, {0, NULL}};   // -1.50..0.00 fraction (crosshair sits at or in front of screen plane)

static void Stereo_OnChange(void);

consvar_t cv_stereomode             = CVAR_INIT("stereomode",             "Off",  CV_SAVE|CV_CALL, stereomode_cons_t,    Stereo_OnChange);
consvar_t cv_stereoipd              = CVAR_INIT("stereoipd",              "60",  CV_SAVE,         stereoipd_cons_t,     NULL);   // ×0.1 → 6.0 wu IPD (~5× human-scale, clear depth pop at default focal)
consvar_t cv_stereofoclen           = CVAR_INIT("stereofoclen",           "100", CV_SAVE,         stereofoclen_cons_t,  NULL);   // ×1.0 → 100.0 wu convergence (typical scene viewing distance)
consvar_t cv_stereoswap             = CVAR_INIT("stereoswap",             "Off",  CV_SAVE,         CV_OnOff,             NULL);
consvar_t cv_stereohuddepth         = CVAR_INIT("stereohuddepth",         "-30",  CV_SAVE,         stereohuddepth_cons_t,       NULL);   // -0.30 fraction — HUD pops slightly forward of screen
consvar_t cv_stereocrosshairdepth   = CVAR_INIT("stereocrosshairdepth",   "-100", CV_SAVE,         stereocrosshairdepth_cons_t, NULL);   // -1.00 fraction — crosshair sits well in front of screen

// current_eye holds the *perspective* eye for the active pass — it tracks
// which eye's view is being rendered (and is what HUD/crosshair shifts and
// the off-axis frustum should follow). When "Swap Eyes" is on, this is the
// opposite of the placement eye that SetStereoMode used to pick the
// viewport / color mask / stencil region.
//
// current_placement_eye holds that placement eye separately so anything
// that needs to re-apply the per-eye GL state mid-pass (e.g. the
// HWR_ClearView / HWR_RenderPlayerView re-applies in d_main.c and
// hw_main.c after a GClipRect) can do so against the original viewport
// region rather than the swapped perspective.
static SINT8    current_eye           = STEREO_EYE_MONO;
static SINT8    current_placement_eye = STEREO_EYE_MONO;
static float   current_iod           = 0.0f;
static float   current_focal         = 1.0f;
static fixed_t cached_crosshair_dist = 0; // populated by R_UpdateStereoCrosshairTrace
static boolean drawing_crosshair_hud = false;
static boolean backbuffer_is_stereo  = false;

static void Stereo_OnChange(void)
{
	// Preserve the user's preference even if the renderer is currently
	// software — R_StereoActive() handles the runtime gate silently. We only
	// warn when both the active renderer AND the saved cv_renderer preference
	// are software, so a config.cfg load that sets cv_renderer=OpenGL doesn't
	// fire a false alarm on the line that processes cv_stereomode (the order
	// of CVAR processing in config.cfg isn't guaranteed).
	if (cv_stereomode.value != STEREO_OFF
		&& rendermode != render_opengl
		&& cv_renderer.value != 2 /* OpenGL */)
	{
		CONS_Alert(CONS_WARNING, "Stereoscopic 3D requires the OpenGL renderer; setting will take effect once OpenGL is selected.\n");
	}
}

void R_RegisterStereoVars(void)
{
	CV_RegisterVar(&cv_stereomode);
	CV_RegisterVar(&cv_stereoipd);
	CV_RegisterVar(&cv_stereofoclen);
	CV_RegisterVar(&cv_stereoswap);
	CV_RegisterVar(&cv_stereohuddepth);
	CV_RegisterVar(&cv_stereocrosshairdepth);
}

boolean R_StereoActive(void)
{
	return (cv_stereomode.value != STEREO_OFF) && (rendermode == render_opengl);
}

stereomode_t R_StereoMode(void)
{
	if (!R_StereoActive())
		return STEREO_OFF;

	// Row-Interlaced: render TaB internally and composite to row-interleaved
	// at display resolution in OglSdlFinishUpdate via a fragment shader.
	// TaB gives each eye full horizontal resolution and half vertical
	// resolution, matching a row-interleaved display's per-eye row count
	// when render_height == display_height — rendered pixels map cleanly
	// to display eye-rows.
	//
	// Splitscreen also works through the same path: the TaB+splitscreen
	// layout (P1L/P2L/P1R/P2R top-to-bottom stripes) already puts both
	// players' L views in the top half of the source texture and both R
	// views in the bottom half, which is exactly what the eye-0/eye-1
	// composite samples — each player's interlaced view ends up in their
	// own half of the display.
	if (cv_stereomode.value == STEREO_ROW_INTERLACED)
		return STEREO_TAB;

	// Column-Interlaced and Checkerboard render SbS internally and
	// composite at display resolution via fragment shaders that pick
	// per-pixel which eye's half to sample (by column parity or
	// (col+row) parity respectively). Same SbS-internal layout as LeiaSR
	// — preserves the per-eye horizontal resolution that matters for
	// column-based displays.
	//
	// Anaglyph also renders SbS internally now: the Dubois optimization
	// requires both eye color values per output pixel, which we get by
	// reading from each half of the SbS source in a composite shader.
	// The old color-mask path (left writes R, right writes GB) can't
	// supply this — each eye only touched its own channels, so the
	// negative cross-eye coefficients in the Dubois matrix had no input
	// data to act on.
	if (cv_stereomode.value == STEREO_COLUMN_INTERLACED
		|| cv_stereomode.value == STEREO_CHECKERBOARD
		|| cv_stereomode.value == STEREO_ANAGLYPH)
		return STEREO_SBS;

	return (stereomode_t)cv_stereomode.value;
}

int R_StereoNumEyes(void)
{
	return R_StereoActive() ? 2 : 1;
}

SINT8 R_StereoEyeForPass(int pass)
{
	if (!R_StereoActive())
		return STEREO_EYE_MONO;

	// Always pass 0 = LEFT placement, pass 1 = RIGHT placement. The "Swap
	// Eyes" CVAR is applied inside R_BeginStereoEye by inverting the iod
	// sign (perspective) without moving the placement — so the physical-
	// screen region chosen by SetStereoMode (left half / red channel / even
	// rows) ends up showing the OPPOSITE eye's view. Inverting both at the
	// same time (the previous behavior) cancelled out and made the toggle a
	// no-op for SbS/TaB/Anaglyph.
	return (pass == 0) ? STEREO_EYE_LEFT : STEREO_EYE_RIGHT;
}

void R_BeginStereoEye(SINT8 eye)
{
	// "Swap Eyes": the placement eye stays as passed in (so SetStereoMode
	// already routed us to the correct half / channel / row). We only flip
	// which perspective gets rendered into that placement, by inverting the
	// effective eye used to derive the iod sign and the HUD-shift state.
	SINT8 perspective_eye = eye;
	if (cv_stereoswap.value && eye != STEREO_EYE_MONO)
		perspective_eye = (eye == STEREO_EYE_LEFT) ? STEREO_EYE_RIGHT : STEREO_EYE_LEFT;

	current_eye           = perspective_eye;
	current_placement_eye = eye;
	// stereofoclen slider value is already in world units (×1.0).
	current_focal = (cv_stereofoclen.value > 0) ? (float)cv_stereofoclen.value : 1.0f;

	if (perspective_eye == STEREO_EYE_MONO)
	{
		current_iod = 0.0f;
	}
	else
	{
		// stereoipd slider value × 0.1 = world units. Half-IPD per eye, signed
		// by eye direction. Matches the off-axis frustum convention where
		// iod > 0 shifts the right eye's frustum.
		const float ipd = cv_stereoipd.value * 0.1f;
		current_iod = (perspective_eye == STEREO_EYE_LEFT) ? -ipd : +ipd;
	}
}

void R_EndStereoEye(void)
{
	current_eye           = STEREO_EYE_MONO;
	current_placement_eye = STEREO_EYE_MONO;
	current_iod           = 0.0f;
	current_focal         = 1.0f;
}

SINT8 R_GetCurrentEye(void)
{
	return current_eye;
}

SINT8 R_GetCurrentPlacementEye(void)
{
	return current_placement_eye;
}

float R_GetStereoIOD(void)
{
	return current_iod;
}

float R_GetStereoFocal(void)
{
	return current_focal;
}

// Convert (iod_world, focal_world) into the per-eye screen-pixel shift
// relative to the mono view. Total inter-ocular disparity (right minus left)
// = 2 × this, so half the formula is intentional: the right eye shifts by
// -shift and the left eye by +shift, summing to the full disparity.
//
//   disparity_px(d→∞) = iod * (vid.width/2) / (focal * tan(fov/2))
//   per_eye_shift_px  = disparity_px / 2
//                     = iod * (vid.width/4) / (focal * tan(fov/2))
static float Stereo_PixelShiftPerEye(float iod_world, float focal_world)
{
	if (focal_world <= 0.0f)
		return 0.0f;

	const float fovrad = (FIXED_TO_FLOAT(cv_fov.value) * (float)M_PIl) / 360.0f;
	const float tan_half = (float)tan((double)fovrad);
	if (tan_half <= 0.0f)
		return 0.0f;

	return iod_world * ((float)vid.width * 0.25f) / (focal_world * tan_half);
}

// Compute the flat chrome-HUD shift (depth-fraction-based). Doesn't consult
// drawing_crosshair_hud, so it's safe to call as a recursion-free fallback
// from R_GetStereoCrosshairShift.
static INT32 R_GetStereoChromeHUDShift_Raw(void)
{
	if (!R_StereoActive() || current_eye == STEREO_EYE_MONO)
		return 0;
	if (cv_stereohuddepth.value == 0 || cv_stereoipd.value == 0)
		return 0;

	const float depth_frac = cv_stereohuddepth.value / 100.0f; // -1..+1
	const float ipd        = cv_stereoipd.value * 0.1f;
	return (INT32)(-current_eye * depth_frac * Stereo_PixelShiftPerEye(ipd, current_focal));
}

INT32 R_GetStereoHUDShift(void)
{
	if (!R_StereoActive() || current_eye == STEREO_EYE_MONO)
		return 0;

	// When the crosshair is being drawn, route to the dynamic-depth shift
	// so the crosshair sits at the world-hit depth instead of the flat
	// chrome-HUD depth.
	if (drawing_crosshair_hud)
		return R_GetStereoCrosshairShift();

	return R_GetStereoChromeHUDShift_Raw();
}

INT32 R_GetStereoCrosshairShift(void)
{
	if (!R_StereoActive() || current_eye == STEREO_EYE_MONO)
		return 0;
	if (cv_stereocrosshairdepth.value == 0 || cv_stereoipd.value == 0)
		return 0;

	// Mirrors the chrome-HUD-shift formula but driven by the separate
	// crosshair-depth CVAR so users can place the crosshair at a different
	// 3D plane than the chrome HUD (e.g. crosshair at typical aim depth,
	// chrome at screen plane).
	const float depth_frac = cv_stereocrosshairdepth.value / 100.0f;
	const float ipd        = cv_stereoipd.value * 0.1f;
	return (INT32)(-current_eye * depth_frac * Stereo_PixelShiftPerEye(ipd, current_focal));
}

void R_UpdateStereoCrosshairTrace(player_t *player)
{
	(void)player;
	// No-op: the dynamic raycast was replaced by the static
	// cv_stereocrosshairdepth CVAR. Kept as a stub so the call site in
	// d_main.c doesn't need conditional compilation.
	cached_crosshair_dist = 0;
}

boolean R_BackbufferIsStereo(void)
{
	return backbuffer_is_stereo;
}

void R_SetBackbufferIsStereo(boolean is_stereo)
{
	backbuffer_is_stereo = is_stereo;
}

void R_BeginCrosshairHUDDraw(void)
{
	drawing_crosshair_hud = true;
}

void R_EndCrosshairHUDDraw(void)
{
	drawing_crosshair_hud = false;
}

void R_StereoComputePlayerEyeRect(stereomode_t mode, SINT8 eye, int player_idx,
                                  INT32 *x, INT32 *y, INT32 *w, INT32 *h)
{
	const INT32 vw         = vid.width;
	const INT32 vh         = vid.height;
	const INT32 vw_half    = vw / 2;
	const INT32 vh_half    = vh / 2;
	const INT32 vh_quarter = vh / 4;
	// player_idx == -1 forces single-player layout (eye half of full
	// screen), regardless of splitscreen state. Used for the top-of-eye-
	// loop overlay setup and the post-render HUD pass — both want the
	// HUD/menu/overlays to span both players' regions in each eye half.
	const boolean is_split = (player_idx >= 0) && splitscreen;
	const boolean is_left  = (eye < 0);
	// In SRB2 conventions, displayplayer (P1) renders into the upper half of
	// the screen — which is the upper viewport rectangle in GL coords (Y is
	// bottom-up). So for splitscreen, P1's vertical region is the upper one.
	const boolean is_p1    = (!is_split || player_idx == 0);

	// LeiaSR shares its viewport layout with SbS — the SR weaver is fed the
	// SbS-composited backbuffer.
	if (mode == STEREO_LEIASR)
		mode = STEREO_SBS;

	switch (mode)
	{
		case STEREO_SBS:
		{
			// Left col (x=0) for L eye, right col (x=vw/2) for R eye.
			*x = is_left ? 0 : vw_half;
			*w = vw_half;
			if (is_split)
			{
				// Top row (GL y=vh/2) for P1, bottom row (GL y=0) for P2.
				*y = is_p1 ? vh_half : 0;
				*h = vh_half;
			}
			else
			{
				*y = 0;
				*h = vh;
			}
			break;
		}
		case STEREO_TAB:
		{
			*x = 0;
			*w = vw;
			if (is_split)
			{
				// 4 horizontal stripes:
				//   GL y=3*vh/4 (top quarter visually): P1 L
				//   GL y=  vh/2 (second quarter):       P2 L
				//   GL y=  vh/4 (third quarter):        P1 R
				//   GL y=     0 (bottom quarter):       P2 R
				if (is_left)
					*y = is_p1 ? (3 * vh_quarter) : vh_half;
				else
					*y = is_p1 ? vh_quarter       : 0;
				*h = vh_quarter;
			}
			else
			{
				// Top half (GL y=vh/2) = L eye, bottom half (GL y=0) = R eye.
				*y = is_left ? vh_half : 0;
				*h = vh_half;
			}
			break;
		}
		case STEREO_ANAGLYPH:
		case STEREO_ROW_INTERLACED:
		case STEREO_COLUMN_INTERLACED:
		case STEREO_CHECKERBOARD:
		{
			// Defensive fallback only — R_StereoMode() substitutes all four
			// of these to STEREO_TAB (Row-Interlaced) or STEREO_SBS (Anaglyph
			// Dubois / Column-Interlaced / Checkerboard) before reaching
			// this function in normal flow, so the SbS/TaB cases above are
			// what actually drives per-eye viewports. Eye separation for
			// these modes happens at present time via composite shaders.
			*x = 0;
			*w = vw;
			if (is_split)
			{
				*y = is_p1 ? vh_half : 0;
				*h = vh_half;
			}
			else
			{
				*y = 0;
				*h = vh;
			}
			break;
		}
		default: // STEREO_OFF
			*x = 0;
			*y = 0;
			*w = vw;
			*h = vh;
			break;
	}
}

void R_DrawAcrossStereoEyes(void (*drawfn)(void))
{
	if (drawfn == NULL)
		return;

#ifdef HWRENDER
	if (R_StereoActive())
	{
		const int npasses = R_StereoNumEyes();
		const stereomode_t mode = R_StereoMode();
		int p;
		// Transient screens (loading, title card, quit) span the whole
		// window, so pass player_idx = -1 to force the single-player
		// layout (eye half of full screen) regardless of whether
		// splitscreen happens to be configured at the time.
		for (p = 0; p < npasses; p++)
		{
			const SINT8 eye = R_StereoEyeForPass(p);
			INT32 rx, ry, rw, rh;
			R_StereoComputePlayerEyeRect(mode, eye, -1, &rx, &ry, &rw, &rh);
			HWR_SetStereoMode((INT32)mode, eye, rx, ry, rw, rh);
			R_BeginStereoEye(eye);
			drawfn();
			R_EndStereoEye();
		}
		HWR_ResetStereoMode();
		R_SetBackbufferIsStereo(true);
		return;
	}
#endif

	drawfn();
	R_SetBackbufferIsStereo(false);
}
