// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 2014-2022 by Sonic Team Junior.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
//-----------------------------------------------------------------------------
/// \file
/// \brief SDL specific part of the OpenGL API for SRB2

#ifdef _MSC_VER
#pragma warning(disable : 4214 4244)
#endif

#ifdef HAVE_SDL
#define _MATH_DEFINES_DEFINED

#include "SDL.h"

#include "sdlmain.h"

#ifdef _MSC_VER
#pragma warning(default : 4214 4244)
#endif

#include "../doomdef.h"

#ifdef HWRENDER
#include "../hardware/r_opengl/r_opengl.h"
#include "../hardware/hw_main.h"
#include "../r_stereo.h"
#include "../r_stereo_leiasr.h"
#include "ogl_sdl.h"
#include "../i_system.h"
#include "hwsym_sdl.h"
#include "../m_argv.h"

#ifdef DEBUG_TO_FILE
#include <stdarg.h>
#if defined (_WIN32) && !defined (__CYGWIN__)
#include <direct.h>
#else
#include <unistd.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#endif

#ifdef USE_WGL_SWAP
PFNWGLEXTSWAPCONTROLPROC wglSwapIntervalEXT = NULL;
#else
typedef int (*PFNGLXSWAPINTERVALPROC) (int);
PFNGLXSWAPINTERVALPROC glXSwapIntervalSGIEXT = NULL;
#endif

#ifndef STATIC_OPENGL
PFNglClear pglClear;
PFNglGetIntegerv pglGetIntegerv;
PFNglGetString pglGetString;
#endif

/**	\brief SDL video display surface
*/
INT32 oglflags = 0;
void *GLUhandle = NULL;
SDL_GLContext sdlglcontext = 0;

void *GetGLFunc(const char *proc)
{
	if (strncmp(proc, "glu", 3) == 0)
	{
		if (GLUhandle)
			return hwSym(proc, GLUhandle);
		else
			return NULL;
	}
	return SDL_GL_GetProcAddress(proc);
}

boolean LoadGL(void)
{
#ifndef STATIC_OPENGL
	const char *OGLLibname = NULL;
	const char *GLULibname = NULL;

	if (M_CheckParm("-OGLlib") && M_IsNextParm())
		OGLLibname = M_GetNextParm();

	if (SDL_GL_LoadLibrary(OGLLibname) != 0)
	{
		CONS_Alert(CONS_ERROR, "Could not load OpenGL Library: %s\n"
					"Falling back to Software mode.\n", SDL_GetError());
		if (!M_CheckParm("-OGLlib"))
			CONS_Printf("If you know what is the OpenGL library's name, use -OGLlib\n");
		return 0;
	}

#if 0
	GLULibname = "/proc/self/exe";
#elif defined (_WIN32)
	GLULibname = "GLU32.DLL";
#elif defined (__MACH__)
	GLULibname = "/System/Library/Frameworks/OpenGL.framework/Libraries/libGLU.dylib";
#elif defined (macintos)
	GLULibname = "OpenGLLibrary";
#elif defined (__unix__)
	GLULibname = "libGLU.so.1";
#elif defined (__HAIKU__)
	GLULibname = "libGLU.so";
#else
	GLULibname = NULL;
#endif

	if (M_CheckParm("-GLUlib") && M_IsNextParm())
		GLULibname = M_GetNextParm();

	if (GLULibname)
	{
		GLUhandle = hwOpen(GLULibname);
		if (GLUhandle)
			return SetupGLfunc();
		else
		{
			CONS_Alert(CONS_ERROR, "Could not load GLU Library: %s\n", GLULibname);
			if (!M_CheckParm ("-GLUlib"))
				CONS_Alert(CONS_ERROR, "If you know what is the GLU library's name, use -GLUlib\n");
		}
	}
	else
	{
		CONS_Alert(CONS_ERROR, "Could not load GLU Library\n");
		CONS_Alert(CONS_ERROR, "If you know what is the GLU library's name, use -GLUlib\n");
	}
#endif
	return SetupGLfunc();
}

/**	\brief	The OglSdlSurface function

	\param	w	width
	\param	h	height
	\param	isFullscreen	if true, go fullscreen

	\return	if true, changed video mode
*/
boolean OglSdlSurface(INT32 w, INT32 h)
{
	INT32 cbpp = cv_scr_depth.value < 16 ? 16 : cv_scr_depth.value;
	static boolean first_init = false;

	oglflags = 0;

	if (!first_init)
	{
		gl_version = pglGetString(GL_VERSION);
		gl_renderer = pglGetString(GL_RENDERER);
		gl_extensions = pglGetString(GL_EXTENSIONS);

		GL_DBG_Printf("OpenGL %s\n", gl_version);
		GL_DBG_Printf("GPU: %s\n", gl_renderer);
		GL_DBG_Printf("Extensions: %s\n", gl_extensions);

		if (strcmp((const char*)gl_renderer, "GDI Generic") == 0 &&
			strcmp((const char*)gl_version, "1.1.0") == 0)
		{
			// Oh no... Windows gave us the GDI Generic rasterizer, so something is wrong...
			// The game will crash later on when unsupported OpenGL commands are encountered.
			// Instead of a nondescript crash, show a more informative error message.
			// Also set the renderer variable back to software so the next launch won't
			// repeat this error.
			CV_StealthSet(&cv_renderer, "Software");
			I_Error("OpenGL Error: Failed to access the GPU. Possible reasons include:\n"
					"- GPU vendor has dropped OpenGL support on your GPU and OS. (Old GPU?)\n"
					"- GPU drivers are missing or broken. You may need to update your drivers.");
		}
	}
	first_init = true;

	if (isExtAvailable("GL_EXT_texture_filter_anisotropic", gl_extensions))
		pglGetIntegerv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maximumAnisotropy);
	else
		maximumAnisotropy = 1;

	SetupGLFunc4();

	glanisotropicmode_cons_t[1].value = maximumAnisotropy;

	SDL_GL_SetSwapInterval(cv_vidwait.value ? 1 : 0);

	SetModelView(w, h);
	SetStates();
	pglClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

	HWR_Startup();
	textureformatGL = cbpp > 16 ? GL_RGBA : GL_RGB5_A1;

	return true;
}

