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
/// Per-eye off-axis asymmetric frustum, parameterized in CLIP SPACE: the user
/// knob is the horizontal shear the projection applies, not a physical eye
/// separation in world units. The shear IS the setting --
///
///     projection[2][0] += separation
///
/// -- and its magnitude is the total background disparity as a fraction of the
/// screen width, which is the same quantity that bounds it (disparity wider
/// than the viewer's IPD forces the eyes to diverge). That makes the slider a
/// number the player can actually see on their own display, and it makes the
/// whole rig FoV-independent by construction: no reference-FoV slider, no
/// tan(fov/2) auto-scale, no EMA settling wrongly for a few frames after a
/// hard FoV change. Convergence stops being coupled to it too -- it now only
/// moves what sits in front of the screen plane, and the physical eye
/// baseline is DERIVED per frame (2 * separation * tan(fov/2) * convergence)
/// rather than stored.
///
/// This replaced an eye-separation-in-world-units parameterization; the two
/// are algebraically identical, the old shear expanding to
/// ipd / (2 * focal * tan(fov/2)) -- a constant, which is exactly the
/// clip-space separation.
///
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
// shows friendly values; the runtime scales them down to the real quantity.
//   stereosep            slider x 0.001 = clip-space separation (slider 30 = 0.030)
//   stereofoclen         slider x 1.0   = convergence, world units
//   stereohuddepth       slider x 0.01  = depth fraction, NEGATED (below)
//   stereoghostcontrast  slider x 0.01  = 0.50..1.00 contrast multiplier
//   stereoghostlift      slider x 0.001 = 0.000..0.100 black floor
//
// Two sign conventions meet at cv_stereohuddepth, so keep them straight.
//
// INTERNALLY (Stereo_DepthFracForDistance, Stereo_ShiftPixelsForFrac and the
// projection itself) a depth fraction of 0 is the screen plane, NEGATIVE is
// further back (-1.0 == optical infinity) and POSITIVE is out toward the
// viewer. That falls out of frac = convergence/z - 1 and is not a choice.
//
// THE SLIDER IS THE OPPOSITE, because the internal sense reads backwards to a
// player: dragging right should push the HUD further away, not pull it into
// their face. So the CVAR is negated on the way in -- higher is deeper, +100
// is optical infinity, 0 is the screen plane, and the small negative tail pops
// the HUD out. Stereo_HudDepthFrac is the single place that conversion
// happens; nothing else should read cv_stereohuddepth directly.
//
// Separation is a screen-width fraction, so it means the same thing at every
// resolution, FoV and world scale: 0.030 puts objects at infinity 3% of the
// screen width apart, 0.050 puts them 5% apart. The hard ceiling is the
// viewer's IPD over their screen width - about 0.105 on a 27" 16:9 panel, and
// less on anything smaller - past which the eyes have to turn outward and no
// amount of comfort tuning helps. 0.150 is the slider max so the range covers
// large TVs and projectors; on a monitor the useful band is well under half
// of that. Convergence is still in SRB2's world units (player ~32 wu tall,
// characters ~50-200 wu away), and unlike the old parameterization it now
// only decides what pops OUT of the screen - the background depth is
// separation's job alone, so pushing the convergence plane back no longer
// flattens the whole image.
// Out-of-range values can still be typed at the console - the slider just
// visually clamps.
static CV_PossibleValue_t stereosep_cons_t[]            = {{0,    "MIN"}, {150, "MAX"}, {0, NULL}};   // 0.000-0.150 screen-width fraction (default 0.030)
static CV_PossibleValue_t stereofoclen_cons_t[]         = {{50,   "MIN"}, {250, "MAX"}, {0, NULL}};   // 50-250 wu (default 100 wu centered)
static CV_PossibleValue_t stereohuddepth_cons_t[]       = {{-50,  "MIN"}, {100, "MAX"}, {0, NULL}};   // -0.50 (pops out to half the convergence distance) .. +1.00 (optical infinity)
static CV_PossibleValue_t stereoghostcontrast_cons_t[]  = {{50,   "MIN"}, {100, "MAX"}, {0, NULL}};   // 0.50-1.00 (100 = off)
static CV_PossibleValue_t stereoghostlift_cons_t[]      = {{0,    "MIN"}, {100, "MAX"}, {0, NULL}};   // 0.000-0.100 (0 = off)

static void Stereo_OnChange(void);

