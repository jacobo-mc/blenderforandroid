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
 * The Original Code is Copyright (C) 2008 Blender Foundation.
 * All rights reserved.
 *
 *
 * Contributor(s): Blender Foundation
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/windowmanager/intern/wm_gesture.c
 *  \ingroup wm
 */


#include "DNA_screen_types.h"
#include "DNA_vec_types.h"
#include "DNA_userdef_types.h"
#include "DNA_windowmanager_types.h"

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_scanfill.h"   /* lasso tessellation */
#include "BLI_utildefines.h"

#include "BKE_context.h"


#include "WM_api.h"
#include "WM_types.h"

#include "wm.h"
#include "wm_event_system.h"
#include "wm_subwindow.h"
#include "wm_draw.h"

#include "GPU_colors.h"
#include "GPU_primitives.h"

#include "BIF_glutil.h"


/* context checked on having screen, window and area */
wmGesture *WM_gesture_new(bContext *C, wmEvent *event, int type)
{
	wmGesture *gesture = MEM_callocN(sizeof(wmGesture), "new gesture");
	wmWindow *window = CTX_wm_window(C);
	ARegion *ar = CTX_wm_region(C);
	int sx, sy;
	
	BLI_addtail(&window->gesture, gesture);
	
	gesture->type = type;
	gesture->event_type = event->type;
	gesture->swinid = ar->swinid;    /* means only in area-region context! */
	
	wm_subwindow_getorigin(window, gesture->swinid, &sx, &sy);
	
	if (ELEM5(type, WM_GESTURE_RECT, WM_GESTURE_CROSS_RECT, WM_GESTURE_TWEAK,
	          WM_GESTURE_CIRCLE, WM_GESTURE_STRAIGHTLINE))
	{
		rcti *rect = MEM_callocN(sizeof(rcti), "gesture rect new");
		
		gesture->customdata = rect;
		rect->xmin = event->x - sx;
		rect->ymin = event->y - sy;
		if (type == WM_GESTURE_CIRCLE) {
#ifdef GESTURE_MEMORY
			rect->xmax = circle_select_size;
#else
			rect->xmax = 25;    // XXX temp
#endif
		}
		else {
			rect->xmax = event->x - sx;
			rect->ymax = event->y - sy;
		}
	}
	else if (ELEM(type, WM_GESTURE_LINES, WM_GESTURE_LASSO)) {
		short *lasso;
		gesture->customdata = lasso = MEM_callocN(2 * sizeof(short) * WM_LASSO_MIN_POINTS, "lasso points");
		lasso[0] = event->x - sx;
		lasso[1] = event->y - sy;
		gesture->points = 1;
		gesture->size = WM_LASSO_MIN_POINTS;
	}
	
	return gesture;
}

void WM_gesture_end(bContext *C, wmGesture *gesture)
{
	wmWindow *win = CTX_wm_window(C);
	
	if (win->tweak == gesture)
		win->tweak = NULL;
	BLI_remlink(&win->gesture, gesture);
	MEM_freeN(gesture->customdata);
	MEM_freeN(gesture);
}

void WM_gestures_remove(bContext *C)
{
	wmWindow *win = CTX_wm_window(C);
	
	while (win->gesture.first)
		WM_gesture_end(C, win->gesture.first);
}


/* tweak and line gestures */
int wm_gesture_evaluate(wmGesture *gesture)
{
	if (gesture->type == WM_GESTURE_TWEAK) {
		rcti *rect = gesture->customdata;
		int dx = BLI_rcti_size_x(rect);
		int dy = BLI_rcti_size_y(rect);
		if (ABS(dx) + ABS(dy) > U.tweak_threshold) {
			int theta = (int)floor(4.0f * atan2f((float)dy, (float)dx) / (float)M_PI + 0.5f);
			int val = EVT_GESTURE_W;

			if (theta == 0) val = EVT_GESTURE_E;
			else if (theta == 1) val = EVT_GESTURE_NE;
			else if (theta == 2) val = EVT_GESTURE_N;
			else if (theta == 3) val = EVT_GESTURE_NW;
			else if (theta == -1) val = EVT_GESTURE_SE;
			else if (theta == -2) val = EVT_GESTURE_S;
			else if (theta == -3) val = EVT_GESTURE_SW;
			
#if 0
			/* debug */
			if (val == 1) printf("tweak north\n");
			if (val == 2) printf("tweak north-east\n");
			if (val == 3) printf("tweak east\n");
			if (val == 4) printf("tweak south-east\n");
			if (val == 5) printf("tweak south\n");
			if (val == 6) printf("tweak south-west\n");
			if (val == 7) printf("tweak west\n");
			if (val == 8) printf("tweak north-west\n");
#endif
			return val;
		}
	}
	return 0;
}


