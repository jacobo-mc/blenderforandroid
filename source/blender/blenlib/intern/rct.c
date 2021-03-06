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
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 */

/** \file blender/blenlib/intern/rct.c
 *  \ingroup bli
 *
 * A minimalist lib for functions doing stuff with rectangle structs.
 */

#include <stdio.h>
#include <math.h>

#include <limits.h>
#include <float.h>

#include "DNA_vec_types.h"
#include "BLI_rect.h"

/**
 * Determine if a rect is empty. An empty
 * rect is one with a zero (or negative)
 * width or height.
 *
 * \return True if \a rect is empty.
 */
int BLI_rcti_is_empty(const rcti *rect)
{
	return ((rect->xmax <= rect->xmin) || (rect->ymax <= rect->ymin));
}

int BLI_rctf_is_empty(const rctf *rect)
{
	return ((rect->xmax <= rect->xmin) || (rect->ymax <= rect->ymin));
}

int BLI_rcti_isect_pt(const rcti *rect, const int x, const int y)
{
	if (x < rect->xmin) return 0;
	if (x > rect->xmax) return 0;
	if (y < rect->ymin) return 0;
	if (y > rect->ymax) return 0;
	return 1;
}

/**
 * Determine if a rect is empty. An empty
 * rect is one with a zero (or negative)
 * width or height.
 *
 * \return True if \a rect is empty.
 */
int BLI_rcti_isect_pt_v(const rcti *rect, const int xy[2])
{
	if (xy[0] < rect->xmin) return 0;
	if (xy[0] > rect->xmax) return 0;
	if (xy[1] < rect->ymin) return 0;
	if (xy[1] > rect->ymax) return 0;
	return 1;
}

int BLI_rctf_isect_pt(const rctf *rect, const float x, const float y)
{
	if (x < rect->xmin) return 0;
	if (x > rect->xmax) return 0;
	if (y < rect->ymin) return 0;
	if (y > rect->ymax) return 0;
	return 1;
}

int BLI_rctf_isect_pt_v(const rctf *rect, const float xy[2])
{
	if (xy[0] < rect->xmin) return 0;
	if (xy[0] > rect->xmax) return 0;
	if (xy[1] < rect->ymin) return 0;
	if (xy[1] > rect->ymax) return 0;
	return 1;
}

/* based closely on 'isect_line_line_v2_int', but in modified so corner cases are treated as intersections */
static int isect_segments_i(const int v1[2], const int v2[2], const int v3[2], const int v4[2])
{
	const double div = (double)((v2[0] - v1[0]) * (v4[1] - v3[1]) - (v2[1] - v1[1]) * (v4[0] - v3[0]));
	if (div == 0.0) {
		return 1; /* co-linear */
	}
	else {
		const double labda = (double)((v1[1] - v3[1]) * (v4[0] - v3[0]) - (v1[0] - v3[0]) * (v4[1] - v3[1])) / div;
		const double mu    = (double)((v1[1] - v3[1]) * (v2[0] - v1[0]) - (v1[0] - v3[0]) * (v2[1] - v1[1])) / div;
		return (labda >= 0.0 && labda <= 1.0 && mu >= 0.0 && mu <= 1.0);
	}
}
static int isect_segments_fl(const float v1[2], const float v2[2], const float v3[2], const float v4[2])
{
	const double div = (double)((v2[0] - v1[0]) * (v4[1] - v3[1]) - (v2[1] - v1[1]) * (v4[0] - v3[0]));
	if (div == 0.0) {
		return 1; /* co-linear */
	}
	else {
		const double labda = (double)((v1[1] - v3[1]) * (v4[0] - v3[0]) - (v1[0] - v3[0]) * (v4[1] - v3[1])) / div;
		const double mu    = (double)((v1[1] - v3[1]) * (v2[0] - v1[0]) - (v1[0] - v3[0]) * (v2[1] - v1[1])) / div;
		return (labda >= 0.0 && labda <= 1.0 && mu >= 0.0 && mu <= 1.0);
	}
}