consvar_t cv_stereomode             = CVAR_INIT("stereomode",             "Off",  CV_SAVE|CV_CALL, stereomode_cons_t,    Stereo_OnChange);
consvar_t cv_stereosep              = CVAR_INIT("stereosep",              "30",   CV_SAVE,         stereosep_cons_t,     NULL);   // x0.001 -> 0.030 of the screen width at infinity
consvar_t cv_stereofoclen           = CVAR_INIT("stereofoclen",           "100",  CV_SAVE,         stereofoclen_cons_t,  NULL);   // x1.0 -> 100.0 wu convergence (typical scene viewing distance)
consvar_t cv_stereoswap             = CVAR_INIT("stereoswap",             "Off",  CV_SAVE,         CV_OnOff,             NULL);
consvar_t cv_stereohuddepth         = CVAR_INIT("stereohuddepth",         "30",   CV_SAVE,         stereohuddepth_cons_t,       NULL);   // 0.30 deep - HUD sits a little way behind the screen plane (~1.4x convergence)
consvar_t cv_stereoghostcontrast    = CVAR_INIT("stereoghostcontrast",    "100",  CV_SAVE,         stereoghostcontrast_cons_t,  NULL);   // x0.01 -> 1.00, i.e. off
consvar_t cv_stereoghostlift        = CVAR_INIT("stereoghostlift",        "0",    CV_SAVE,         stereoghostlift_cons_t,      NULL);   // x0.001 -> 0.000, i.e. off

// current_eye holds the *perspective* eye for the active pass — it tracks
// which eye's view is being rendered (and is what the HUD shift and
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
// Signed clip-space separation for the active pass: the projection's [2][0]
// shear coefficient, ready to use as-is. See the sign note above
// Stereo_EyeDir.
static float   current_separation    = 0.0f;
static float   current_convergence   = 1.0f;
static boolean backbuffer_is_stereo  = false;
static boolean stereo_render_in_progress = false;

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
	CV_RegisterVar(&cv_stereosep);
	CV_RegisterVar(&cv_stereofoclen);
	CV_RegisterVar(&cv_stereoswap);
	CV_RegisterVar(&cv_stereohuddepth);
	CV_RegisterVar(&cv_stereoghostcontrast);
	CV_RegisterVar(&cv_stereoghostlift);
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
	// Eyes" CVAR is applied inside R_BeginStereoEye by inverting the
	// separation sign (perspective) without moving the placement — so the
	// physical-screen region chosen by SetStereoMode (left half / red channel
	// / even rows) ends up showing the OPPOSITE eye's view. Inverting both at the
	// same time (the previous behavior) cancelled out and made the toggle a
	// no-op for SbS/TaB/Anaglyph.
	return (pass == 0) ? STEREO_EYE_LEFT : STEREO_EYE_RIGHT;
}

// Per-eye direction for BOTH the projection shear and every screen-space
// shift derived from it.
//
// A codebase with an off-axis rig usually has two independent per-eye sign
// switches -- one for which way the frustum bends, one for which way a
// composited overlay slides -- and they are not guaranteed to agree, because
// they answer different questions. Here they do agree, and the algebra says
// why: the rig puts a point at view-space depth z at an NDC x-offset of
//
//     ndc_dx = -eye * separation * (convergence/z - 1)
//
// away from the mono projection, which is the same -eye the shear carries. So
// one signed value drives both, and current_separation below is that value.
// Do not carry the assumption to another renderer without re-deriving it.
//
// Verify each direction separately if the projection is ever reworked:
//  * shear: an object nearer than the convergence plane must show CROSSED
//    disparity (left-eye image displaced to the RIGHT of the right-eye one).
//  * overlay: with a world-anchored HUD element, moving the object from far
//    to near must slide the element the same way the geometry it labels goes.
static float Stereo_EyeDir(void)
{
	return (float)(-current_eye);
}

void R_BeginStereoEye(SINT8 eye)
{
	// "Swap Eyes": the placement eye stays as passed in (so SetStereoMode
	// already routed us to the correct half / channel / row). We only flip
	// which perspective gets rendered into that placement, by inverting the
	// effective eye used to derive the separation sign and the HUD-shift
	// state.
	SINT8 perspective_eye = eye;
	if (cv_stereoswap.value && eye != STEREO_EYE_MONO)
		perspective_eye = (eye == STEREO_EYE_LEFT) ? STEREO_EYE_RIGHT : STEREO_EYE_LEFT;

	current_eye           = perspective_eye;
	current_placement_eye = eye;
	// stereofoclen slider value is already in world units (x1.0).
	current_convergence = (cv_stereofoclen.value > 0) ? (float)cv_stereofoclen.value : 1.0f;

	if (perspective_eye == STEREO_EYE_MONO)
	{
		current_separation = 0.0f;
	}
	else
	{
		// stereosep slider value x 0.001 = the clip-space separation, which
		// IS the projection's shear coefficient once signed. Nothing here
		// depends on FoV, convergence or world scale -- that independence is
		// the whole point of the parameterization.
		current_separation = Stereo_EyeDir() * (cv_stereosep.value * 0.001f);
	}
}

