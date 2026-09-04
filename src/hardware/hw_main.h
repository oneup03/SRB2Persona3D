// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2022 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file hw_main.h
/// \brief 3D render mode functions

#ifndef __HWR_MAIN_H__
#define __HWR_MAIN_H__

#include "hw_data.h"
#include "hw_defs.h"

#include "../am_map.h"
#include "../d_player.h"
#include "../r_defs.h"

#include "../m_perfstats.h"

// Startup & Shutdown the hardware mode renderer
void HWR_Startup(void);
void HWR_Switch(void);
void HWR_Shutdown(void);

void HWR_drawAMline(const fline_t *fl, INT32 color);
void HWR_FadeScreenMenuBack(UINT16 color, UINT8 strength);
void HWR_DrawConsoleBack(UINT32 color, INT32 height);
void HWR_DrawTutorialBack(UINT32 color, INT32 boxheight);
void HWR_RenderSkyboxView(INT32 viewnumber, player_t *player);
void HWR_RenderPlayerView(INT32 viewnumber, player_t *player);
void HWR_ClearSkyDome(void);
void HWR_BuildSkyDome(void);
void HWR_DrawViewBorder(INT32 clearlines);
void HWR_DrawFlatFill(INT32 x, INT32 y, INT32 w, INT32 h, lumpnum_t flatlumpnum);
void HWR_InitTextureMapping(void);
void HWR_SetViewSize(void);
void HWR_DrawIndexPatch(patch_t *gpatch, fixed_t x, fixed_t y, fixed_t pscale, fixed_t vscale, INT32 option, INT32 c);
void HWR_DrawPatch(patch_t *gpatch, INT32 x, INT32 y, INT32 option);
void HWR_DrawStretchyFixedPatch(patch_t *gpatch, fixed_t x, fixed_t y, fixed_t pscale, fixed_t vscale, INT32 option, const UINT8 *colormap);
void HWR_DrawCroppedPatch(patch_t *gpatch, fixed_t x, fixed_t y, fixed_t pscale, fixed_t vscale, INT32 option, const UINT8 *colormap, fixed_t sx, fixed_t sy, fixed_t w, fixed_t h);
void HWR_MakePatch(const patch_t *patch, GLPatch_t *grPatch, GLMipmap_t *grMipmap, boolean makebitmap);
void HWR_CreatePlanePolygons(INT32 bspnum);
void HWR_CreateStaticLightmaps(INT32 bspnum);
void HWR_DrawFill(INT32 x, INT32 y, INT32 w, INT32 h, INT32 color);
void HWR_DrawFadeFill(INT32 x, INT32 y, INT32 w, INT32 h, INT32 color, UINT16 actualcolor, UINT8 strength);
void HWR_DrawConsoleFill(INT32 x, INT32 y, INT32 w, INT32 h, INT32 color, UINT32 actualcolor);	// Lat: separate flags from color since color needs to be an uint to work right.
void HWR_DrawPic(INT32 x,INT32 y,lumpnum_t lumpnum);

UINT8 *HWR_GetScreenshot(void);
boolean HWR_Screenshot(const char *pathname);

void HWR_AddCommands(void);
void HWR_AddSessionCommands(void);
void transform(float *cx, float *cy, float *cz);
INT32 HWR_GetTextureUsed(void);
void HWR_DoPostProcessor(player_t *player);
void HWR_StartScreenWipe(void);
void HWR_EndScreenWipe(void);
void HWR_DrawIntermissionBG(void);
void HWR_DoWipe(UINT8 wipenum, UINT8 scrnnum);
void HWR_DoTintedWipe(UINT8 wipenum, UINT8 scrnnum);
void HWR_MakeScreenFinalTexture(void);
void HWR_DrawScreenFinalTexture(int width, int height, boolean stretch);

// ==========================================================================
//                                                       STEREOSCOPIC 3D
// ==========================================================================
// Thin pass-throughs for the stereo driver hooks. Anything that includes
// r_opengl.h sets _CREATE_DLL_, which hides HWD, so callers like ogl_sdl.c
// and r_stereo.c have to drive per-eye state through these wrappers.

void HWR_SetStereoMode(INT32 mode, INT32 eye, INT32 x, INT32 y, INT32 w, INT32 h);
void HWR_ResetStereoMode(void);

// One unscissored full-screen colour clear. The per-view clear inside
// HWR_RenderPlayerView is scissor-gated and so is skipped in stereo (it
// would wipe the other eye's half); D_Display calls this once per frame
// before the eye loop instead.
void HWR_ClearFrameBuffer(void);

// Captures the current framebuffer into the "screen snapshot" sampled by
// HWR_DrawIntermissionBG and the underwater/heat wave. d_main.c calls this
// once after the stereo eye loop completes so the snapshot contains BOTH
// eyes' fully-painted halves; the per-player capture inside
// HWR_DoPostProcessor otherwise fires before each eye's HUD pass and would
// leave the snapshot's second half missing its HUD (visible as "intermission
// BG asymmetric, lives/rings only show in one eye").
void HWR_MakeScreenTexture(void);