int BLI_rcti_isect_segment(const rcti *rect, const int s1[2], const int s2[2])
{
	/* first do outside-bounds check for both points of the segment */
	if (s1[0] < rect->xmin && s2[0] < rect->xmin) return 0;
	if (s1[0] > rect->xmax && s2[0] > rect->xmax) return 0;
	if (s1[1] < rect->ymin && s2[1] < rect->ymin) return 0;
	if (s1[1] > rect->ymax && s2[1] > rect->ymax) return 0;

	/* if either points intersect then we definetly intersect */
	if (BLI_rcti_isect_pt_v(rect, s1) || BLI_rcti_isect_pt_v(rect, s2)) {
		return 1;
	}
	else {
		/* both points are outside but may insersect the rect */
		int tvec1[2];
		int tvec2[2];
		/* diagonal: [/] */
		tvec1[0] = rect->xmin; tvec1[1] = rect->ymin;
		tvec2[0] = rect->xmin; tvec2[1] = rect->ymax;
		if (isect_segments_i(s1, s2, tvec1, tvec2)) {
			return 1;
		}

		/* diagonal: [\] */
		tvec1[0] = rect->xmin; tvec1[1] = rect->ymax;
		tvec2[0] = rect->xmax; tvec2[1] = rect->ymin;
		if (isect_segments_i(s1, s2, tvec1, tvec2)) {
			return 1;
		}

		/* no intersection */
		return 0;
	}
}

int BLI_rctf_isect_segment(const rctf *rect, const float s1[2], const float s2[2])
{
	/* first do outside-bounds check for both points of the segment */
	if (s1[0] < rect->xmin && s2[0] < rect->xmin) return 0;
	if (s1[0] > rect->xmax && s2[0] > rect->xmax) return 0;
	if (s1[1] < rect->ymin && s2[1] < rect->ymin) return 0;
	if (s1[1] > rect->ymax && s2[1] > rect->ymax) return 0;

	/* if either points intersect then we definetly intersect */
	if (BLI_rctf_isect_pt_v(rect, s1) || BLI_rctf_isect_pt_v(rect, s2)) {
		return 1;
	}
	else {
		/* both points are outside but may insersect the rect */
		float tvec1[2];
		float tvec2[2];
		/* diagonal: [/] */
		tvec1[0] = rect->xmin; tvec1[1] = rect->ymin;
		tvec2[0] = rect->xmin; tvec2[1] = rect->ymax;
		if (isect_segments_fl(s1, s2, tvec1, tvec2)) {
			return 1;
		}

		/* diagonal: [\] */
		tvec1[0] = rect->xmin; tvec1[1] = rect->ymax;
		tvec2[0] = rect->xmax; tvec2[1] = rect->ymin;
		if (isect_segments_fl(s1, s2, tvec1, tvec2)) {
			return 1;
		}

		/* no intersection */
		return 0;
	}
}

void BLI_rctf_union(rctf *rct1, const rctf *rct2)
{
	if (rct1->xmin > rct2->xmin) rct1->xmin = rct2->xmin;
	if (rct1->xmax < rct2->xmax) rct1->xmax = rct2->xmax;
	if (rct1->ymin > rct2->ymin) rct1->ymin = rct2->ymin;
	if (rct1->ymax < rct2->ymax) rct1->ymax = rct2->ymax;
}

void BLI_rcti_union(rcti *rct1, const rcti *rct2)
{
	if (rct1->xmin > rct2->xmin) rct1->xmin = rct2->xmin;
	if (rct1->xmax < rct2->xmax) rct1->xmax = rct2->xmax;
	if (rct1->ymin > rct2->ymin) rct1->ymin = rct2->ymin;
	if (rct1->ymax < rct2->ymax) rct1->ymax = rct2->ymax;
}

void BLI_rctf_init(rctf *rect, float xmin, float xmax, float ymin, float ymax)
{
	if (xmin <= xmax) {
		rect->xmin = xmin;
		rect->xmax = xmax;
	}
	else {
		rect->xmax = xmin;
		rect->xmin = xmax;
	}
	if (ymin <= ymax) {
		rect->ymin = ymin;
		rect->ymax = ymax;
	}
	else {
		rect->ymax = ymin;
		rect->ymin = ymax;
	}
}