/**	\brief	The OglSdlFinishUpdate function

	\param	vidwait	wait for video sync

	\return	void
*/
void OglSdlFinishUpdate(boolean waitvbl)
{
	static boolean oldwaitvbl = false;
	int sdlw, sdlh;
	INT32 composite_shader = -1;
	boolean weave = false;
	boolean stereo_stretch;
	if (oldwaitvbl != waitvbl)
	{
		SDL_GL_SetSwapInterval(waitvbl ? 1 : 0);
	}

	oldwaitvbl = waitvbl;

	// Drawable size, NOT SDL_GetWindowSize: the latter is in logical units,
	// which diverge from real framebuffer pixels on a DPI-scaled display.
	// Everything below is pixel-exact work -- stretching the render to fill
	// the panel, recapturing it, and handing it to the interlaced/checkerboard
	// composites or the SR weaver -- so a logical size silently scales all of
	// it wrong and the stereo output turns to garbage.
	SDL_GL_GetDrawableSize(window, &sdlw, &sdlh);

	// Stereo present paths stretch the rendered backbuffer to fill the window:
	// black bars from aspect mismatch would break a full-SbS display's signal,
	// the SR weaver's input, and the composite shaders' source layout.
	stereo_stretch = R_BackbufferIsStereo();

	// Which display format the user asked for. Deliberately cv_stereomode and
	// NOT R_StereoMode(): the latter has already collapsed several distinct
	// user-facing modes down to the SbS/TaB layout they render as internally,
	// which is exactly the information we must not lose here.
	if (stereo_stretch)
	{
		switch (cv_stereomode.value)
		{
			case STEREO_ANAGLYPH:          composite_shader = SHADER_ANAGLYPH_DUBOIS_COMPOSITE;   break;
			case STEREO_ROW_INTERLACED:    composite_shader = SHADER_ROW_INTERLACED_COMPOSITE;    break;
			case STEREO_COLUMN_INTERLACED: composite_shader = SHADER_COLUMN_INTERLACED_COMPOSITE; break;
			case STEREO_CHECKERBOARD:      composite_shader = SHADER_CHECKERBOARD_COMPOSITE;      break;
			case STEREO_LEIASR:            weave = R_LeiaSR_Available();                          break;
			default: break;
		}
	}

	HWR_MakeScreenFinalTexture();
	HWR_DrawScreenFinalTexture(sdlw, sdlh, stereo_stretch);

	if (composite_shader >= 0 || weave)
	{
		// Both paths need a source texture whose dimensions match the display,
		// so that per-pixel parity (interlaced/checkerboard) and the weaver's
		// [0,1] sampling land on real display pixels. The draw above already
		// stretched the render to window size; recapture that.
		//
		// Do NOT call GClipRect between here and the weave: it derives its
		// viewport from screen_height (the *rendered* height), which goes
		// negative once sdlh > screen_height and pushes the output off-screen.
		HWR_MakeScreenLeiaTextureSized(sdlw, sdlh);

		if (composite_shader >= 0)
			HWR_DrawStereoComposite(composite_shader, sdlw, sdlh);
		else
		{
			// The weaver writes into the bound viewport, which after the eye
			// loop is still the engine's render rect.
			HWR_SetPresentViewport(sdlw, sdlh);
			R_LeiaSR_Weave(HWR_GetScreenLeiaTextureID(), sdlw, sdlh);
		}
	}

	// Ask for the lenticular lens exactly while we're weaving through it, so a
	// panel with a switchable lens goes back to being a sharp 2D monitor the
	// moment the player leaves LeiaSR mode rather than at exit. Cached inside;
	// this only reaches the SR runtime when the answer changes. Deliberately
	// after the weave above, so the first weaving frame has already brought
	// the weaver up and there is a context to ask with.
	R_LeiaSR_SetLens(weave);

	SDL_GL_SwapWindow(window);

	GClipRect(0, 0, realwidth, realheight, NZCLIP_PLANE);

	// Sryder:	We need to draw the final screen texture again into the other buffer in the original position so that
	//			effects that want to take the old screen can do so after this
	HWR_DrawScreenFinalTexture(realwidth, realheight, stereo_stretch);
}

EXPORT void HWRAPI(OglSdlSetPalette) (RGBA_t *palette)
{
	size_t palsize = (sizeof(RGBA_t) * 256);
	// on a palette change, you have to reload all of the textures
	if (memcmp(&myPaletteData, palette, palsize))
	{
		memcpy(&myPaletteData, palette, palsize);
		Flush();
	}
}

#endif //HWRENDER
#endif //SDL
