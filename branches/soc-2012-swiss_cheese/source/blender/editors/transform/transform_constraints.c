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
 */

/** \file blender/editors/transform/transform_constraints.c
 *  \ingroup edtransform
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef WIN32
#include <unistd.h>
#else
#include <io.h>
#endif


#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_view3d_types.h"

#include "GPU_primitives.h"

#include "BIF_glutil.h"

#include "BKE_context.h"

#include "ED_image.h"
#include "ED_view3d.h"

#include "BLI_math.h"
#include "BLI_utildefines.h"
#include "BLI_string.h"

#include "UI_resources.h"

#include "transform.h"

static void drawObjectConstraint(TransInfo *t);

/* ************************** CONSTRAINTS ************************* */
static void constraintAutoValues(TransInfo *t, float vec[3])
{
	int mode = t->con.mode;
	if (mode & CON_APPLY) {
		float nval = (t->flag & T_NULL_ONE) ? 1.0f : 0.0f;

		if ((mode & CON_AXIS0) == 0) {
			vec[0] = nval;
		}
		if ((mode & CON_AXIS1) == 0) {
			vec[1] = nval;
		}
		if ((mode & CON_AXIS2) == 0) {
			vec[2] = nval;
		}
	}
}

void constraintNumInput(TransInfo *t, float vec[3])
{
	int mode = t->con.mode;
	if (mode & CON_APPLY) {
		float nval = (t->flag & T_NULL_ONE) ? 1.0f : 0.0f;

		if (getConstraintSpaceDimension(t) == 2) {
			int axis = mode & (CON_AXIS0 | CON_AXIS1 | CON_AXIS2);
			if (axis == (CON_AXIS0 | CON_AXIS1)) {
				/* vec[0] = vec[0]; */ /* same */
				/* vec[1] = vec[1]; */ /* same */
				vec[2] = nval;
			}
			else if (axis == (CON_AXIS1 | CON_AXIS2)) {
				vec[2] = vec[1];
				vec[1] = vec[0];
				vec[0] = nval;
			}
			else if (axis == (CON_AXIS0 | CON_AXIS2)) {
				/* vec[0] = vec[0]; */  /* same */
				vec[2] = vec[1];
				vec[1] = nval;
			}
		}
		else if (getConstraintSpaceDimension(t) == 1) {
			if (mode & CON_AXIS0) {
				/* vec[0] = vec[0]; */ /* same */
				vec[1] = nval;
				vec[2] = nval;
			}
			else if (mode & CON_AXIS1) {
				vec[1] = vec[0];
				vec[0] = nval;
				vec[2] = nval;
			}
			else if (mode & CON_AXIS2) {
				vec[2] = vec[0];
				vec[0] = nval;
				vec[1] = nval;
			}
		}
	}
}

static void postConstraintChecks(TransInfo *t, float vec[3], float pvec[3])
{
	int i = 0;

	mul_m3_v3(t->con.imtx, vec);

	snapGrid(t, vec);

	if (t->num.flag & T_NULL_ONE) {
		if (!(t->con.mode & CON_AXIS0))
			vec[0] = 1.0f;

		if (!(t->con.mode & CON_AXIS1))
			vec[1] = 1.0f;

		if (!(t->con.mode & CON_AXIS2))
			vec[2] = 1.0f;
	}

	if (hasNumInput(&t->num)) {
		applyNumInput(&t->num, vec);
		removeAspectRatio(t, vec);
		constraintNumInput(t, vec);
	}

	/* autovalues is operator param, use that directly but not if snapping is forced */
	if (t->flag & T_AUTOVALUES && (t->tsnap.status & SNAP_FORCED) == 0) {
		mul_v3_m3v3(vec, t->con.imtx, t->auto_values);
		constraintAutoValues(t, vec);
		/* inverse transformation at the end */
	}

	if (t->con.mode & CON_AXIS0) {
		pvec[i++] = vec[0];
	}
	if (t->con.mode & CON_AXIS1) {
		pvec[i++] = vec[1];
	}
	if (t->con.mode & CON_AXIS2) {
		pvec[i++] = vec[2];
	}

	mul_m3_v3(t->con.mtx, vec);
}

