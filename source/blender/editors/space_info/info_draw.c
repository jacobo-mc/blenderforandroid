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
 * The Original Code is Copyright (C) 2010 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Blender Foundation
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/space_info/info_draw.c
 *  \ingroup spinfo
 */



#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>

#include "BLF_api.h"

#include "BLI_blenlib.h"
#include "BLI_utildefines.h"

#include "DNA_space_types.h"
#include "DNA_screen_types.h"

// #include "BKE_suggestions.h"
#include "BKE_report.h"

#include "GPU_compatibility.h"

#include "MEM_guardedalloc.h"

#include "BIF_glutil.h"

#include "ED_datafiles.h"
#include "ED_types.h"

#include "UI_resources.h"

#include "info_intern.h"
#include "../space_info/textview.h"

/* complicates things a bit, so leaving in old simple code */
#define USE_INFO_NEWLINE

static void info_report_color(unsigned char *fg, unsigned char *bg, Report *report, const short do_tint)
{
	if (report->flag & SELECT) {
		fg[0] = 255; fg[1] = 255; fg[2] = 255;
		if (do_tint) {
			bg[0] = 96; bg[1] = 128; bg[2] = 255;
		}
		else {
			bg[0] = 90; bg[1] = 122; bg[2] = 249;
		}
	}
	else {
		fg[0] = 0; fg[1] = 0; fg[2] = 0;
		
		if (report->type & RPT_ERROR_ALL) {
			if (do_tint) { bg[0] = 220; bg[1] = 0;   bg[2] = 0;   }
			else         { bg[0] = 214; bg[1] = 0;   bg[2] = 0;   }
		}
		else if (report->type & RPT_WARNING_ALL) {
			if (do_tint) { bg[0] = 220; bg[1] = 128; bg[2] = 96;  }
			else         { bg[0] = 214; bg[1] = 122; bg[2] = 90;  }
		}
#if 0 // XXX: this looks like the selected color, so don't use this
		else if (report->type & RPT_OPERATOR_ALL) {
			if (do_tint) { bg[0] = 96;  bg[1] = 128; bg[2] = 255; }
			else         { bg[0] = 90;  bg[1] = 122; bg[2] = 249; }
		}
#endif
		else if (report->type & RPT_INFO_ALL) {
			if (do_tint) { bg[0] = 0;   bg[1] = 170; bg[2] = 0;   }
			else         { bg[0] = 0;   bg[1] = 164; bg[2] = 0;   }
		}
		else if (report->type & RPT_DEBUG_ALL) {
			if (do_tint) { bg[0] = 196; bg[1] = 196; bg[2] = 196; }
			else         { bg[0] = 190; bg[1] = 190; bg[2] = 190; }
		}
		else {
			if (do_tint) { bg[0] = 120; bg[1] = 120; bg[2] = 120; }
			else         { bg[0] = 114; bg[1] = 114; bg[2] = 114; }
		}
	}
}

/* reports! */
#ifdef USE_INFO_NEWLINE
static void report_textview_init__internal(TextViewContext *tvc)
{
	Report *report = (Report *)tvc->iter;
	const char *str = report->message;
	const char *next_str = strchr(str + tvc->iter_char, '\n');

	if (next_str) {
		tvc->iter_char_next = (int)(next_str - str);
	}
	else {
		tvc->iter_char_next = report->len;
	}
}

static int report_textview_skip__internal(TextViewContext *tvc)
{
	SpaceInfo *sinfo = (SpaceInfo *)tvc->arg1;
	const int report_mask = info_report_mask(sinfo);
	while (tvc->iter && (((Report *)tvc->iter)->type & report_mask) == 0) {
		tvc->iter = (void *)((Link *)tvc->iter)->prev;
	}
	return (tvc->iter != NULL);
}

#endif // USE_INFO_NEWLINE

static int report_textview_begin(TextViewContext *tvc)
{
	// SpaceConsole *sc = (SpaceConsole *)tvc->arg1;
	ReportList *reports = (ReportList *)tvc->arg2;

	tvc->lheight = 14; //sc->lheight;
	tvc->sel_start = 0;
	tvc->sel_end = 0;

	/* iterator */
	tvc->iter = reports->list.last;

	gpuColorAndClear(120.0 / 255.0, 120.0 / 255.0, 120.0 / 255.0, 1.0);

#ifdef USE_INFO_NEWLINE
	tvc->iter_tmp = 0;
	if (tvc->iter && report_textview_skip__internal(tvc)) {
		/* init the newline iterator */
		tvc->iter_char = 0;
		report_textview_init__internal(tvc);

		return TRUE;
	}
	else {
		return FALSE;
	}
#else
	return (tvc->iter != NULL);
#endif
}