void R_EndStereoEye(void)
{
	current_eye           = STEREO_EYE_MONO;
	current_placement_eye = STEREO_EYE_MONO;
	current_separation    = 0.0f;
	current_convergence   = 1.0f;
}

SINT8 R_GetCurrentEye(void)
{
	return current_eye;
}

SINT8 R_GetCurrentPlacementEye(void)
{
	return current_placement_eye;
}

float R_GetStereoSeparation(void)
{
	return current_separation;
}

float R_GetStereoConvergence(void)
{
	return current_convergence;
}

float R_GetStereoGhostContrast(void)
{
	if (!R_StereoActive())
		return 1.0f;   // exact no-op

	return cv_stereoghostcontrast.value * 0.01f;
}

float R_GetStereoGhostLift(void)
{
	if (!R_StereoActive())
		return 0.0f;   // exact no-op

	return cv_stereoghostlift.value * 0.001f;
}

boolean R_StereoGhostReductionActive(void)
{
	if (!R_StereoActive())
		return false;

	// Compare against the slider units, not the scaled floats, so "off" is
	// an exact integer test and the untouched present path stays bit-exact.
	return (cv_stereoghostcontrast.value != 100) || (cv_stereoghostlift.value != 0);
}

// Per-eye screen-pixel shift for an element at depth fraction `frac`, i.e.
// the screen-space twin of what the projection shear does to world geometry:
//
//   shift_px = dir * separation * (convergence/z - 1) * eye_w / 2
//            = current_separation * frac * eye_w / 2
//
// with dir already folded into current_separation. No FoV term and no unit
// conversion survive the clip-space parameterization -- compare the old form,
// which needed both, and needed the FoV it used here to match the one the
// projection used, something splitscreen quietly broke (the world got SRB2's
// 0.8 vertical-FoV fudge, this did not). Now neither cares.
//
// eye_w is vid.width rather than the eye viewport's own width because the
// caller converts through R_StereoBaseOffsetFromPixels, which divides by
// vid.width to reach the BASE 320-wide space that maps across whatever
// viewport the eye was given. The two conventions have to agree, and they do.
//
// The clamp is a safety net against a bad depth, and it is deliberately
// ASYMMETRIC. Behind the screen plane the constraint is physical: uncrossed
// disparity past the viewer's IPD forces the eyes to diverge and cannot be
// fused at any comfort setting, so ~0.05 eye-widths per eye is the real
// ceiling. In front the eyes converge inward instead, there is no divergence
// to protect against, and reusing the same limit would just clip valid
// pop-out -- an element nearer than convergence/(1 + 2*limit/separation)
// would stop tracking depth entirely and sit at a fixed offset, which reads
// as "the HUD came unstuck from the enemy" rather than as clipping.
//
// Branch on `frac`, never on the sign of the shift: the eye direction is
// folded into current_separation, so that sign says which EYE, not
// near-versus-far, and it flips between passes while the near/far sense does
// not.
//
// The behind limit also has to yield to the scene itself. frac is bounded
// below by -1 for any depth in front of the camera, so the deepest an element
// can legitimately go is separation/2 per eye -- exactly what the background
// is already doing. Below separation 0.10 the fixed limit is therefore
// unreachable, and above it the fixed limit would clamp perfectly legitimate
// values, pulling an at-infinity overlay FORWARD of the sky behind it. Taking
// whichever is larger keeps the guard for a garbage depth while never second-
// guessing a separation the player chose.
#define STEREO_SHIFT_LIMIT_NEAR   0.15f   // pop-out, crossed disparity
#define STEREO_SHIFT_LIMIT_BEHIND 0.05f   // ~IPD / screen width
static float Stereo_ShiftPixelsForFrac(float frac)
{
	const float eye_w = (float)vid.width;
	const float at_infinity = fabsf(current_separation) * 0.5f;
	float limit, shift;

	if (eye_w <= 0.0f)
		return 0.0f;

	if (frac > 0.0f)
		limit = STEREO_SHIFT_LIMIT_NEAR;
	else
		limit = (at_infinity > STEREO_SHIFT_LIMIT_BEHIND) ? at_infinity : STEREO_SHIFT_LIMIT_BEHIND;

	shift = current_separation * frac * 0.5f;   // as a fraction of eye_w

	if (shift > limit)
		shift = limit;
	else if (shift < -limit)
		shift = -limit;

	return shift * eye_w;
}