static void viewAxisCorrectCenter(TransInfo *t, float t_con_center[3])
{
	if (t->spacetype == SPACE_VIEW3D) {
		// View3D *v3d = t->sa->spacedata.first;
		const float min_dist = 1.0f;  /* v3d->near; */
		float dir[3];
		float l;

		sub_v3_v3v3(dir, t_con_center, t->viewinv[3]);
		if (dot_v3v3(dir, t->viewinv[2]) < 0.0f) {
			negate_v3(dir);
		}
		project_v3_v3v3(dir, dir, t->viewinv[2]);

		l = len_v3(dir);

		if (l < min_dist) {
			float diff[3];
			normalize_v3_v3(diff, t->viewinv[2]);
			mul_v3_fl(diff, min_dist - l);

			sub_v3_v3(t_con_center, diff);
		}
	}
}

static void axisProjection(TransInfo *t, float axis[3], float in[3], float out[3])
{
	float norm[3], vec[3], factor, angle;
	float t_con_center[3];

	if (in[0] == 0.0f && in[1] == 0.0f && in[2] == 0.0f)
		return;

	copy_v3_v3(t_con_center, t->con.center);

	/* checks for center being too close to the view center */
	viewAxisCorrectCenter(t, t_con_center);
	
	angle = fabsf(angle_v3v3(axis, t->viewinv[2]));
	if (angle > (float)M_PI / 2.0f) {
		angle = (float)M_PI - angle;
	}
	angle = RAD2DEGF(angle);

	/* For when view is parallel to constraint... will cause NaNs otherwise
	 * So we take vertical motion in 3D space and apply it to the
	 * constraint axis. Nice for camera grab + MMB */
	if (angle < 5.0f) {
		project_v3_v3v3(vec, in, t->viewinv[1]);
		factor = dot_v3v3(t->viewinv[1], vec) * 2.0f;
		/* since camera distance is quite relative, use quadratic relationship. holding shift can compensate */
		if (factor < 0.0f) factor *= -factor;
		else factor *= factor;

		copy_v3_v3(out, axis);
		normalize_v3(out);
		mul_v3_fl(out, -factor);  /* -factor makes move down going backwards */
	}
	else {
		float v[3], i1[3], i2[3];
		float v2[3], v4[3];
		float norm_center[3];
		float plane[3];

		getViewVector(t, t_con_center, norm_center);
		cross_v3_v3v3(plane, norm_center, axis);

		project_v3_v3v3(vec, in, plane);
		sub_v3_v3v3(vec, in, vec);
		
		add_v3_v3v3(v, vec, t_con_center);
		getViewVector(t, v, norm);

		/* give arbitrary large value if projection is impossible */
		factor = dot_v3v3(axis, norm);
		if (1.0f - fabsf(factor) < 0.0002f) {
			copy_v3_v3(out, axis);
			if (factor > 0) {
				mul_v3_fl(out, 1000000000.0f);
			}
			else {
				mul_v3_fl(out, -1000000000.0f);
			}
		}
		else {
			add_v3_v3v3(v2, t_con_center, axis);
			add_v3_v3v3(v4, v, norm);
			
			isect_line_line_v3(t_con_center, v2, v, v4, i1, i2);
			
			sub_v3_v3v3(v, i2, v);
	
			sub_v3_v3v3(out, i1, t_con_center);

			/* possible some values become nan when
			 * viewpoint and object are both zero */
			if (!finite(out[0])) out[0] = 0.0f;
			if (!finite(out[1])) out[1] = 0.0f;
			if (!finite(out[2])) out[2] = 0.0f;
		}
	}
}

