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
 * The Original Code is Copyright (C) 2011 Blender Foundation.
 * All rights reserved.
 *
 *
 * Contributor(s): Blender Foundation,
 *                 Sergey Sharybin
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/space_clip/clip_graph_draw.c
 *  \ingroup spclip
 */

#include "DNA_movieclip_types.h"
#include "DNA_scene_types.h"
#include "DNA_object_types.h"   /* SELECT */

#include "MEM_guardedalloc.h"

#include "BKE_context.h"
#include "BKE_movieclip.h"
#include "BKE_tracking.h"

#include "BLI_utildefines.h"
#include "BLI_math.h"
#include "BLI_string.h"

#include "ED_screen.h"
#include "ED_clip.h"

#include "GPU_colors.h"
#include "GPU_primitives.h"

#include "BIF_glutil.h"

#include "WM_types.h"

#include "UI_interface.h"
#include "UI_resources.h"
#include "UI_view2d.h"

#include "BLF_api.h"

#include "clip_intern.h"    // own include

static void draw_curve_knot(float x, float y, float xscale, float yscale, float hsize)
{
	gpuPushMatrix();
	gpuTranslate(x, y, 0.0f);
	gpuScale(1.0f / xscale * hsize, 1.0f / yscale * hsize, 1.0f);

	gpuSingleCircle(0, 0, 0.7, 8);

	gpuPopMatrix();
}

static void tracking_segment_point_cb(void *UNUSED(userdata), MovieTrackingTrack *UNUSED(track),
                                      MovieTrackingMarker *UNUSED(marker), int UNUSED(coord),
                                      int scene_framenr, float val)
{
	gpuVertex2f(scene_framenr, val);
}

static void tracking_segment_start_cb(void *userdata, MovieTrackingTrack *track, int coord)
{
	static float colors[2][3] = {{1.0f, 0.0f, 0.0f},
	                             {0.0f, 1.0f, 0.0f}};
	float col[4];

	copy_v3_v3(col, colors[coord]);

	if (track == userdata) {
		col[3] = 1.0f;
		glLineWidth(2.0f);
	}
	else {
		col[3] = 0.5f;
		glLineWidth(1.0f);
	}

	gpuCurrentColor4fv(col);

	gpuBegin(GL_LINE_STRIP);
}

static void tracking_segment_end_cb(void *UNUSED(userdata))
{
	gpuEnd();

	glLineWidth(1.0f);
}

static void tracking_segment_knot_cb(void *userdata, MovieTrackingTrack *track,
                                     MovieTrackingMarker *marker, int coord, int scene_framenr, float val)
{
	struct { MovieTrackingTrack *act_track; int sel; float xscale, yscale, hsize; } *data = userdata;
	int sel = 0, sel_flag;

	if (track != data->act_track)
		return;

	sel_flag = coord == 0 ? MARKER_GRAPH_SEL_X : MARKER_GRAPH_SEL_Y;
	sel = (marker->flag & sel_flag) ? 1 : 0;

	if (sel == data->sel) {
		if (sel)
			UI_ThemeColor(TH_HANDLE_VERTEX_SELECT);
		else
			UI_ThemeColor(TH_HANDLE_VERTEX);

		draw_curve_knot(scene_framenr, val, data->xscale, data->yscale, data->hsize);
	}
}

static void draw_tracks_curves(View2D *v2d, SpaceClip *sc)
{
	MovieClip *clip = ED_space_clip_get_clip(sc);
	MovieTracking *tracking = &clip->tracking;
	MovieTrackingTrack *act_track = BKE_tracking_track_get_active(tracking);
	int width, height;
	struct { MovieTrackingTrack *act_track; int sel; float xscale, yscale, hsize; } userdata;

	BKE_movieclip_get_size(clip, &sc->user, &width, &height);

	if (!width || !height)
		return;

	/* non-selected knot handles */
	userdata.hsize = UI_GetThemeValuef(TH_HANDLE_VERTEX_SIZE);
	userdata.sel = FALSE;
	userdata.act_track = act_track;
	UI_view2d_getscale(v2d, &userdata.xscale, &userdata.yscale);
	clip_graph_tracking_values_iterate(sc, sc->flag & SC_SHOW_GRAPH_SEL_ONLY, sc->flag & SC_SHOW_GRAPH_HIDDEN,
	                                   &userdata, tracking_segment_knot_cb, NULL, NULL);

	/* draw graph lines */
	glEnable(GL_BLEND);
	clip_graph_tracking_values_iterate(sc, sc->flag & SC_SHOW_GRAPH_SEL_ONLY, sc->flag & SC_SHOW_GRAPH_HIDDEN,
	                                   act_track, tracking_segment_point_cb, tracking_segment_start_cb,
	                                   tracking_segment_end_cb);
	glDisable(GL_BLEND);

	/* selected knot handles on top of curves */
	userdata.sel = TRUE;
	clip_graph_tracking_values_iterate(sc, sc->flag & SC_SHOW_GRAPH_SEL_ONLY, sc->flag & SC_SHOW_GRAPH_HIDDEN,
	                                   &userdata, tracking_segment_knot_cb, NULL, NULL);
}

static void draw_frame_curves(SpaceClip *sc)
{
	MovieClip *clip = ED_space_clip_get_clip(sc);
	MovieTracking *tracking = &clip->tracking;
	MovieTrackingReconstruction *reconstruction = BKE_tracking_get_active_reconstruction(tracking);
	int i, lines = 0, prevfra = 0;

	gpuColor3x(CPACK_BLUE);

	for (i = 0; i < reconstruction->camnr; i++) {
		MovieReconstructedCamera *camera = &reconstruction->cameras[i];
		int framenr;

		if (lines && camera->framenr != prevfra + 1) {
			gpuEnd();
			lines = 0;
		}

		if (!lines) {
			gpuBegin(GL_LINE_STRIP);
			lines = 1;
		}

		framenr = BKE_movieclip_remap_clip_to_scene_frame(clip, camera->framenr);
		gpuVertex2f(framenr, camera->error);

		prevfra = camera->framenr;
	}

	if (lines)
		gpuEnd();
}

void clip_draw_graph(SpaceClip *sc, ARegion *ar, Scene *scene)
{
	MovieClip *clip = ED_space_clip_get_clip(sc);
	View2D *v2d = &ar->v2d;
	View2DGrid *grid;
	short unitx = V2D_UNIT_FRAMESCALE, unity = V2D_UNIT_VALUES;

	/* grid */
	grid = UI_view2d_grid_calc(scene, v2d, unitx, V2D_GRID_NOCLAMP, unity, V2D_GRID_NOCLAMP, ar->winx, ar->winy);
	UI_view2d_grid_draw(v2d, grid, V2D_GRIDLINES_ALL);
	UI_view2d_grid_free(grid);

	if (clip) {
		if (sc->flag & SC_SHOW_GRAPH_TRACKS)
			draw_tracks_curves(v2d, sc);

		if (sc->flag & SC_SHOW_GRAPH_FRAMES)
			draw_frame_curves(sc);
	}

	/* frame range */
	clip_draw_sfra_efra(v2d, scene);

	/* current frame */
	clip_draw_cfra(sc, ar, scene);
}