// cv_stereohuddepth in the INTERNAL depth-fraction sense: negated, because
// the slider runs the other way round (see the convention note up top). Every
// read of that CVAR goes through here.
static float Stereo_HudDepthFrac(void)
{
	return -cv_stereohuddepth.value / 100.0f;
}

INT32 R_GetStereoHUDShift(void)
{
	float px;

	if (!R_StereoActive() || current_eye == STEREO_EYE_MONO)
		return 0;
	if (cv_stereohuddepth.value == 0 || cv_stereosep.value == 0)
		return 0;

	px = Stereo_ShiftPixelsForFrac(Stereo_HudDepthFrac());
	return (INT32)px;
}

// Internal depth fraction that puts an element at world distance `z` along
// the view axis -- the same quantity Stereo_HudDepthFrac produces for the HUD,
// so the two are directly comparable.
//
// Derivation. For a point at view-space depth z the off-axis rig in
// GLPerspectiveStereo produces an NDC x-offset from the mono projection of
//
//   ndc_dx = -eye * separation * (convergence/z - 1)
//
// (Both halves of the rig contribute: the frustum shear gives the constant
// term, the eye translate gives the 1/z term. Note this is independent of the
// point's screen x, so one scalar offset is exact for anything drawn at that
// depth, anywhere on screen.)
//
// A flat element at depth fraction `frac` gets -eye * separation * frac, so
// equating the two:
//
//   frac = convergence/z - 1
//
// which sanity-checks at the endpoints: z == convergence gives 0 (screen
// plane) and z == infinity gives -1 (optical infinity). Confirms "lower is
// deeper". Unclamped here -- Stereo_ShiftPixelsForFrac owns the safety limit,
// because a limit only means something once it is expressed as disparity.
static float Stereo_DepthFracForDistance(fixed_t viewdist)
{
	const float z = FIXED_TO_FLOAT(viewdist);

	if (z <= 0.0f)   // at or behind the eye - caller shouldn't be drawing anyway
		return 0.0f;

	return (current_convergence / z) - 1.0f;
}

fixed_t R_StereoBaseOffsetFromPixels(INT32 px)
{
	if (px == 0 || vid.width <= 0)
		return 0;

	return (fixed_t)(((INT64)px * BASEVIDWIDTH * FRACUNIT) / vid.width);
}

// Parallax for world-anchored HUD elements — the battle UI's targeting
// reticles, floating HP bars, weakness/block/repel markers, damage numbers and
// turn-order digits. These are 2D patches, but they're positioned by projecting
// a mobj's world position to screen coords, so at the flat chrome-HUD depth
// they visibly detach from the enemy they label.
//
// Returns the extra x offset, in BASE (320-wide) fixed-point coords, to add to
// such an element's position. It is NET of the chrome-HUD shift that
// V_StereoHUDOffset will separately apply inside every V_Draw* call — the
// caller adds this on top and the two together land the element at world depth.
fixed_t R_GetStereoWorldHUDOffset(fixed_t viewdist)
{
	float shift;
	INT32 px;

	if (!R_StereoActive() || current_eye == STEREO_EYE_MONO)
		return 0;
	if (cv_stereosep.value == 0)
		return 0;

	// Net of the flat chrome-HUD shift V_StereoHUDOffset applies separately,
	// so the two together land the element at world depth. Each half goes
	// through the clamp on its own, which is what keeps the safety limit a
	// limit on where the element ENDS UP rather than on the correction.
	shift = Stereo_ShiftPixelsForFrac(Stereo_DepthFracForDistance(viewdist))
	      - Stereo_ShiftPixelsForFrac(Stereo_HudDepthFrac());
	px = (INT32)shift;

	return R_StereoBaseOffsetFromPixels(px);
}

// D_Display brackets its whole eye loop with this so NetUpdate can stand down
// for the duration. See R_StereoRenderInProgress in r_stereo.h for why.
void R_SetStereoRenderInProgress(boolean in_progress)
{
	stereo_render_in_progress = in_progress;
}

boolean R_StereoRenderInProgress(void)
{
	return stereo_render_in_progress;
}

boolean R_BackbufferIsStereo(void)
{
	return backbuffer_is_stereo;
}

void R_SetBackbufferIsStereo(boolean is_stereo)
{
	backbuffer_is_stereo = is_stereo;
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