static void planeProjection(TransInfo *t, float in[3], float out[3])
{
	float vec[3], factor, norm[3];

	add_v3_v3v3(vec, in, t->con.center);
	getViewVector(t, vec, norm);

	sub_v3_v3v3(vec, out, in);

	factor = dot_v3v3(vec, norm);
	if (fabsf(factor) <= 0.001f) {
		return; /* prevent divide by zero */
	}
	factor = dot_v3v3(vec, vec) / factor;

	copy_v3_v3(vec, norm);
	mul_v3_fl(vec, factor);

	add_v3_v3v3(out, in, vec);
}

/*
 * Generic callback for constant spatial constraints applied to linear motion
 *
 * The IN vector in projected into the constrained space and then further
 * projected along the view vector.
 * (in perspective mode, the view vector is relative to the position on screen)
 *
 */

static void applyAxisConstraintVec(TransInfo *t, TransData *td, float in[3], float out[3], float pvec[3])
{
	copy_v3_v3(out, in);
	if (!td && t->con.mode & CON_APPLY) {
		mul_m3_v3(t->con.pmtx, out);

		// With snap, a projection is alright, no need to correct for view alignment
		if (!(t->tsnap.mode != SCE_SNAP_MODE_INCREMENT && activeSnap(t))) {
			if (getConstraintSpaceDimension(t) == 2) {
				if (out[0] != 0.0f || out[1] != 0.0f || out[2] != 0.0f) {
					planeProjection(t, in, out);
				}
			}
			else if (getConstraintSpaceDimension(t) == 1) {
				float c[3];

				if (t->con.mode & CON_AXIS0) {
					copy_v3_v3(c, t->con.mtx[0]);
				}
				else if (t->con.mode & CON_AXIS1) {
					copy_v3_v3(c, t->con.mtx[1]);
				}
				else if (t->con.mode & CON_AXIS2) {
					copy_v3_v3(c, t->con.mtx[2]);
				}
				axisProjection(t, c, in, out);
			}
		}
		postConstraintChecks(t, out, pvec);
	}
}

/*
 * Generic callback for object based spatial constraints applied to linear motion
 *
 * At first, the following is applied to the first data in the array
 * The IN vector in projected into the constrained space and then further
 * projected along the view vector.
 * (in perspective mode, the view vector is relative to the position on screen)
 *
 * Further down, that vector is mapped to each data's space.
 */

static void applyObjectConstraintVec(TransInfo *t, TransData *td, float in[3], float out[3], float pvec[3])
{
	copy_v3_v3(out, in);
	if (t->con.mode & CON_APPLY) {
		if (!td) {
			mul_m3_v3(t->con.pmtx, out);
			if (getConstraintSpaceDimension(t) == 2) {
				if (out[0] != 0.0f || out[1] != 0.0f || out[2] != 0.0f) {
					planeProjection(t, in, out);
				}
			}
			else if (getConstraintSpaceDimension(t) == 1) {
				float c[3];

				if (t->con.mode & CON_AXIS0) {
					copy_v3_v3(c, t->con.mtx[0]);
				}
				else if (t->con.mode & CON_AXIS1) {
					copy_v3_v3(c, t->con.mtx[1]);
				}
				else if (t->con.mode & CON_AXIS2) {
					copy_v3_v3(c, t->con.mtx[2]);
				}
				axisProjection(t, c, in, out);
			}
			postConstraintChecks(t, out, pvec);
			copy_v3_v3(out, pvec);
		}
		else {
			int i = 0;

			out[0] = out[1] = out[2] = 0.0f;
			if (t->con.mode & CON_AXIS0) {
				out[0] = in[i++];
			}
			if (t->con.mode & CON_AXIS1) {
				out[1] = in[i++];
			}
			if (t->con.mode & CON_AXIS2) {
				out[2] = in[i++];
			}
			mul_m3_v3(td->axismtx, out);
		}
	}
}

/*
 * Generic callback for constant spatial constraints applied to resize motion
 */