void BLI_rcti_init(rcti *rect, int xmin, int xmax, int ymin, int ymax)
{
	if (xmin <= xmax) {
		rect->xmin = xmin;
		rect->xmax = xmax;
	}
	else {
		rect->xmax = xmin;
		rect->xmin = xmax;
	}
	if (ymin <= ymax) {
		rect->ymin = ymin;
		rect->ymax = ymax;
	}
	else {
		rect->ymax = ymin;
		rect->ymin = ymax;
	}
}

void BLI_rcti_init_minmax(rcti *rect)
{
	rect->xmin = rect->ymin = INT_MAX;
	rect->xmax = rect->ymax = INT_MIN;
}

void BLI_rctf_init_minmax(rctf *rect)
{
	rect->xmin = rect->ymin =  FLT_MAX;
	rect->xmax = rect->ymax = -FLT_MAX;
}

void BLI_rcti_do_minmax_v(rcti *rect, const int xy[2])
{
	if (xy[0] < rect->xmin) rect->xmin = xy[0];
	if (xy[0] > rect->xmax) rect->xmax = xy[0];
	if (xy[1] < rect->ymin) rect->ymin = xy[1];
	if (xy[1] > rect->ymax) rect->ymax = xy[1];
}

void BLI_rctf_do_minmax_v(rctf *rect, const float xy[2])
{
	if (xy[0] < rect->xmin) rect->xmin = xy[0];
	if (xy[0] > rect->xmax) rect->xmax = xy[0];
	if (xy[1] < rect->ymin) rect->ymin = xy[1];
	if (xy[1] > rect->ymax) rect->ymax = xy[1];
}

void BLI_rcti_translate(rcti *rect, int x, int y)
{
	rect->xmin += x;
	rect->ymin += y;
	rect->xmax += x;
	rect->ymax += y;
}
void BLI_rctf_translate(rctf *rect, float x, float y)
{
	rect->xmin += x;
	rect->ymin += y;
	rect->xmax += x;
	rect->ymax += y;
}

/* change width & height around the central location */
void BLI_rcti_resize(rcti *rect, int x, int y)
{
	rect->xmin = rect->xmax = BLI_rcti_cent_x(rect);
	rect->ymin = rect->ymax = BLI_rcti_cent_y(rect);
	rect->xmin -= x / 2;
	rect->ymin -= y / 2;
	rect->xmax = rect->xmin + x;
	rect->ymax = rect->ymin + y;
}

void BLI_rctf_resize(rctf *rect, float x, float y)
{
	rect->xmin = rect->xmax = BLI_rctf_cent_x(rect);
	rect->ymin = rect->ymax = BLI_rctf_cent_y(rect);
	rect->xmin -= x * 0.5f;
	rect->ymin -= y * 0.5f;
	rect->xmax = rect->xmin + x;
	rect->ymax = rect->ymin + y;
}

void BLI_rctf_interp(rctf *rect, const rctf *rect_a, const rctf *rect_b, const float fac)
{
	const float ifac = 1.0f - fac;
	rect->xmin = (rect_a->xmin * ifac) + (rect_b->xmin * fac);
	rect->xmax = (rect_a->xmax * ifac) + (rect_b->xmax * fac);
	rect->ymin = (rect_a->ymin * ifac) + (rect_b->ymin * fac);
	rect->ymax = (rect_a->ymax * ifac) + (rect_b->ymax * fac);
}

/* BLI_rcti_interp() not needed yet */


int BLI_rctf_clamp_pt_v(const struct rctf *rect, float xy[2])
{
	int change = 0;
	if (xy[0] < rect->xmin) { xy[0] = rect->xmin; change = 1; }
	if (xy[0] > rect->xmax) { xy[0] = rect->xmax; change = 1; }
	if (xy[1] < rect->ymin) { xy[1] = rect->ymin; change = 1; }
	if (xy[1] > rect->ymax) { xy[1] = rect->ymax; change = 1; }
	return change;
}

int BLI_rcti_clamp_pt_v(const struct rcti *rect, int xy[2])
{
	int change = 0;
	if (xy[0] < rect->xmin) { xy[0] = rect->xmin; change = 1; }
	if (xy[0] > rect->xmax) { xy[0] = rect->xmax; change = 1; }
	if (xy[1] < rect->ymin) { xy[1] = rect->ymin; change = 1; }
	if (xy[1] > rect->ymax) { xy[1] = rect->ymax; change = 1; }
	return change;
}

