/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version. 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * Contributor(s): 2007 Blender Foundation (refactor)
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 *
 * Subwindow opengl handling. 
 * BTW: subwindows open/close in X11 are way too slow, tried it, and choose for my own system... (ton)
 * 
 */

/** \file blender/windowmanager/intern/wm_subwindow.c
 *  \ingroup wm
 */

#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_windowmanager_types.h"
#include "DNA_screen_types.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_utildefines.h"

#include "BIF_glutil.h"

#include "BKE_context.h"
#include "BKE_global.h"

#include "GPU_compatibility.h"
#include "GPU_extensions.h"

#include "WM_api.h"
#include "wm_subwindow.h"
#include "wm_window.h"

/* wmSubWindow stored in wmWindow... but not exposed outside this C file */
/* it seems a bit redundant (area regions can store it too, but we keep it
 * because we can store all kind of future opengl fanciness here */

/* we use indices and array because:
 * - index has safety, no pointers from this C file hanging around
 * - fast lookups of indices with array, list would give overhead
 * - old code used it this way...
 * - keep option open to have 2 screens using same window
 */

typedef struct wmSubWindow {
	struct wmSubWindow *next, *prev;
	
	rcti winrct;
	int swinid;
} wmSubWindow;


/* ******************* open, free, set, get data ******************** */

/* not subwindow itself */
static void wm_subwindow_free(wmSubWindow *UNUSED(swin))
{
	/* future fancy stuff */
}

void wm_subwindows_free(wmWindow *win)
{
	wmSubWindow *swin;
	
	for (swin = win->subwindows.first; swin; swin = swin->next)
		wm_subwindow_free(swin);
	
	BLI_freelistN(&win->subwindows);
}


int wm_subwindow_get(wmWindow *win)	
{
	if (win->curswin)
		return win->curswin->swinid;
	return 0;
}

static wmSubWindow *swin_from_swinid(wmWindow *win, int swinid)
{
	wmSubWindow *swin;
	
	for (swin = win->subwindows.first; swin; swin = swin->next)
		if (swin->swinid == swinid)
			break;
	return swin;
}

void wm_subwindow_getsize(wmWindow *win, int swinid, int *x, int *y) 
{
	wmSubWindow *swin = swin_from_swinid(win, swinid);

	if (swin) {
		*x = BLI_rcti_size_x(&swin->winrct) + 1;
		*y = BLI_rcti_size_y(&swin->winrct) + 1;
	}
}

void wm_subwindow_getorigin(wmWindow *win, int swinid, int *x, int *y)
{
	wmSubWindow *swin = swin_from_swinid(win, swinid);

	if (swin) {
		*x = swin->winrct.xmin;
		*y = swin->winrct.ymin;
	}
}

void wm_subwindow_getmatrix(wmWindow *win, int swinid, float mat[][4])
{
	wmSubWindow *swin = swin_from_swinid(win, swinid);

	if (swin) {
		/* used by UI, should find a better way to get the matrix there */
		if (swinid == win->screen->mainwin) {
			int width, height;

			wm_subwindow_getsize(win, swin->swinid, &width, &height);
			orthographic_m4(mat, -GLA_PIXEL_OFS, (float)width - GLA_PIXEL_OFS, -GLA_PIXEL_OFS, (float)height - GLA_PIXEL_OFS, -100, 100);
		}
		else {
			gpuGetMatrix(GL_PROJECTION_MATRIX, (float *)mat);
		}
	}
}