static void applyAxisConstraintSize(TransInfo *t, TransData *td, float smat[3][3])
{
	if (!td && t->con.mode & CON_APPLY) {
		float tmat[3][3];

		if (!(t->con.mode & CON_AXIS0)) {
			smat[0][0] = 1.0f;
		}
		if (!(t->con.mode & CON_AXIS1)) {
			smat[1][1] = 1.0f;
		}
		if (!(t->con.mode & CON_AXIS2)) {
			smat[2][2] = 1.0f;
		}

		mul_m3_m3m3(tmat, smat, t->con.imtx);
		mul_m3_m3m3(smat, t->con.mtx, tmat);
	}
}

/*
 * Callback for object based spatial constraints applied to resize motion
 */

static void applyObjectConstraintSize(TransInfo *t, TransData *td, float smat[3][3])
{
	if (td && t->con.mode & CON_APPLY) {
		float tmat[3][3];
		float imat[3][3];

		invert_m3_m3(imat, td->axismtx);

		if (!(t->con.mode & CON_AXIS0)) {
			smat[0][0] = 1.0f;
		}
		if (!(t->con.mode & CON_AXIS1)) {
			smat[1][1] = 1.0f;
		}
		if (!(t->con.mode & CON_AXIS2)) {
			smat[2][2] = 1.0f;
		}

		mul_m3_m3m3(tmat, smat, imat);
		mul_m3_m3m3(smat, td->axismtx, tmat);
	}
}

/*
 * Generic callback for constant spatial constraints applied to rotations
 *
 * The rotation axis is copied into VEC.
 *
 * In the case of single axis constraints, the rotation axis is directly the one constrained to.
 * For planar constraints (2 axis), the rotation axis is the normal of the plane.
 *
 * The following only applies when CON_NOFLIP is not set.
 * The vector is then modified to always point away from the screen (in global space)
 * This insures that the rotation is always logically following the mouse.
 * (ie: not doing counterclockwise rotations when the mouse moves clockwise).
 */

static void applyAxisConstraintRot(TransInfo *t, TransData *td, float vec[3], float *angle)
{
	if (!td && t->con.mode & CON_APPLY) {
		int mode = t->con.mode & (CON_AXIS0 | CON_AXIS1 | CON_AXIS2);

		switch (mode) {
			case CON_AXIS0:
			case (CON_AXIS1 | CON_AXIS2):
				copy_v3_v3(vec, t->con.mtx[0]);
				break;
			case CON_AXIS1:
			case (CON_AXIS0 | CON_AXIS2):
				copy_v3_v3(vec, t->con.mtx[1]);
				break;
			case CON_AXIS2:
			case (CON_AXIS0 | CON_AXIS1):
				copy_v3_v3(vec, t->con.mtx[2]);
				break;
		}
		/* don't flip axis if asked to or if num input */
		if (angle && (mode & CON_NOFLIP) == 0 && hasNumInput(&t->num) == 0) {
			if (dot_v3v3(vec, t->viewinv[2]) > 0.0f) {
				*angle = -(*angle);
			}
		}
	}
}

/*
 * Callback for object based spatial constraints applied to rotations
 *
 * The rotation axis is copied into VEC.
 *
 * In the case of single axis constraints, the rotation axis is directly the one constrained to.
 * For planar constraints (2 axis), the rotation axis is the normal of the plane.
 *
 * The following only applies when CON_NOFLIP is not set.
 * The vector is then modified to always point away from the screen (in global space)
 * This insures that the rotation is always logically following the mouse.
 * (ie: not doing counterclockwise rotations when the mouse moves clockwise).
 */

static void applyObjectConstraintRot(TransInfo *t, TransData *td, float vec[3], float *angle)
{
	if (t->con.mode & CON_APPLY) {
		int mode = t->con.mode & (CON_AXIS0 | CON_AXIS1 | CON_AXIS2);

		/* on setup call, use first object */
		if (td == NULL) {
			td = t->data;
		}

		switch (mode) {
			case CON_AXIS0:
			case (CON_AXIS1 | CON_AXIS2):
				copy_v3_v3(vec, td->axismtx[0]);
				break;
			case CON_AXIS1:
			case (CON_AXIS0 | CON_AXIS2):
				copy_v3_v3(vec, td->axismtx[1]);
				break;
			case CON_AXIS2:
			case (CON_AXIS0 | CON_AXIS1):
				copy_v3_v3(vec, td->axismtx[2]);
				break;
		}
		if (angle && (mode & CON_NOFLIP) == 0 && hasNumInput(&t->num) == 0) {
			if (dot_v3v3(vec, t->viewinv[2]) > 0.0f) {
				*angle = -(*angle);
			}
		}
	}
}