static void report_textview_end(TextViewContext *UNUSED(tvc))
{
	/* pass */
}

#ifdef USE_INFO_NEWLINE
static int report_textview_step(TextViewContext *tvc)
{
	/* simple case, but no newline support */
	Report *report = (Report *)tvc->iter;

	if (report->len <= tvc->iter_char_next) {
		tvc->iter = (void *)((Link *)tvc->iter)->prev;
		if (tvc->iter && report_textview_skip__internal(tvc)) {
			tvc->iter_tmp++;

			tvc->iter_char = 0; /* reset start */
			report_textview_init__internal(tvc);

			return TRUE;
		}
		else {
			return FALSE;
		}
	}
	else {
		/* step to the next newline */
		tvc->iter_char = tvc->iter_char_next + 1;
		report_textview_init__internal(tvc);

		return TRUE;
	}
}

static int report_textview_line_get(struct TextViewContext *tvc, const char **line, int *len)
{
	Report *report = (Report *)tvc->iter;
	*line = report->message + tvc->iter_char;
	*len = tvc->iter_char_next - tvc->iter_char;
	return 1;
}

static int report_textview_line_color(struct TextViewContext *tvc, unsigned char fg[3], unsigned char bg[3])
{
	Report *report = (Report *)tvc->iter;
	info_report_color(fg, bg, report, tvc->iter_tmp % 2);
	return TVC_LINE_FG | TVC_LINE_BG;
}


#else // USE_INFO_NEWLINE

static int report_textview_step(TextViewContext *tvc)
{
	SpaceInfo *sinfo = (SpaceInfo *)tvc->arg1;
	const int report_mask = info_report_mask(sinfo);
	do {
		tvc->iter = (void *)((Link *)tvc->iter)->prev;
	} while (tvc->iter && (((Report *)tvc->iter)->type & report_mask) == 0);

	return (tvc->iter != NULL);
}

static int report_textview_line_get(struct TextViewContext *tvc, const char **line, int *len)
{
	Report *report = (Report *)tvc->iter;
	*line = report->message;
	*len = report->len;

	return 1;
}

static int report_textview_line_color(struct TextViewContext *tvc, unsigned char fg[3], unsigned char bg[3])
{
	Report *report = (Report *)tvc->iter;
	info_report_color(fg, bg, report, tvc->iter_tmp % 2);
	return TVC_LINE_FG | TVC_LINE_BG;
}

#endif // USE_INFO_NEWLINE

#undef USE_INFO_NEWLINE

static int info_textview_main__internal(struct SpaceInfo *sinfo, ARegion *ar, ReportList *reports, int draw, int mval[2], void **mouse_pick, int *pos_pick)
{
	int ret = 0;
	
	View2D *v2d = &ar->v2d;

	TextViewContext tvc = {0};
	tvc.begin = report_textview_begin;
	tvc.end = report_textview_end;

	tvc.step = report_textview_step;
	tvc.line_get = report_textview_line_get;
	tvc.line_color = report_textview_line_color;

	tvc.arg1 = sinfo;
	tvc.arg2 = reports;

	/* view */
	tvc.sel_start = 0;
	tvc.sel_end = 0;
	tvc.lheight = 14; //sc->lheight;
	tvc.ymin = v2d->cur.ymin;
	tvc.ymax = v2d->cur.ymax;
	tvc.winx = ar->winx;

	ret = textview_draw(&tvc, draw, mval, mouse_pick, pos_pick);
	
	return ret;
}

void *info_text_pick(struct SpaceInfo *sinfo, ARegion *ar, ReportList *reports, int mouse_y)
{
	void *mouse_pick = NULL;
	int mval[2];

	mval[0] = 0;
	mval[1] = mouse_y;

	info_textview_main__internal(sinfo, ar, reports, 0, mval, &mouse_pick, NULL);
	return (void *)mouse_pick;
}


int info_textview_height(struct SpaceInfo *sinfo, ARegion *ar, ReportList *reports)
{
	int mval[2] = {INT_MAX, INT_MAX};
	return info_textview_main__internal(sinfo, ar, reports, 0,  mval, NULL, NULL);
}

void info_textview_main(struct SpaceInfo *sinfo, ARegion *ar, ReportList *reports)
{
	int mval[2] = {INT_MAX, INT_MAX};
	info_textview_main__internal(sinfo, ar, reports, 1,  mval, NULL, NULL);
}