// Capture the framebuffer at exact (width, height) into the tightly-fitted
// NPOT LeiaSR slot. Used when the rendered backbuffer is smaller than the
// SDL window -- the caller stretches the rendered content to fill first,
// then captures the stretched output.
void   HWR_MakeScreenLeiaTextureSized(INT32 width, INT32 height);
UINT32 HWR_GetScreenLeiaTextureID(void);

// Set the GL viewport to (0, 0, width, height). Used by the LeiaSR present
// path right before R_LeiaSR_Weave so the weaver writes to the full SDL
// window instead of the engine's render rectangle (which may be smaller).
void HWR_SetPresentViewport(INT32 width, INT32 height);

// Composite the captured stereo frame into a display format via a fragment
// shader. shader_target selects the composite kind:
//   SHADER_ROW_INTERLACED_COMPOSITE     -- TaB source -> row-interleaved
//   SHADER_COLUMN_INTERLACED_COMPOSITE  -- SbS source -> column-interleaved
//   SHADER_CHECKERBOARD_COMPOSITE       -- SbS source -> checkerboard
//   SHADER_ANAGLYPH_DUBOIS_COMPOSITE    -- SbS source -> red/cyan Dubois
//   SHADER_STEREO_GHOST_COMPOSITE       -- passthrough; carries the ghost
//                                          reduction for SbS / TaB / LeiaSR,
//                                          which have no composite of their
//                                          own to fold it into
void HWR_DrawStereoComposite(INT32 shader_target, INT32 width, INT32 height);
// Push the ghost/crosstalk reduction levers down to the composite shaders.
// Call before HWR_DrawStereoComposite; (1.0f, 0.0f) is the exact no-op.
void HWR_SetStereoGhostReduction(float contrast, float lift);

// This stuff is put here so models can use them
void HWR_Lighting(FSurfaceInfo *Surface, INT32 light_level, extracolormap_t *colormap);
UINT8 HWR_FogBlockAlpha(INT32 light, extracolormap_t *colormap); // Let's see if this can work

UINT8 HWR_GetTranstableAlpha(INT32 transtablenum);
FBITFIELD HWR_GetBlendModeFlag(INT32 style);
FBITFIELD HWR_SurfaceBlend(INT32 style, INT32 transtablenum, FSurfaceInfo *pSurf);
FBITFIELD HWR_TranstableToAlpha(INT32 transtablenum, FSurfaceInfo *pSurf);

boolean HWR_CompileShaders(void);

void HWR_LoadAllCustomShaders(void);
void HWR_LoadCustomShadersFromFile(UINT16 wadnum, boolean PK3);
const char *HWR_GetShaderName(INT32 shader);

extern customshaderxlat_t shaderxlat[];

extern CV_PossibleValue_t glanisotropicmode_cons_t[];

#ifdef ALAM_LIGHTING
extern consvar_t cv_gldynamiclighting;
extern consvar_t cv_glstaticlighting;
extern consvar_t cv_glcoronas;
extern consvar_t cv_glcoronasize;
#endif

extern consvar_t cv_glshaders, cv_glallowshaders;
extern consvar_t cv_glmodels;
extern consvar_t cv_glmodelinterpolation;
extern consvar_t cv_glmodellighting;
extern consvar_t cv_glfiltermode;
extern consvar_t cv_glanisotropicmode;
extern consvar_t cv_fovchange;
extern consvar_t cv_glsolvetjoin;
extern consvar_t cv_glshearing;
extern consvar_t cv_glspritebillboarding;
extern consvar_t cv_glskydome;
extern consvar_t cv_glfakecontrast;
extern consvar_t cv_glslopecontrast;

extern consvar_t cv_glbatching;

extern float gl_viewwidth, gl_viewheight, gl_baseviewwindowy;

extern float gl_viewwindowx, gl_basewindowcentery;

// BP: big hack for a test in lighting ref : 1249753487AB
extern fixed_t *hwbbox;
extern FTransform atransform;


// Render stats
extern ps_metric_t ps_hw_skyboxtime;
extern ps_metric_t ps_hw_nodesorttime;
extern ps_metric_t ps_hw_nodedrawtime;
extern ps_metric_t ps_hw_spritesorttime;
extern ps_metric_t ps_hw_spritedrawtime;

// Render stats for batching
extern ps_metric_t ps_hw_numpolys;
extern ps_metric_t ps_hw_numverts;
extern ps_metric_t ps_hw_numcalls;
extern ps_metric_t ps_hw_numshaders;
extern ps_metric_t ps_hw_numtextures;
extern ps_metric_t ps_hw_numpolyflags;
extern ps_metric_t ps_hw_numcolors;
extern ps_metric_t ps_hw_batchsorttime;
extern ps_metric_t ps_hw_batchdrawtime;

extern boolean gl_init;
extern boolean gl_maploaded;
extern boolean gl_maptexturesloaded;
extern boolean gl_sessioncommandsadded;
extern boolean gl_shadersavailable;

#endif