/*--------------------- INTERNAL SETUP CALLS ------------------*/

void setConstraint(TransInfo *t, float space[3][3], int mode, const char text[])
{
	BLI_strncpy(t->con.text + 1, text, sizeof(t->con.text) - 1);
	copy_m3_m3(t->con.mtx, space);
	t->con.mode = mode;
	getConstraintMatrix(t);

	startConstraint(t);

	t->con.drawExtra = NULL;
	t->con.applyVec = applyAxisConstraintVec;
	t->con.applySize = applyAxisConstraintSize;
	t->con.applyRot = applyAxisConstraintRot;
	t->redraw = 1;
}

void setLocalConstraint(TransInfo *t, int mode, const char text[])
{
	if (t->flag & T_EDIT) {
		float obmat[3][3];
		copy_m3_m4(obmat, t->scene->obedit->obmat);
		normalize_m3(obmat);
		setConstraint(t, obmat, mode, text);
	}
	else {
		if (t->total == 1) {
			setConstraint(t, t->data->axismtx, mode, text);
		}
		else {
			BLI_strncpy(t->con.text + 1, text, sizeof(t->con.text) - 1);
			copy_m3_m3(t->con.mtx, t->data->axismtx);
			t->con.mode = mode;
			getConstraintMatrix(t);

			startConstraint(t);

			t->con.drawExtra = drawObjectConstraint;
			t->con.applyVec = applyObjectConstraintVec;
			t->con.applySize = applyObjectConstraintSize;
			t->con.applyRot = applyObjectConstraintRot;
			t->redraw = 1;
		}
	}
}

/*
 * Set the constraint according to the user defined orientation
 *
 * ftext is a format string passed to BLI_snprintf. It will add the name of
 * the orientation where %s is (logically).
 */
void setUserConstraint(TransInfo *t, short orientation, int mode, const char ftext[])
{
	char text[40];

	switch (orientation) {
		case V3D_MANIP_GLOBAL:
		{
			float mtx[3][3] = MAT3_UNITY;
			BLI_snprintf(text, sizeof(text), ftext, "global");
			setConstraint(t, mtx, mode, text);
		}
		break;
		case V3D_MANIP_LOCAL:
			BLI_snprintf(text, sizeof(text), ftext, "local");
			setLocalConstraint(t, mode, text);
			break;
		case V3D_MANIP_NORMAL:
			BLI_snprintf(text, sizeof(text), ftext, "normal");
			setConstraint(t, t->spacemtx, mode, text);
			break;
		case V3D_MANIP_VIEW:
			BLI_snprintf(text, sizeof(text), ftext, "view");
			setConstraint(t, t->spacemtx, mode, text);
			break;
		case V3D_MANIP_GIMBAL:
			BLI_snprintf(text, sizeof(text), ftext, "gimbal");
			setConstraint(t, t->spacemtx, mode, text);
			break;
		default: /* V3D_MANIP_CUSTOM */
			BLI_snprintf(text, sizeof(text), ftext, t->spacename);
			setConstraint(t, t->spacemtx, mode, text);
			break;
	}

	t->con.orientation = orientation;

	t->con.mode |= CON_USER;
}

