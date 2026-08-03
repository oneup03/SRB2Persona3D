// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2022 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file hw_drv.h
/// \brief imports/exports for the 3D hardware low-level interface API

#ifndef __HWR_DRV_H__
#define __HWR_DRV_H__

// this must be here 19991024 by Kin
#include "../screen.h"
#include "hw_data.h"
#include "hw_defs.h"
#include "hw_md2.h"

#include "hw_dll.h"

// ==========================================================================
//                                                       STANDARD DLL EXPORTS
// ==========================================================================

EXPORT boolean HWRAPI(Init) (void);
#ifndef HAVE_SDL
EXPORT void HWRAPI(Shutdown) (void);
#endif
#ifdef _WINDOWS
EXPORT void HWRAPI(GetModeList) (vmode_t **pvidmodes, INT32 *numvidmodes);
#endif
EXPORT void HWRAPI(SetPalette) (RGBA_t *ppal);
EXPORT void HWRAPI(FinishUpdate) (INT32 waitvbl);
EXPORT void HWRAPI(Draw2DLine) (F2DCoord *v1, F2DCoord *v2, RGBA_t Color);
EXPORT void HWRAPI(DrawPolygon) (FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags);
EXPORT void HWRAPI(DrawIndexedTriangles) (FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags, UINT32 *IndexArray);
EXPORT void HWRAPI(RenderSkyDome) (gl_sky_t *sky);
EXPORT void HWRAPI(SetBlend) (FBITFIELD PolyFlags);
EXPORT void HWRAPI(ClearBuffer) (FBOOLEAN ColorMask, FBOOLEAN DepthMask, FRGBAFloat *ClearColor);
EXPORT void HWRAPI(SetTexture) (GLMipmap_t *TexInfo);
EXPORT void HWRAPI(UpdateTexture) (GLMipmap_t *TexInfo);
EXPORT void HWRAPI(DeleteTexture) (GLMipmap_t *TexInfo);
EXPORT void HWRAPI(ReadRect) (INT32 x, INT32 y, INT32 width, INT32 height, INT32 dst_stride, UINT16 *dst_data);
EXPORT void HWRAPI(GClipRect) (INT32 minx, INT32 miny, INT32 maxx, INT32 maxy, float nearclip);
EXPORT void HWRAPI(ClearMipMapCache) (void);

//Hurdler: added for backward compatibility
EXPORT void HWRAPI(SetSpecialState) (hwdspecialstate_t IdState, INT32 Value);

//Hurdler: added for new development
EXPORT void HWRAPI(DrawModel) (model_t *model, INT32 frameIndex, INT32 duration, INT32 tics, INT32 nextFrameIndex, FTransform *pos, float scale, UINT8 flipped, UINT8 hflipped, FSurfaceInfo *Surface);
EXPORT void HWRAPI(CreateModelVBOs) (model_t *model);
EXPORT void HWRAPI(SetTransform) (FTransform *ptransform);
EXPORT INT32 HWRAPI(GetTextureUsed) (void);

EXPORT void HWRAPI(FlushScreenTextures) (void);
EXPORT void HWRAPI(StartScreenWipe) (void);
EXPORT void HWRAPI(EndScreenWipe) (void);
EXPORT void HWRAPI(DoScreenWipe) (void);
EXPORT void HWRAPI(DrawIntermissionBG) (void);
EXPORT void HWRAPI(MakeScreenTexture) (void);
EXPORT void HWRAPI(MakeScreenFinalTexture) (void);
// stretch=true fills the (width, height) viewport without aspect
// preservation. Used by stereo present paths (full-SbS displays presenting
// as 3840x1080, LeiaSR weaver input, interlaced composite source) where
// black bars would break the stereo signal pipeline. stretch=false aspect-
// preserves the rendered backbuffer inside the viewport -- used for mono
// rendering so the early-startup BASE 320x200 loading window doesn't get
// stretched across the full desktop.
EXPORT void HWRAPI(DrawScreenFinalTexture) (int width, int height, boolean stretch);

// ==========================================================================
//                                                       STEREOSCOPIC 3D
// ==========================================================================

// Returns the OpenGL texture ID of the LeiaSR capture slot, so the weaver
// bridge can hand the side-by-side capture to the SR runtime.
EXPORT UINT32 HWRAPI(GetLeiaTextureID)(void);

// Capture the backbuffer into a tightly-fitted NPOT texture of exactly
// (width, height) pixels, recreating it when the dimensions change. The SR
// weaver samples [0,1] across its input and would otherwise see padding, so
// this can't reuse the power-of-two screen texture.
EXPORT void HWRAPI(MakeScreenTextureSized)(INT32 width, INT32 height);