/* ******************* gesture draw ******************* */

static void wm_gesture_draw_rect(wmGesture *gt)
{
	rcti *rect = (rcti *)gt->customdata;

	glEnable(GL_BLEND);

	gpuCurrentColor4x(CPACK_WHITE, 0.050f);
	gpuBegin(GL_TRIANGLE_FAN);
	gpuVertex2i(rect->xmax, rect->ymin);
	gpuVertex2i(rect->xmax, rect->ymax);
	gpuVertex2i(rect->xmin, rect->ymax);
	gpuVertex2i(rect->xmin, rect->ymin);
	gpuEnd();

	glDisable(GL_BLEND);

	glEnable(GL_LINE_STIPPLE);

	gpuCurrentGray3f(0.376f);
	glLineStipple(1, 0xCCCC);
	gpuDrawWireRecti(rect->xmin, rect->ymin, rect->xmax, rect->ymax);

	gpuCurrentColor3x(CPACK_WHITE);
	glLineStipple(1, 0x3333);
	gpuDrawWireRecti(rect->xmin, rect->ymin, rect->xmax, rect->ymax);

	glDisable(GL_LINE_STIPPLE);
}

static void wm_gesture_draw_line(wmGesture *gt)
{
	rcti *rect = (rcti *)gt->customdata;

	glEnable(GL_LINE_STIPPLE);

	glLineStipple(1, 0xAAAA);
	gpuCurrentGray3f(0.376f);
	gpuDrawLinei(rect->xmin, rect->ymin, rect->xmax, rect->ymax);

	glLineStipple(1, 0x5555);
	gpuCurrentColor3x(CPACK_WHITE);
	gpuDrawLinei(rect->xmin, rect->ymin, rect->xmax, rect->ymax);

	glDisable(GL_LINE_STIPPLE);
}

static void wm_gesture_draw_circle(wmGesture *gt)
{
	rcti *rect = (rcti *)gt->customdata;

	float x = (float)(rect->xmin);
	float y = (float)(rect->ymin);

	glEnable(GL_BLEND);

	gpuCurrentColor4x(CPACK_WHITE, 0.050f);
	gpuDrawDisk(x, y, rect->xmax, 40);

	glDisable(GL_BLEND);

	glEnable(GL_LINE_STIPPLE);

	glLineStipple(1, 0xAAAA);
	gpuCurrentGray3f(0.376f);
	gpuDrawCircle(x, y, rect->xmax, 40);

	glLineStipple(1, 0x5555);
	gpuCurrentColor3x(CPACK_WHITE);
	gpuDrawCircle(x, y, rect->xmax, 40);

	glDisable(GL_LINE_STIPPLE);
}

static void draw_filled_lasso(wmGesture *gt)
{
	ScanFillContext sf_ctx;
	ScanFillVert *sf_vert = NULL, *sf_vert_last = NULL, *sf_vert_first = NULL;
	ScanFillFace *sf_tri;
	short *lasso = (short *)gt->customdata;
	int i;

	BLI_scanfill_begin(&sf_ctx);
	for (i = 0; i < gt->points; i++, lasso += 2) {
		float co[3];

		co[0] = (float)lasso[0];
		co[1] = (float)lasso[1];
		co[2] = 0.0f;

		sf_vert = BLI_scanfill_vert_add(&sf_ctx, co);
		if (sf_vert_last)
			/* e = */ /* UNUSED */ BLI_scanfill_edge_add(&sf_ctx, sf_vert_last, sf_vert);
		sf_vert_last = sf_vert;
		if (sf_vert_first == NULL) sf_vert_first = sf_vert;
	}

	/* highly unlikely this will fail, but could crash if (gt->points == 0) */
	if (sf_vert_first) {
		const float zvec[3] = {0.0f, 0.0f, 1.0f};
		BLI_scanfill_edge_add(&sf_ctx, sf_vert_first, sf_vert);
		BLI_scanfill_calc_ex(&sf_ctx, FALSE, zvec);

		glEnable(GL_BLEND);

		gpuCurrentColor4x(CPACK_WHITE, 0.050f);
		gpuBegin(GL_TRIANGLES);
		for (sf_tri = sf_ctx.fillfacebase.first; sf_tri; sf_tri = sf_tri->next) {
			gpuVertex2fv(sf_tri->v1->co);
			gpuVertex2fv(sf_tri->v2->co);
			gpuVertex2fv(sf_tri->v3->co);
		}
		gpuEnd();

		glDisable(GL_BLEND);

		BLI_scanfill_end(&sf_ctx);
	}
}