/* called from drawview.c, as an extra per-window draw option */
void drawPropCircle(const struct bContext *C, TransInfo *t)
{
	if (t->flag & T_PROP_EDIT) {
		RegionView3D *rv3d = CTX_wm_region_view3d(C);
		float tmat[4][4], imat[4][4];
		float center[3];

		UI_ThemeColor(TH_GRID);

		if (t->spacetype == SPACE_VIEW3D && rv3d != NULL) {
			copy_m4_m4(tmat, rv3d->viewmat);
			invert_m4_m4(imat, tmat);
		}
		else {
			unit_m4(tmat);
			unit_m4(imat);
		}

		gpuPushMatrix();

		copy_v3_v3(center, t->center);

		if ((t->spacetype == SPACE_VIEW3D) && t->obedit) {
			mul_m4_v3(t->obedit->obmat, center); /* because t->center is in local space */
		}
		else if (t->spacetype == SPACE_IMAGE) {
			float aspx, aspy;

			if (t->options & CTX_MASK) {
				/* untested - mask aspect is TODO */
				ED_space_image_get_aspect(t->sa->spacedata.first, &aspx, &aspy);
			}
			else {
				ED_space_image_get_uv_aspect(t->sa->spacedata.first, &aspx, &aspy);
			}
			gpuScale(1.0f / aspx, 1.0f / aspy, 1.0);
		}

		set_inverted_drawing(1);
		gpuSingleFastBall(GL_LINE_LOOP, center, t->prop_size, imat);
		set_inverted_drawing(0);

		gpuPopMatrix();
	}
}

static void drawObjectConstraint(TransInfo *t)
{
	int i;
	TransData *td = t->data;

	gpuImmediateFormat_C4_V3();

	gpuBegin(GL_LINES);

	/* Draw the first one lighter because that's the one who controls the others.
	 * Meaning the transformation is projected on that one and just copied on the others
	 * constraint space.
	 * In a nutshell, the object with light axis is controlled by the user and the others follow.
	 * Without drawing the first light, users have little clue what they are doing.
	 */
	if (t->con.mode & CON_AXIS0) {
		drawConstraintLine(t, td->ob->obmat[3], td->axismtx[0], 'X', DRAWLIGHT);
	}

	if (t->con.mode & CON_AXIS1) {
		drawConstraintLine(t, td->ob->obmat[3], td->axismtx[1], 'Y', DRAWLIGHT);
	}

	if (t->con.mode & CON_AXIS2) {
		drawConstraintLine(t, td->ob->obmat[3], td->axismtx[2], 'Z', DRAWLIGHT);
	}

	td++;

	for (i = 1; i < t->total; i++, td++) {
		if (t->con.mode & CON_AXIS0) {
			drawConstraintLine(t, td->ob->obmat[3], td->axismtx[0], 'X', 0);
		}

		if (t->con.mode & CON_AXIS1) {
			drawConstraintLine(t, td->ob->obmat[3], td->axismtx[1], 'Y', 0);
		}

		if (t->con.mode & CON_AXIS2) {
			drawConstraintLine(t, td->ob->obmat[3], td->axismtx[2], 'Z', 0);
		}
	}

	gpuEnd();

	gpuImmediateUnformat();
}

/*--------------------- START / STOP CONSTRAINTS ---------------------- */

void startConstraint(TransInfo *t)
{
	t->con.mode |= CON_APPLY;
	*t->con.text = ' ';
	t->num.idx_max = MIN2(getConstraintSpaceDimension(t) - 1, t->idx_max);
}

void stopConstraint(TransInfo *t)
{
	t->con.mode &= ~(CON_APPLY | CON_SELECT);
	*t->con.text = '\0';
	t->num.idx_max = t->idx_max;
}

void getConstraintMatrix(TransInfo *t)
{
	float mat[3][3];
	invert_m3_m3(t->con.imtx, t->con.mtx);
	unit_m3(t->con.pmtx);

	if (!(t->con.mode & CON_AXIS0)) {
		t->con.pmtx[0][0]       =
		    t->con.pmtx[0][1]   =
		    t->con.pmtx[0][2]   = 0.0f;
	}

	if (!(t->con.mode & CON_AXIS1)) {
		t->con.pmtx[1][0]       =
		    t->con.pmtx[1][1]   =
		    t->con.pmtx[1][2]   = 0.0f;
	}

	if (!(t->con.mode & CON_AXIS2)) {
		t->con.pmtx[2][0]       =
		    t->con.pmtx[2][1]   =
		    t->con.pmtx[2][2]   = 0.0f;
	}

	mul_m3_m3m3(mat, t->con.pmtx, t->con.imtx);
	mul_m3_m3m3(t->con.pmtx, t->con.mtx, mat);
}