// Stereoscopic 3D mode setup. mode is the stereomode_t enum from r_stereo.h,
// eye is -1 (left) / +1 (right). The (x, y, w, h) rect is the EXACT viewport
// region the eye+player render should occupy -- d_main.c computes this via
// R_StereoComputePlayerEyeRect so callers only need to pass it through.
// SetStereoMode applies viewport+scissor to the rect; Anaglyph (Dubois),
// Row/Column-Interlaced and Checkerboard all render SbS/TaB internally and
// composite at present time via a fragment shader (see ogl_sdl.c), so this
// entrypoint never touches the color mask or stencil per-eye.
EXPORT void HWRAPI(SetStereoMode)(INT32 mode, INT32 eye,
                                  INT32 x, INT32 y, INT32 w, INT32 h);
// Re-applies the cached SetStereoMode state. HWR_ClearView calls this
// after GClipRect overwrites the viewport so subsequent geometry stays
// inside the per-eye region without needing to recompute the rect.
EXPORT void HWRAPI(ReapplyStereoMode)(void);
// Restores full viewport and color mask to mono defaults.
EXPORT void HWRAPI(ResetStereoMode)(void);
// Composite a TaB/SbS-rendered source texture into a stereo display format
// using the currently-bound composite fragment shader (set by the caller
// via HWR_DrawStereoComposite). Caller has captured the source texture at
// (width, height) -- typically after a stretch step.
EXPORT void HWRAPI(DrawInterlacedComposite)(INT32 width, INT32 height);
// Set the GL viewport to (0, 0, width, height). Used by the LeiaSR present
// path to give the SR weaver the full-SDL-window viewport as its output
// region before calling weave() -- the weaver writes into the currently
// bound viewport, and after the eye loop / ResetStereoMode the viewport is
// still at the engine's render size (which may be smaller than the window).
EXPORT void HWRAPI(SetPresentViewport)(INT32 width, INT32 height);

#define SCREENVERTS 10
EXPORT void HWRAPI(PostImgRedraw) (float points[SCREENVERTS][SCREENVERTS][2]);

EXPORT boolean HWRAPI(CompileShaders) (void);
EXPORT void HWRAPI(CleanShaders) (void);
EXPORT void HWRAPI(SetShader) (int type);
EXPORT void HWRAPI(UnSetShader) (void);

EXPORT void HWRAPI(SetShaderInfo) (hwdshaderinfo_t info, INT32 value);
EXPORT void HWRAPI(LoadCustomShader) (int number, char *code, size_t size, boolean isfragment);

// ==========================================================================
//                                      HWR DRIVER OBJECT, FOR CLIENT PROGRAM
// ==========================================================================

#if !defined (_CREATE_DLL_)

struct hwdriver_s
{
	Init                pfnInit;
	SetPalette          pfnSetPalette;
	FinishUpdate        pfnFinishUpdate;
	Draw2DLine          pfnDraw2DLine;
	DrawPolygon         pfnDrawPolygon;
	DrawIndexedTriangles    pfnDrawIndexedTriangles;
	RenderSkyDome       pfnRenderSkyDome;
	SetBlend            pfnSetBlend;
	ClearBuffer         pfnClearBuffer;
	SetTexture          pfnSetTexture;
	UpdateTexture       pfnUpdateTexture;
	DeleteTexture       pfnDeleteTexture;
	ReadRect            pfnReadRect;
	GClipRect           pfnGClipRect;
	ClearMipMapCache    pfnClearMipMapCache;
	SetSpecialState     pfnSetSpecialState;//Hurdler: added for backward compatibility
	DrawModel           pfnDrawModel;
	CreateModelVBOs     pfnCreateModelVBOs;
	SetTransform        pfnSetTransform;
	GetTextureUsed      pfnGetTextureUsed;
#ifdef _WINDOWS
	GetModeList         pfnGetModeList;
#endif
#ifndef HAVE_SDL
	Shutdown            pfnShutdown;
#endif
	PostImgRedraw       pfnPostImgRedraw;
	FlushScreenTextures pfnFlushScreenTextures;
	StartScreenWipe     pfnStartScreenWipe;
	EndScreenWipe       pfnEndScreenWipe;
	DoScreenWipe        pfnDoScreenWipe;
	DrawIntermissionBG  pfnDrawIntermissionBG;
	MakeScreenTexture   pfnMakeScreenTexture;
	MakeScreenFinalTexture  pfnMakeScreenFinalTexture;
	DrawScreenFinalTexture  pfnDrawScreenFinalTexture;

	CompileShaders      pfnCompileShaders;
	CleanShaders        pfnCleanShaders;
	SetShader           pfnSetShader;
	UnSetShader         pfnUnSetShader;

	SetShaderInfo       pfnSetShaderInfo;
	LoadCustomShader    pfnLoadCustomShader;

	GetLeiaTextureID    pfnGetLeiaTextureID;
	MakeScreenTextureSized pfnMakeScreenTextureSized;
	SetStereoMode       pfnSetStereoMode;
	ReapplyStereoMode   pfnReapplyStereoMode;
	ResetStereoMode     pfnResetStereoMode;
	DrawInterlacedComposite pfnDrawInterlacedComposite;
	SetPresentViewport  pfnSetPresentViewport;
};

extern struct hwdriver_s hwdriver;

#define HWD hwdriver

#endif //not defined _CREATE_DLL_

#endif //__HWR_DRV_H__