int BLI_rctf_compare(const struct rctf *rect_a, const struct rctf *rect_b, const float limit)
{
	if (fabsf(rect_a->xmin - rect_b->xmin) < limit)
		if (fabsf(rect_a->xmax - rect_b->xmax) < limit)
			if (fabsf(rect_a->ymin - rect_b->ymin) < limit)
				if (fabsf(rect_a->ymax - rect_b->ymax) < limit)
					return 1;

	return 0;
}

int BLI_rcti_compare(const struct rcti *rect_a, const struct rcti *rect_b)
{
	if (rect_a->xmin == rect_b->xmin)
		if (rect_a->xmax == rect_b->xmax)
			if (rect_a->ymin == rect_b->ymin)
				if (rect_a->ymax == rect_b->ymax)
					return 1;

	return 0;
}

int BLI_rctf_isect(const rctf *src1, const rctf *src2, rctf *dest)
{
	float xmin, xmax;
	float ymin, ymax;

	xmin = (src1->xmin) > (src2->xmin) ? (src1->xmin) : (src2->xmin);
	xmax = (src1->xmax) < (src2->xmax) ? (src1->xmax) : (src2->xmax);
	ymin = (src1->ymin) > (src2->ymin) ? (src1->ymin) : (src2->ymin);
	ymax = (src1->ymax) < (src2->ymax) ? (src1->ymax) : (src2->ymax);

	if (xmax >= xmin && ymax >= ymin) {
		if (dest) {
			dest->xmin = xmin;
			dest->xmax = xmax;
			dest->ymin = ymin;
			dest->ymax = ymax;
		}
		return 1;
	}
	else {
		if (dest) {
			dest->xmin = 0;
			dest->xmax = 0;
			dest->ymin = 0;
			dest->ymax = 0;
		}
		return 0;
	}
}

int BLI_rcti_isect(const rcti *src1, const rcti *src2, rcti *dest)
{
	int xmin, xmax;
	int ymin, ymax;

	xmin = (src1->xmin) > (src2->xmin) ? (src1->xmin) : (src2->xmin);
	xmax = (src1->xmax) < (src2->xmax) ? (src1->xmax) : (src2->xmax);
	ymin = (src1->ymin) > (src2->ymin) ? (src1->ymin) : (src2->ymin);
	ymax = (src1->ymax) < (src2->ymax) ? (src1->ymax) : (src2->ymax);

	if (xmax >= xmin && ymax >= ymin) {
		if (dest) {
			dest->xmin = xmin;
			dest->xmax = xmax;
			dest->ymin = ymin;
			dest->ymax = ymax;
		}
		return 1;
	}
	else {
		if (dest) {
			dest->xmin = 0;
			dest->xmax = 0;
			dest->ymin = 0;
			dest->ymax = 0;
		}
		return 0;
	}
}

void BLI_rcti_rctf_copy(rcti *dst, const rctf *src)
{
	dst->xmin = floorf(src->xmin + 0.5f);
	dst->xmax = dst->xmin + floorf(BLI_rctf_size_x(src) + 0.5f);
	dst->ymin = floorf(src->ymin + 0.5f);
	dst->ymax = dst->ymin + floorf(BLI_rctf_size_y(src) + 0.5f);
}

void BLI_rctf_rcti_copy(rctf *dst, const rcti *src)
{
	dst->xmin = src->xmin;
	dst->xmax = src->xmax;
	dst->ymin = src->ymin;
	dst->ymax = src->ymax;
}

void print_rctf(const char *str, const rctf *rect)
{
	printf("%s: xmin %.3f, xmax %.3f, ymin %.3f, ymax %.3f (%.3fx%.3f)\n", str,
	       rect->xmin, rect->xmax, rect->ymin, rect->ymax, BLI_rctf_size_x(rect), BLI_rctf_size_y(rect));
}

void print_rcti(const char *str, const rcti *rect)
{
	printf("%s: xmin %d, xmax %d, ymin %d, ymax %d (%dx%d)\n", str,
	       rect->xmin, rect->xmax, rect->ymin, rect->ymax, BLI_rcti_size_x(rect), BLI_rcti_size_y(rect));
}