/*------------------------- MMB Select -------------------------------*/

void initSelectConstraint(TransInfo *t, float mtx[3][3])
{
	copy_m3_m3(t->con.mtx, mtx);
	t->con.mode |= CON_APPLY;
	t->con.mode |= CON_SELECT;

	setNearestAxis(t);
	t->con.drawExtra = NULL;
	t->con.applyVec = applyAxisConstraintVec;
	t->con.applySize = applyAxisConstraintSize;
	t->con.applyRot = applyAxisConstraintRot;
}

void selectConstraint(TransInfo *t)
{
	if (t->con.mode & CON_SELECT) {
		setNearestAxis(t);
		startConstraint(t);
	}
}

void postSelectConstraint(TransInfo *t)
{
	if (!(t->con.mode & CON_SELECT))
		return;

	t->con.mode &= ~CON_AXIS0;
	t->con.mode &= ~CON_AXIS1;
	t->con.mode &= ~CON_AXIS2;
	t->con.mode &= ~CON_SELECT;

	setNearestAxis(t);

	startConstraint(t);
	t->redraw = 1;
}

static void setNearestAxis2d(TransInfo *t)
{
	/* no correction needed... just use whichever one is lower */
	if (abs(t->mval[0] - t->con.imval[0]) < abs(t->mval[1] - t->con.imval[1]) ) {
		t->con.mode |= CON_AXIS1;
		BLI_snprintf(t->con.text, sizeof(t->con.text), " along Y axis");
	}
	else {
		t->con.mode |= CON_AXIS0;
		BLI_snprintf(t->con.text, sizeof(t->con.text), " along X axis");
	}
}

static void setNearestAxis3d(TransInfo *t)
{
	float zfac;
	float mvec[3], axis[3], proj[3];
	float len[3];
	int i, icoord[2];

	/* calculate mouse movement */
	mvec[0] = (float)(t->mval[0] - t->con.imval[0]);
	mvec[1] = (float)(t->mval[1] - t->con.imval[1]);
	mvec[2] = 0.0f;

	/* we need to correct axis length for the current zoomlevel of view,
	 * this to prevent projected values to be clipped behind the camera
	 * and to overflow the short integers.
	 * The formula used is a bit stupid, just a simplification of the subtraction
	 * of two 2D points 30 pixels apart (that's the last factor in the formula) after
	 * projecting them with window_to_3d_delta and then get the length of that vector.
	 */
	zfac = t->persmat[0][3] * t->center[0] + t->persmat[1][3] * t->center[1] + t->persmat[2][3] * t->center[2] + t->persmat[3][3];
	zfac = len_v3(t->persinv[0]) * 2.0f / t->ar->winx * zfac * 30.0f;

	for (i = 0; i < 3; i++) {
		copy_v3_v3(axis, t->con.mtx[i]);

		mul_v3_fl(axis, zfac);
		/* now we can project to get window coordinate */
		add_v3_v3(axis, t->con.center);
		projectIntView(t, axis, icoord);

		axis[0] = (float)(icoord[0] - t->center2d[0]);
		axis[1] = (float)(icoord[1] - t->center2d[1]);
		axis[2] = 0.0f;

		if (normalize_v3(axis) != 0.0f) {
			project_v3_v3v3(proj, mvec, axis);
			sub_v3_v3v3(axis, mvec, proj);
			len[i] = normalize_v3(axis);
		}
		else {
			len[i] = 10000000000.0f;
		}
	}

	if (len[0] <= len[1] && len[0] <= len[2]) {
		if (t->modifiers & MOD_CONSTRAINT_PLANE) {
			t->con.mode |= (CON_AXIS1 | CON_AXIS2);
			BLI_snprintf(t->con.text, sizeof(t->con.text), " locking %s X axis", t->spacename);
		}
		else {
			t->con.mode |= CON_AXIS0;
			BLI_snprintf(t->con.text, sizeof(t->con.text), " along %s X axis", t->spacename);
		}
	}
	else if (len[1] <= len[0] && len[1] <= len[2]) {
		if (t->modifiers & MOD_CONSTRAINT_PLANE) {
			t->con.mode |= (CON_AXIS0 | CON_AXIS2);
			BLI_snprintf(t->con.text, sizeof(t->con.text), " locking %s Y axis", t->spacename);
		}
		else {
			t->con.mode |= CON_AXIS1;
			BLI_snprintf(t->con.text, sizeof(t->con.text), " along %s Y axis", t->spacename);
		}
	}
	else if (len[2] <= len[1] && len[2] <= len[0]) {
		if (t->modifiers & MOD_CONSTRAINT_PLANE) {
			t->con.mode |= (CON_AXIS0 | CON_AXIS1);
			BLI_snprintf(t->con.text, sizeof(t->con.text), " locking %s Z axis", t->spacename);
		}
		else {
			t->con.mode |= CON_AXIS2;
			BLI_snprintf(t->con.text, sizeof(t->con.text), " along %s Z axis", t->spacename);
		}
	}
}