static void wm_gesture_draw_lasso(wmGesture *gt)
{
	short *lasso = (short *)gt->customdata;
	int i;

	draw_filled_lasso(gt);
	
	glEnable(GL_LINE_STIPPLE);

	glLineStipple(1, 0xAAAA);
	gpuCurrentGray3f(0.376f);

	gpuBegin(GL_LINE_STRIP);

	for (i = 0; i < gt->points; i++, lasso += 2) {
		gpuVertex2sv(lasso);
	}

	if (gt->type == WM_GESTURE_LASSO) {
		gpuVertex2sv((short *)gt->customdata);
	}

	gpuEnd();

	glLineStipple(1, 0x5555);
	gpuCurrentColor3x(CPACK_WHITE);

	gpuBegin(GL_LINE_STRIP);

	lasso = (short *)gt->customdata;
	for (i = 0; i < gt->points; i++, lasso += 2) {
		gpuVertex2sv(lasso);
	}

	if (gt->type == WM_GESTURE_LASSO) {
		gpuVertex2sv((short *)gt->customdata);
	}

	gpuEnd();

	glDisable(GL_LINE_STIPPLE);
}

static void wm_gesture_draw_cross(wmWindow *win, wmGesture *gt)
{
	rcti *rect = (rcti *)gt->customdata;

	glEnable(GL_LINE_STIPPLE);

	glLineStipple(1, 0xCCCC);
	gpuCurrentGray3f(0.376f);
	gpuBegin(GL_LINES);
	gpuAppendLinei(rect->xmin - win->sizex, rect->ymin, rect->xmin + win->sizex, rect->ymin);
	gpuAppendLinei(rect->xmin, rect->ymin - win->sizey, rect->xmin, rect->ymin + win->sizey);
	gpuEnd();

	glLineStipple(1, 0x3333);
	gpuCurrentColor3x(CPACK_WHITE);
	gpuBegin(GL_LINES);
	gpuAppendLinei(rect->xmin - win->sizex, rect->ymin, rect->xmin + win->sizex, rect->ymin);
	gpuAppendLinei(rect->xmin, rect->ymin - win->sizey, rect->xmin, rect->ymin + win->sizey);
	gpuEnd();

	glDisable(GL_LINE_STIPPLE);
}

/* called in wm_draw.c */
void wm_gesture_draw(wmWindow *win)
{
	wmGesture *gt = (wmGesture *)win->gesture.first;

	gpuImmediateFormat_V2();

	for (; gt; gt = gt->next) {
		/* all in subwindow space */
		wmSubWindowSet(win, gt->swinid);

		if (gt->type == WM_GESTURE_RECT)
			wm_gesture_draw_rect(gt);
//		else if (gt->type == WM_GESTURE_TWEAK)
//			wm_gesture_draw_line(gt);
		else if (gt->type == WM_GESTURE_CIRCLE)
			wm_gesture_draw_circle(gt);
		else if (gt->type == WM_GESTURE_CROSS_RECT) {
			if (gt->mode == 1)
				wm_gesture_draw_rect(gt);
			else
				wm_gesture_draw_cross(win, gt);
		}
		else if (gt->type == WM_GESTURE_LINES)
			wm_gesture_draw_lasso(gt);
		else if (gt->type == WM_GESTURE_LASSO)
			wm_gesture_draw_lasso(gt);
		else if (gt->type == WM_GESTURE_STRAIGHTLINE)
			wm_gesture_draw_line(gt);
	}

	gpuImmediateUnformat();
}

void wm_gesture_tag_redraw(bContext *C)
{
	wmWindow *win = CTX_wm_window(C);
	bScreen *screen = CTX_wm_screen(C);
	ARegion *ar = CTX_wm_region(C);
	
	if (screen)
		screen->do_draw_gesture = TRUE;

	wm_tag_redraw_overlay(win, ar);
}