/* always sets pixel-precise 2D window/view matrices */
/* coords is in whole pixels. xmin = 15, xmax = 16: means window is 2 pix big */
int wm_subwindow_open(wmWindow *win, rcti *winrct)
{
	wmSubWindow *swin;
	int width, height;
	int freewinid = 1;
	
	for (swin = win->subwindows.first; swin; swin = swin->next)
		if (freewinid <= swin->swinid)
			freewinid = swin->swinid + 1;

	win->curswin = swin = MEM_callocN(sizeof(wmSubWindow), "swinopen");
	BLI_addtail(&win->subwindows, swin);
	
	swin->swinid = freewinid;
	swin->winrct = *winrct;

	/* and we appy it all right away */
	wmSubWindowSet(win, swin->swinid);
	
	/* extra service */
	wm_subwindow_getsize(win, swin->swinid, &width, &height);
	wmOrtho2(-GLA_PIXEL_OFS, (float)width - GLA_PIXEL_OFS, -GLA_PIXEL_OFS, (float)height - GLA_PIXEL_OFS);
	gpuLoadIdentity();

	return swin->swinid;
}


void wm_subwindow_close(wmWindow *win, int swinid)
{
	wmSubWindow *swin = swin_from_swinid(win, swinid);

	if (swin) {
		if (swin == win->curswin)
			win->curswin = NULL;
		wm_subwindow_free(swin);
		BLI_remlink(&win->subwindows, swin);
		MEM_freeN(swin);
	}
	else {
		printf("%s: Internal error, bad winid: %d\n", __func__, swinid);
	}
}

/* pixels go from 0-99 for a 100 pixel window */
void wm_subwindow_position(wmWindow *win, int swinid, rcti *winrct)
{
	wmSubWindow *swin = swin_from_swinid(win, swinid);
	
	if (swin) {
		int width, height;
		
		swin->winrct = *winrct;
		
		/* CRITICAL, this clamping ensures that
		 * the viewport never goes outside the screen
		 * edges (assuming the x, y coords aren't
		 *        outside). This caused a hardware lock
		 * on Matrox cards if it happens.
		 *
		 * Really Blender should never _ever_ try
		 * to do such a thing, but just to be safe
		 * clamp it anyway (or fix the bScreen
		 * scaling routine, and be damn sure you
		 * fixed it). - zr  (2001!)
		 */
		
		if (swin->winrct.xmax > win->sizex)
			swin->winrct.xmax = win->sizex;
		if (swin->winrct.ymax > win->sizey)
			swin->winrct.ymax = win->sizey;
		
		/* extra service */
		wmSubWindowSet(win, swinid);
		wm_subwindow_getsize(win, swinid, &width, &height);
		wmOrtho2(-GLA_PIXEL_OFS, (float)width - GLA_PIXEL_OFS, -GLA_PIXEL_OFS, (float)height - GLA_PIXEL_OFS);
	}
	else {
		printf("%s: Internal error, bad winid: %d\n", __func__, swinid);
	}
}

/* ---------------- WM versions of OpenGL style API calls ------------------------ */
/* ----------------- exported in WM_api.h ------------------------------------------------------ */

/* internal state, no threaded opengl! XXX */
static wmWindow    *_curwindow  = NULL;
static wmSubWindow *_curswin    = NULL;

void wmSubWindowScissorSet(wmWindow *win, int swinid, rcti *srct)
{
	int x, y, width, height;

	_curswin = swin_from_swinid(win, swinid);

	if (_curswin == NULL) {
		printf("%s %d: doesn't exist\n", __func__, swinid);
		return;
	}

	win->curswin = _curswin;

	_curwindow = win;

	x      = _curswin->winrct.xmin;
	y      = _curswin->winrct.ymin;
	width  = BLI_rcti_size_x(&_curswin->winrct) + 1;
	height = BLI_rcti_size_y(&_curswin->winrct) + 1;

	gpuViewport(x, y, width, height);

	if (srct) {
		x      = srct->xmin;
		y      = srct->ymin;
		width  = BLI_rcti_size_x(srct) + 1;
		height = BLI_rcti_size_y(srct) + 1;
	}

	gpuScissor(x, y, width, height);

	wmOrtho2(-GLA_PIXEL_OFS, (float)width - GLA_PIXEL_OFS, -GLA_PIXEL_OFS, (float)height - GLA_PIXEL_OFS);

	gpuLoadIdentity(); /* reset MODELVIEW */
}

/* enable the WM versions of opengl calls */
void wmSubWindowSet(wmWindow *win, int swinid)
{
	wmSubWindowScissorSet(win, swinid, NULL);
}