void setNearestAxis(TransInfo *t)
{
	/* clear any prior constraint flags */
	t->con.mode &= ~CON_AXIS0;
	t->con.mode &= ~CON_AXIS1;
	t->con.mode &= ~CON_AXIS2;

	/* constraint setting - depends on spacetype */
	if (t->spacetype == SPACE_VIEW3D) {
		/* 3d-view */
		setNearestAxis3d(t);
	}
	else {
		/* assume that this means a 2D-Editor */
		setNearestAxis2d(t);
	}

	getConstraintMatrix(t);
}

/*-------------- HELPER FUNCTIONS ----------------*/

char constraintModeToChar(TransInfo *t)
{
	if ((t->con.mode & CON_APPLY) == 0) {
		return '\0';
	}
	switch (t->con.mode & (CON_AXIS0 | CON_AXIS1 | CON_AXIS2)) {
		case (CON_AXIS0):
		case (CON_AXIS1 | CON_AXIS2):
			return 'X';
		case (CON_AXIS1):
		case (CON_AXIS0 | CON_AXIS2):
			return 'Y';
		case (CON_AXIS2):
		case (CON_AXIS0 | CON_AXIS1):
			return 'Z';
		default:
			return '\0';
	}
}


int isLockConstraint(TransInfo *t)
{
	int mode = t->con.mode;

	if ((mode & (CON_AXIS0 | CON_AXIS1)) == (CON_AXIS0 | CON_AXIS1))
		return 1;

	if ((mode & (CON_AXIS1 | CON_AXIS2)) == (CON_AXIS1 | CON_AXIS2))
		return 1;

	if ((mode & (CON_AXIS0 | CON_AXIS2)) == (CON_AXIS0 | CON_AXIS2))
		return 1;

	return 0;
}

/*
 * Returns the dimension of the constraint space.
 *
 * For that reason, the flags always needs to be set to properly evaluate here,
 * even if they aren't actually used in the callback function. (Which could happen
 * for weird constraints not yet designed. Along a path for example.)
 */

int getConstraintSpaceDimension(TransInfo *t)
{
	int n = 0;

	if (t->con.mode & CON_AXIS0)
		n++;

	if (t->con.mode & CON_AXIS1)
		n++;

	if (t->con.mode & CON_AXIS2)
		n++;

	return n;
/*
 * Someone willing to do it cryptically could do the following instead:
 *
 * return t->con & (CON_AXIS0|CON_AXIS1|CON_AXIS2);
 *
 * Based on the assumptions that the axis flags are one after the other and start at 1
 */
}