void wmFrustum(float x1, float x2, float y1, float y2, float n, float f)
{
	gpuMatrixMode(GL_PROJECTION);
	gpuLoadFrustum(x1, x2, y1, y2, n, f);
	gpuMatrixMode(GL_MODELVIEW);
}

void wmOrtho(float x1, float x2, float y1, float y2, float n, float f)
{
	gpuMatrixMode(GL_PROJECTION);
	gpuLoadOrtho(x1, x2, y1, y2, n, f);
	gpuMatrixMode(GL_MODELVIEW);
}

void wmOrtho2(float x1, float x2, float y1, float y2)
{
	/* make sure the window rectangle is not degenerate */

	if (x1 == x2) {
		x2 += 1.0f;
	}

	if (y1 == y2) {
		y2 += 1.0f;
	}

	wmOrtho(x1, x2, y1, y2, -100, 100);
}

/* *************************** Framebuffer color depth, for selection codes ********************** */

#ifdef __APPLE__

/* apple seems to round colors to below and up on some configs */

unsigned int index_to_framebuffer(int index)
{
	unsigned int i = index;

	switch (GPU_color_depth()) {
		case 12:
			i = ((i & 0xF00) << 12) + ((i & 0xF0) << 8) + ((i & 0xF) << 4);
			/* sometimes dithering subtracts! */
			i |= 0x070707;
			break;
		case 15:
		case 16:
			i = ((i & 0x7C00) << 9) + ((i & 0x3E0) << 6) + ((i & 0x1F) << 3);
			i |= 0x030303;
			break;
		case 24:
			break;
		default: /* 18 bits... */
			i = ((i & 0x3F000) << 6) + ((i & 0xFC0) << 4) + ((i & 0x3F) << 2);
			i |= 0x010101;
			break;
	}

	return i;
}

#else

/* this is the old method as being in use for ages.... seems to work? colors are rounded to lower values */

unsigned int index_to_framebuffer(int index)
{
	unsigned int i = index;
	
	switch (GPU_color_depth()) {
		case 8:
			i = ((i & 48) << 18) + ((i & 12) << 12) + ((i & 3) << 6);
			i |= 0x3F3F3F;
			break;
		case 12:
			i = ((i & 0xF00) << 12) + ((i & 0xF0) << 8) + ((i & 0xF) << 4);
			/* sometimes dithering subtracts! */
			i |= 0x0F0F0F;
			break;
		case 15:
		case 16:
			i = ((i & 0x7C00) << 9) + ((i & 0x3E0) << 6) + ((i & 0x1F) << 3);
			i |= 0x070707;
			break;
		case 24:
			break;
		default:    /* 18 bits... */
			i = ((i & 0x3F000) << 6) + ((i & 0xFC0) << 4) + ((i & 0x3F) << 2);
			i |= 0x030303;
			break;
	}
	
	return i;
}

#endif

void WM_framebuffer_index_set(int index)
{
	gpuColor3x(index_to_framebuffer(index));
}

void WM_set_framebuffer_index_current_color(int index)
{
	gpuCurrentColor3x(index_to_framebuffer(index));
}

int WM_framebuffer_to_index(unsigned int col)
{
	if (col == 0) return 0;

	switch (GPU_color_depth()) {
		case 8:
			return ((col & 0xC00000) >> 18) + ((col & 0xC000) >> 12) + ((col & 0xC0) >> 6);
		case 12:
			return ((col & 0xF00000) >> 12) + ((col & 0xF000) >> 8) + ((col & 0xF0) >> 4);
		case 15:
		case 16:
			return ((col & 0xF80000) >> 9) + ((col & 0xF800) >> 6) + ((col & 0xF8) >> 3);
		case 24:
			return col & 0xFFFFFF;
		default: // 18 bits...
			return ((col & 0xFC0000) >> 6) + ((col & 0xFC00) >> 4) + ((col & 0xFC) >> 2);
	}
}


/* ********** END MY WINDOW ************** */
