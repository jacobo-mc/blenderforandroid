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
 * The Original Code is Copyright (C) 2005 Blender Foundation
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/transform/transform_manipulator.c
 *  \ingroup edtransform
 */


#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

#ifndef WIN32
#include <unistd.h>
#else
#include <io.h>
#endif

#include "MEM_guardedalloc.h"

#include "DNA_armature_types.h"
#include "DNA_curve_types.h"
#include "DNA_lattice_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meta_types.h"
#include "DNA_screen_types.h"
#include "DNA_scene_types.h"
#include "DNA_view3d_types.h"

#include "BLI_math.h"
#include "BLI_utildefines.h"

#include "RNA_access.h"

#include "BKE_action.h"
#include "BKE_context.h"
#include "BKE_curve.h"
#include "BKE_global.h"
#include "BKE_mesh.h"
#include "BKE_particle.h"
#include "BKE_pointcache.h"
#include "BKE_tessmesh.h"

#include "GPU_primitives.h"


#include "WM_api.h"
#include "WM_types.h"

#include "ED_armature.h"
#include "ED_curve.h"
#include "ED_mesh.h"
#include "ED_particle.h"
#include "ED_view3d.h"

#include "UI_resources.h"

/* local module include */
#include "transform.h"

/* return codes for select, and drawing flags */

#define MAN_TRANS_X		(1 << 0)
#define MAN_TRANS_Y		(1 << 1)
#define MAN_TRANS_Z		(1 << 2)
#define MAN_TRANS_C		(MAN_TRANS_X | MAN_TRANS_Y | MAN_TRANS_Z)

#define MAN_ROT_X		(1 << 3)
#define MAN_ROT_Y		(1 << 4)
#define MAN_ROT_Z		(1 << 5)
#define MAN_ROT_V		(1 << 6)
#define MAN_ROT_T		(1 << 7)
#define MAN_ROT_C		(MAN_ROT_X | MAN_ROT_Y | MAN_ROT_Z | MAN_ROT_V | MAN_ROT_T)

#define MAN_SCALE_X		(1 << 8)
#define MAN_SCALE_Y		(1 << 9)
#define MAN_SCALE_Z		(1 << 10)
#define MAN_SCALE_C		(MAN_SCALE_X | MAN_SCALE_Y | MAN_SCALE_Z)

/* color codes */

#define MAN_RGB     0
#define MAN_GHOST   1
#define MAN_MOVECOL 2

/* transform widget center calc helper for below */
static void calc_tw_center(Scene *scene, const float co[3])
{
	float *twcent = scene->twcent;
	float *min = scene->twmin;
	float *max = scene->twmax;

	minmax_v3v3_v3(min, max, co);
	add_v3_v3(twcent, co);
}

static void protectflag_to_drawflags(short protectflag, short *drawflags)
{
	if (protectflag & OB_LOCK_LOCX)
		*drawflags &= ~MAN_TRANS_X;
	if (protectflag & OB_LOCK_LOCY)
		*drawflags &= ~MAN_TRANS_Y;
	if (protectflag & OB_LOCK_LOCZ)
		*drawflags &= ~MAN_TRANS_Z;

	if (protectflag & OB_LOCK_ROTX)
		*drawflags &= ~MAN_ROT_X;
	if (protectflag & OB_LOCK_ROTY)
		*drawflags &= ~MAN_ROT_Y;
	if (protectflag & OB_LOCK_ROTZ)
		*drawflags &= ~MAN_ROT_Z;

	if (protectflag & OB_LOCK_SCALEX)
		*drawflags &= ~MAN_SCALE_X;
	if (protectflag & OB_LOCK_SCALEY)
		*drawflags &= ~MAN_SCALE_Y;
	if (protectflag & OB_LOCK_SCALEZ)
		*drawflags &= ~MAN_SCALE_Z;
}

/* for pose mode */
static void stats_pose(Scene *scene, RegionView3D *rv3d, bPoseChannel *pchan)
{
	Bone *bone = pchan->bone;

	if (bone) {
		if (bone->flag & BONE_TRANSFORM) {
			calc_tw_center(scene, pchan->pose_head);
			protectflag_to_drawflags(pchan->protectflag, &rv3d->twdrawflag);
		}
	}
}

/* for editmode*/
static void stats_editbone(RegionView3D *rv3d, EditBone *ebo)
{
	if (ebo->flag & BONE_EDITMODE_LOCKED)
		protectflag_to_drawflags(OB_LOCK_LOC | OB_LOCK_ROT | OB_LOCK_SCALE, &rv3d->twdrawflag);
}

/* could move into BLI_math however this is only useful for display/editing purposes */
static void axis_angle_to_gimbal_axis(float gmat[3][3], float axis[3], float angle)
{
	/* X/Y are arbitrary axies, most importantly Z is the axis of rotation */

	float cross_vec[3];
	float quat[4];

	/* this is an un-scientific method to get a vector to cross with
	 * XYZ intentionally YZX */
	cross_vec[0] = axis[1];
	cross_vec[1] = axis[2];
	cross_vec[2] = axis[0];

	/* X-axis */
	cross_v3_v3v3(gmat[0], cross_vec, axis);
	normalize_v3(gmat[0]);
	axis_angle_to_quat(quat, axis, angle);
	mul_qt_v3(quat, gmat[0]);

	/* Y-axis */
	axis_angle_to_quat(quat, axis, M_PI / 2.0);
	copy_v3_v3(gmat[1], gmat[0]);
	mul_qt_v3(quat, gmat[1]);

	/* Z-axis */
	copy_v3_v3(gmat[2], axis);

	normalize_m3(gmat);
}


static int test_rotmode_euler(short rotmode)
{
	return (ELEM(rotmode, ROT_MODE_AXISANGLE, ROT_MODE_QUAT)) ? 0 : 1;
}

int gimbal_axis(Object *ob, float gmat[][3])
{
	if (ob) {
		if (ob->mode & OB_MODE_POSE) {
			bPoseChannel *pchan = BKE_pose_channel_active(ob);

			if (pchan) {
				float mat[3][3], tmat[3][3], obmat[3][3];
				if (test_rotmode_euler(pchan->rotmode)) {
					eulO_to_gimbal_axis(mat, pchan->eul, pchan->rotmode);
				}
				else if (pchan->rotmode == ROT_MODE_AXISANGLE) {
					axis_angle_to_gimbal_axis(mat, pchan->rotAxis, pchan->rotAngle);
				}
				else { /* quat */
					return 0;
				}


				/* apply bone transformation */
				mul_m3_m3m3(tmat, pchan->bone->bone_mat, mat);

				if (pchan->parent) {
					float parent_mat[3][3];

					copy_m3_m4(parent_mat, pchan->parent->pose_mat);
					mul_m3_m3m3(mat, parent_mat, tmat);

					/* needed if object transformation isn't identity */
					copy_m3_m4(obmat, ob->obmat);
					mul_m3_m3m3(gmat, obmat, mat);
				}
				else {
					/* needed if object transformation isn't identity */
					copy_m3_m4(obmat, ob->obmat);
					mul_m3_m3m3(gmat, obmat, tmat);
				}

				normalize_m3(gmat);
				return 1;
			}
		}
		else {
			if (test_rotmode_euler(ob->rotmode)) {
				eulO_to_gimbal_axis(gmat, ob->rot, ob->rotmode);
			}
			else if (ob->rotmode == ROT_MODE_AXISANGLE) {
				axis_angle_to_gimbal_axis(gmat, ob->rotAxis, ob->rotAngle);
			}
			else { /* quat */
				return 0;
			}

			if (ob->parent) {
				float parent_mat[3][3];
				copy_m3_m4(parent_mat, ob->parent->obmat);
				normalize_m3(parent_mat);
				mul_m3_m3m3(gmat, parent_mat, gmat);
			}
			return 1;
		}
	}

	return 0;
}


/* centroid, boundbox, of selection */
/* returns total items selected */
int calc_manipulator_stats(const bContext *C)
{
	ScrArea *sa = CTX_wm_area(C);
	ARegion *ar = CTX_wm_region(C);
	Scene *scene = CTX_data_scene(C);
	Object *obedit = CTX_data_edit_object(C);
	ToolSettings *ts = CTX_data_tool_settings(C);
	View3D *v3d = sa->spacedata.first;
	RegionView3D *rv3d = ar->regiondata;
	Base *base;
	Object *ob = OBACT;
	int a, totsel = 0;

	/* transform widget matrix */
	unit_m4(rv3d->twmat);

	rv3d->twdrawflag = 0xFFFF;

	/* transform widget centroid/center */
	INIT_MINMAX(scene->twmin, scene->twmax);
	zero_v3(scene->twcent);

	if (obedit) {
		ob = obedit;
		if ((ob->lay & v3d->lay) == 0) return 0;

		if (obedit->type == OB_MESH) {
			BMEditMesh *em = BMEdit_FromObject(obedit);
			BMEditSelection ese;
			float vec[3] = {0, 0, 0};

			/* USE LAST SELECTE WITH ACTIVE */
			if ((v3d->around == V3D_ACTIVE) && BM_select_history_active_get(em->bm, &ese)) {
				BM_editselection_center(&ese, vec);
				calc_tw_center(scene, vec);
				totsel = 1;
			}
			else {
				BMesh *bm = em->bm;
				BMVert *eve;

				BMIter iter;

				/* do vertices/edges/faces for center depending on selection
				 * mode. note we can't use just vertex selection flag because
				 * it is not flush down on changes */
				if (ts->selectmode & SCE_SELECT_VERTEX) {
					BM_ITER_MESH (eve, &iter, bm, BM_VERTS_OF_MESH) {
						if (!BM_elem_flag_test(eve, BM_ELEM_HIDDEN)) {
							if (BM_elem_flag_test(eve, BM_ELEM_SELECT)) {
								totsel++;
								calc_tw_center(scene, eve->co);
							}
						}
					}
				}
				else if (ts->selectmode & SCE_SELECT_EDGE) {
					BMIter itersub;
					BMEdge *eed;
					BM_ITER_MESH (eve, &iter, bm, BM_VERTS_OF_MESH) {
						if (!BM_elem_flag_test(eve, BM_ELEM_HIDDEN)) {
							/* check the vertex has a selected edge, only add it once */
							BM_ITER_ELEM (eed, &itersub, eve, BM_EDGES_OF_VERT) {
								if (BM_elem_flag_test(eed, BM_ELEM_SELECT)) {
									totsel++;
									calc_tw_center(scene, eve->co);
									break;
								}
							}
						}
					}
				}
				else {
					BMIter itersub;
					BMFace *efa;
					BM_ITER_MESH (eve, &iter, bm, BM_VERTS_OF_MESH) {
						if (!BM_elem_flag_test(eve, BM_ELEM_HIDDEN)) {
							/* check the vertex has a selected face, only add it once */
							BM_ITER_ELEM (efa, &itersub, eve, BM_FACES_OF_VERT) {
								if (BM_elem_flag_test(efa, BM_ELEM_SELECT)) {
									totsel++;
									calc_tw_center(scene, eve->co);
									break;
								}
							}
						}
					}
				}
			}
		} /* end editmesh */
		else if (obedit->type == OB_ARMATURE) {
			bArmature *arm = obedit->data;
			EditBone *ebo;
			for (ebo = arm->edbo->first; ebo; ebo = ebo->next) {
				if (EBONE_VISIBLE(arm, ebo)) {
					if (ebo->flag & BONE_TIPSEL) {
						calc_tw_center(scene, ebo->tail);
						totsel++;
					}
					if (ebo->flag & BONE_ROOTSEL) {
						calc_tw_center(scene, ebo->head);
						totsel++;
					}
					if (ebo->flag & BONE_SELECTED) {
						stats_editbone(rv3d, ebo);
					}
				}
			}
		}
		else if (ELEM(obedit->type, OB_CURVE, OB_SURF)) {
			Curve *cu = obedit->data;
			float center[3];

			if (v3d->around == V3D_ACTIVE && ED_curve_actSelection(cu, center)) {
				calc_tw_center(scene, center);
				totsel++;
			}
			else {
				Nurb *nu;
				BezTriple *bezt;
				BPoint *bp;
				ListBase *nurbs = BKE_curve_editNurbs_get(cu);

				nu = nurbs->first;
				while (nu) {
					if (nu->type == CU_BEZIER) {
						bezt = nu->bezt;
						a = nu->pntsu;
						while (a--) {
							/* exceptions
							 * if handles are hidden then only check the center points.
							 * If the center knot is selected then only use this as the center point.
							 */
							if (cu->drawflag & CU_HIDE_HANDLES) {
								if (bezt->f2 & SELECT) {
									calc_tw_center(scene, bezt->vec[1]);
									totsel++;
								}
							}
							else if (bezt->f2 & SELECT) {
								calc_tw_center(scene, bezt->vec[1]);
								totsel++;
							}
							else {
								if (bezt->f1) {
									calc_tw_center(scene, bezt->vec[0]);
									totsel++;
								}
								if (bezt->f3) {
									calc_tw_center(scene, bezt->vec[2]);
									totsel++;
								}
							}
							bezt++;
						}
					}
					else {
						bp = nu->bp;
						a = nu->pntsu * nu->pntsv;
						while (a--) {
							if (bp->f1 & SELECT) {
								calc_tw_center(scene, bp->vec);
								totsel++;
							}
							bp++;
						}
					}
					nu = nu->next;
				}
			}
		}
		else if (obedit->type == OB_MBALL) {
			MetaBall *mb = (MetaBall *)obedit->data;
			MetaElem *ml /* , *ml_sel = NULL */ /* UNUSED */;

			ml = mb->editelems->first;
			while (ml) {
				if (ml->flag & SELECT) {
					calc_tw_center(scene, &ml->x);
					/* ml_sel = ml; */ /* UNUSED */
					totsel++;
				}
				ml = ml->next;
			}
		}
		else if (obedit->type == OB_LATTICE) {
			BPoint *bp;
			Lattice *lt = obedit->data;

			bp = lt->editlatt->latt->def;

			a = lt->editlatt->latt->pntsu * lt->editlatt->latt->pntsv * lt->editlatt->latt->pntsw;
			while (a--) {
				if (bp->f1 & SELECT) {
					calc_tw_center(scene, bp->vec);
					totsel++;
				}
				bp++;
			}
		}

		/* selection center */
		if (totsel) {
			mul_v3_fl(scene->twcent, 1.0f / (float)totsel);   // centroid!
			mul_m4_v3(obedit->obmat, scene->twcent);
			mul_m4_v3(obedit->obmat, scene->twmin);
			mul_m4_v3(obedit->obmat, scene->twmax);
		}
	}
	else if (ob && (ob->mode & OB_MODE_POSE)) {
		bPoseChannel *pchan;
		int mode = TFM_ROTATION; // mislead counting bones... bah. We don't know the manipulator mode, could be mixed

		if ((ob->lay & v3d->lay) == 0) return 0;

		totsel = count_set_pose_transflags(&mode, 0, ob);

		if (totsel) {
			/* use channels to get stats */
			for (pchan = ob->pose->chanbase.first; pchan; pchan = pchan->next) {
				stats_pose(scene, rv3d, pchan);
			}

			mul_v3_fl(scene->twcent, 1.0f / (float)totsel);   // centroid!
			mul_m4_v3(ob->obmat, scene->twcent);
			mul_m4_v3(ob->obmat, scene->twmin);
			mul_m4_v3(ob->obmat, scene->twmax);
		}
	}
	else if (ob && (ob->mode & OB_MODE_ALL_PAINT)) {
		/* pass */
	}
	else if (ob && ob->mode & OB_MODE_PARTICLE_EDIT) {
		PTCacheEdit *edit = PE_get_current(scene, ob);
		PTCacheEditPoint *point;
		PTCacheEditKey *ek;
		int k;

		if (edit) {
			point = edit->points;
			for (a = 0; a < edit->totpoint; a++, point++) {
				if (point->flag & PEP_HIDE) continue;

				for (k = 0, ek = point->keys; k < point->totkey; k++, ek++) {
					if (ek->flag & PEK_SELECT) {
						calc_tw_center(scene, ek->flag & PEK_USE_WCO ? ek->world_co : ek->co);
						totsel++;
					}
				}
			}

			/* selection center */
			if (totsel)
				mul_v3_fl(scene->twcent, 1.0f / (float)totsel);  // centroid!
		}
	}
	else {

		/* we need the one selected object, if its not active */
		ob = OBACT;
		if (ob && !(ob->flag & SELECT)) ob = NULL;

		for (base = scene->base.first; base; base = base->next) {
			if (TESTBASELIB(v3d, base)) {
				if (ob == NULL)
					ob = base->object;
				calc_tw_center(scene, base->object->obmat[3]);
				protectflag_to_drawflags(base->object->protectflag, &rv3d->twdrawflag);
				totsel++;
			}
		}

		/* selection center */
		if (totsel) {
			mul_v3_fl(scene->twcent, 1.0f / (float)totsel);   // centroid!
		}
	}

	/* global, local or normal orientation? */
	if (ob && totsel) {

		switch (v3d->twmode) {
		
			case V3D_MANIP_GLOBAL:
				break; /* nothing to do */

			case V3D_MANIP_GIMBAL:
			{
				float mat[3][3];
				if (gimbal_axis(ob, mat)) {
					copy_m4_m3(rv3d->twmat, mat);
					break;
				}
				/* if not gimbal, fall through to normal */
			}
			case V3D_MANIP_NORMAL:
				if (obedit || ob->mode & OB_MODE_POSE) {
					float mat[3][3];
					ED_getTransformOrientationMatrix(C, mat, (v3d->around == V3D_ACTIVE));
					copy_m4_m3(rv3d->twmat, mat);
					break;
				}
			/* no break we define 'normal' as 'local' in Object mode */
			case V3D_MANIP_LOCAL:
				copy_m4_m4(rv3d->twmat, ob->obmat);
				normalize_m4(rv3d->twmat);
				break;

			case V3D_MANIP_VIEW:
			{
				float mat[3][3];
				copy_m3_m4(mat, rv3d->viewinv);
				normalize_m3(mat);
				copy_m4_m3(rv3d->twmat, mat);
			}
			break;
			default: /* V3D_MANIP_CUSTOM */
			{
				float mat[3][3];
				applyTransformOrientation(C, mat, NULL);
				copy_m4_m3(rv3d->twmat, mat);
				break;
			}
		}

	}

	return totsel;
}

/* don't draw axis perpendicular to the view */
static void test_manipulator_axis(const bContext *C)
{
	RegionView3D *rv3d = CTX_wm_region_view3d(C);
	float angle;
	float vec[3];

	ED_view3d_global_to_vector(rv3d, rv3d->twmat[3], vec);

	angle = fabsf(angle_v3v3(rv3d->twmat[0], vec));
	if (angle > (float)M_PI / 2.0f) {
		angle = (float)M_PI - angle;
	}
	angle = rv3d->twangle[0] = RAD2DEGF(angle);
	if (angle < 5.0f) {
		rv3d->twdrawflag &= ~(MAN_TRANS_X | MAN_SCALE_X);
	}

	angle = fabsf(angle_v3v3(rv3d->twmat[1], vec));
	if (angle > (float)M_PI / 2.0f) {
		angle = (float)M_PI - angle;
	}
	angle = rv3d->twangle[1] = RAD2DEGF(angle);
	if (angle < 5.0f) {
		rv3d->twdrawflag &= ~(MAN_TRANS_Y | MAN_SCALE_Y);
	}

	angle = fabsf(angle_v3v3(rv3d->twmat[2], vec));
	if (angle > (float)M_PI / 2.0f) {
		angle = (float)M_PI - angle;
	}
	angle = rv3d->twangle[2] = RAD2DEGF(angle);
	if (angle < 5.0f) {
		rv3d->twdrawflag &= ~(MAN_TRANS_Z | MAN_SCALE_Z);
	}
}


/* ******************** DRAWING STUFFIES *********** */

static float screen_aligned(RegionView3D *rv3d, float mat[][4])
{
	gpuTranslate(mat[3][0], mat[3][1], mat[3][2]);

	/* sets view screen aligned */
	gpuRotateVector(-2.0f * saacos(rv3d->viewquat[0]), rv3d->viewquat+1);

	return len_v3(mat[0]); /* draw scale */
}


/* radring = radius of doughnut rings
 * radhole = radius hole
 * start = starting segment (based on nrings)
 * end   = end segment
 * nsides = amount of points in ring
 * nrigns = amount of rings
 */
static void partial_doughnut(float radring, float radhole, int start, int end, int nsides, int nrings)
{
	float theta, phi, theta1;
	float cos_theta, sin_theta;
	float cos_theta1, sin_theta1;
	float ring_delta, side_delta;
	int i, j, do_caps = TRUE;

	if (start == 0 && end == nrings) do_caps = FALSE;

	ring_delta = 2.0f * (float)M_PI / (float)nrings;
	side_delta = 2.0f * (float)M_PI / (float)nsides;

	theta = (float)M_PI + 0.5f * ring_delta;
	cos_theta = (float)cos(theta);
	sin_theta = (float)sin(theta);

	for (i = nrings - 1; i >= 0; i--) {
		theta1 = theta + ring_delta;
		cos_theta1 = (float)cos(theta1);
		sin_theta1 = (float)sin(theta1);

		if (do_caps && i == start) {  // cap
			gpuBegin(GL_TRIANGLE_FAN);
			phi = 0.0;
			for (j = nsides; j >= 0; j--) {
				float cos_phi, sin_phi, dist;

				phi += side_delta;
				cos_phi = (float)cos(phi);
				sin_phi = (float)sin(phi);
				dist = radhole + radring * cos_phi;

				gpuVertex3f(cos_theta1 * dist, -sin_theta1 * dist,  radring * sin_phi);
			}
			gpuEnd();
		}
		if (i >= start && i <= end) {
			gpuBegin(GL_TRIANGLE_STRIP);
			phi = 0.0;
			for (j = nsides; j >= 0; j--) {
				float cos_phi, sin_phi, dist;

				phi += side_delta;
				cos_phi = (float)cos(phi);
				sin_phi = (float)sin(phi);
				dist = radhole + radring * cos_phi;

				gpuVertex3f(cos_theta1 * dist, -sin_theta1 * dist, radring * sin_phi);
				gpuVertex3f(cos_theta * dist, -sin_theta * dist,  radring * sin_phi);
			}
			gpuEnd();
		}

		if (do_caps && i == end) {    // cap
			gpuBegin(GL_TRIANGLE_FAN);
			phi = 0.0;
			for (j = nsides; j >= 0; j--) {
				float cos_phi, sin_phi, dist;

				phi -= side_delta;
				cos_phi = (float)cos(phi);
				sin_phi = (float)sin(phi);
				dist = radhole + radring * cos_phi;

				gpuVertex3f(cos_theta * dist, -sin_theta * dist,  radring * sin_phi);
			}
			gpuEnd();
		}


		theta = theta1;
		cos_theta = cos_theta1;
		sin_theta = sin_theta1;
	}
}

static char axisBlendAngle(float angle)
{
	if (angle > 20)
		return 255;

	if (angle < 5)
		return 0;

	return (char)(255.0f * (angle - 5) / 15.0f);
}

/* three colors can be set:
 * gray for ghosting
 * moving: in transform theme color
 * else the red/green/blue
 */
static void set_manipulator_color(View3D *v3d, char axis, int colcode, unsigned char alpha)
{
	GLubyte col[4];

	if (colcode == MAN_GHOST) {
		col[0] = 0;
		col[1] = 0;
		col[2] = 0;
		col[3] = 70;
	}
	else if (colcode == MAN_MOVECOL) {
		UI_GetThemeColor3ubv(TH_TRANSFORM, col);
		col[3] = alpha;
	}
	else {
		switch (axis) {
			case 'C':
				UI_GetThemeColor3ubv(TH_TRANSFORM, col);
				if (v3d->twmode == V3D_MANIP_LOCAL) {
				col[0] = col[0] > 200 ? 255 : col[0]+55;
				col[1] = col[1] > 200 ? 255 : col[1]+55;
				col[2] = col[2] > 200 ? 255 : col[2]+55;
				col[3] = alpha;
				}
				else if (v3d->twmode == V3D_MANIP_NORMAL) {
				col[0] = col[0] < 55 ? 0 : col[0]-55;
				col[1] = col[1] < 55 ? 0 : col[1]-55;
				col[2] = col[2] < 55 ? 0 : col[2]-55;
				col[3] = alpha;
				}
				break;
			case 'X':
				UI_GetThemeColor3ubv(TH_AXIS_X, col);
				col[3] = alpha;
				break;
			case 'Y':
				UI_GetThemeColor3ubv(TH_AXIS_Y, col);
				col[3] = alpha;
				break;
			case 'Z':
				UI_GetThemeColor3ubv(TH_AXIS_Z, col);
				col[3] = alpha;
				break;
			default:
				BLI_assert(!"invalid axis arg");
		}
	}

	gpuCurrentColor4ubv(col);
}

/* viewmatrix should have been set OK, also no shademode! */
static void draw_manipulator_axes(View3D *v3d, RegionView3D *rv3d, int colcode, int flagx, int flagy, int flagz)
{
	/* axes */
	if (flagx) {
		set_manipulator_color(v3d, 'X', colcode, axisBlendAngle(rv3d->twangle[0]));

		if (flagx & MAN_SCALE_X) {
			glLoadName(MAN_SCALE_X);
		}
		else if (flagx & MAN_TRANS_X) {
			glLoadName(MAN_TRANS_X);
		}

		gpuBegin(GL_LINES);
		gpuVertex3f(0.2f, 0.0f, 0.0f);
		gpuVertex3f(1.0f, 0.0f, 0.0f);
		gpuEnd();
	}

	if (flagy) {
		if (flagy & MAN_SCALE_Y) {
			glLoadName(MAN_SCALE_Y);
		}
		else if (flagy & MAN_TRANS_Y) {
			glLoadName(MAN_TRANS_Y);
		}

		set_manipulator_color(v3d, 'Y', colcode, axisBlendAngle(rv3d->twangle[1]));

		gpuBegin(GL_LINES);
		gpuVertex3f(0.0f, 0.2f, 0.0f);
		gpuVertex3f(0.0f, 1.0f, 0.0f);
		gpuEnd();
	}

	if (flagz) {
		if (flagz & MAN_SCALE_Z) {
			glLoadName(MAN_SCALE_Z);
		}
		else if (flagz & MAN_TRANS_Z) {
			glLoadName(MAN_TRANS_Z);
		}

		set_manipulator_color(v3d, 'Z', colcode, axisBlendAngle(rv3d->twangle[2]));

		gpuBegin(GL_LINES);
		gpuVertex3f(0.0f, 0.0f, 0.2f);
		gpuVertex3f(0.0f, 0.0f, 1.0f);
		gpuEnd();
	}
}

static void preOrthoFront(int ortho, float twmat[][4], int axis)
{
	if (ortho == 0) {
		float omat[4][4];
		copy_m4_m4(omat, twmat);
		orthogonalize_m4(omat, axis);
		gpuPushMatrix();
		gpuMultMatrix(omat);
		glFrontFace(is_negative_m4(omat) ? GL_CW : GL_CCW);
	}
}

static void postOrtho(int ortho)
{
	if (ortho == 0) {
		gpuPopMatrix();
	}
}

static void draw_manipulator_rotate(View3D *v3d, RegionView3D *rv3d, int moving, int drawflags, int combo)
{
	double plane[4];
	float matt[4][4];
	float size;
	float cywid = 0.33f * 0.01f * (float)U.tw_handlesize;
	float cusize = cywid * 0.65f;
	int arcs = (G.debug_value != 2);
	int colcode;
	int ortho;

	colcode = moving ? MAN_MOVECOL : MAN_RGB;

	/* when called while moving in mixed mode, do not draw when... */
	if (!(drawflags & MAN_ROT_C)) {
		return;
	}

	/* Init stuff */
	glDisable(GL_DEPTH_TEST);

	/* prepare for screen aligned draw */
	size = len_v3(rv3d->twmat[0]);
	gpuPushMatrix();
	gpuTranslate(rv3d->twmat[3][0], rv3d->twmat[3][1], rv3d->twmat[3][2]);

	if (arcs) {
		/* clipplane makes nice handles, calc here because of multmatrix but with translate! */
		copy_v3db_v3fl(plane, rv3d->viewinv[2]);
		plane[3] = -0.02f * size; // clip just a bit more
		glClipPlane(GL_CLIP_PLANE0, plane);
	}

	/* sets view screen aligned */
	gpuRotateVector(-2.0f * saacos(rv3d->viewquat[0]), rv3d->viewquat+1);

	/* Screen aligned help circle */
	if (arcs && !(G.f & G_PICKSEL)) {
		UI_ThemeColorShade(TH_BACK, -30);
		gpuDrawFastCircleXY(size);
	}

	/* Screen aligned trackball rot circle */
	if (drawflags & MAN_ROT_T) {
		if (G.f & G_PICKSEL) {
			glLoadName(MAN_ROT_T);
		}

		UI_ThemeColor(TH_TRANSFORM);
		gpuDrawFastCircleXY(0.2f*size);
	}

	/* Screen aligned view rot circle */
	if (drawflags & MAN_ROT_V) {
		if (G.f & G_PICKSEL) {
			glLoadName(MAN_ROT_V);
		}

		UI_ThemeColor(TH_TRANSFORM);
		gpuDrawFastCircleXY(1.2f*size);

		if (moving) {
			float vec[3];
			vec[0] = 0; // XXX (float)(t->imval[0] - t->center2d[0]);
			vec[1] = 0; // XXX (float)(t->imval[1] - t->center2d[1]);
			vec[2] = 0.0f;
			normalize_v3(vec);
			mul_v3_fl(vec, 1.2f * size);

			gpuBegin(GL_LINES);
			gpuVertex3f(0.0f, 0.0f, 0.0f);
			gpuVertex3fv(vec);
			gpuEnd();
		}
	}

	gpuPopMatrix();

	ortho = is_orthogonal_m4(rv3d->twmat);

	/* apply the transform delta */
	if (moving) {
		copy_m4_m4(matt, rv3d->twmat); // to copy the parts outside of [3][3]
		// XXX mul_m4_m3m4(matt, t->mat, rv3d->twmat);
		if (ortho) {
			gpuMultMatrix(matt);
			glFrontFace(is_negative_m4(matt) ? GL_CW : GL_CCW);
		}
	}
	else if (ortho) {
		glFrontFace(is_negative_m4(rv3d->twmat) ? GL_CW:GL_CCW);
		gpuMultMatrix(rv3d->twmat);
	}

	// donut arcs
	if (arcs) {
		glEnable(GL_CLIP_PLANE0);

		/* Z circle */
		if (drawflags & MAN_ROT_Z) {
			preOrthoFront(ortho, rv3d->twmat, 2);

			if (G.f & G_PICKSEL) {
				glLoadName(MAN_ROT_Z);
			}

			set_manipulator_color(v3d, 'Z', colcode, 255);
			partial_doughnut(cusize/4.0f, 1.0f, 0, 48, 8, 48);
			postOrtho(ortho);
		}

		/* X circle */
		if (drawflags & MAN_ROT_X) {
			preOrthoFront(ortho, rv3d->twmat, 0);

			if (G.f & G_PICKSEL) {
				glLoadName(MAN_ROT_X);
			}

			gpuRotateRight('Y');
			set_manipulator_color(v3d, 'X', colcode, 255);
			partial_doughnut(cusize/4.0f, 1.0f, 0, 48, 8, 48);
			gpuRotateRight(-'Y');
			postOrtho(ortho);
		}

		/* Y circle */
		if (drawflags & MAN_ROT_Y) {
			preOrthoFront(ortho, rv3d->twmat, 1);

			if (G.f & G_PICKSEL) {
				glLoadName(MAN_ROT_Y);
			}

			gpuRotateRight(-'X');
			set_manipulator_color(v3d, 'Y', colcode, 255);
			partial_doughnut(cusize/4.0f, 1.0f, 0, 48, 8, 48);
			gpuRotateRight('X');
			postOrtho(ortho);
		}

		glDisable(GL_CLIP_PLANE0);
	}
	else /* !arcs */ {
		/* axes */
		if (!(G.f & G_PICKSEL) && !(combo & V3D_MANIP_SCALE)) {

			if ( (drawflags & MAN_ROT_X) || (moving && (drawflags & MAN_ROT_Z)) ) {
				preOrthoFront(ortho, rv3d->twmat, 2);
				set_manipulator_color(v3d, 'X', colcode, 255);

				gpuBegin(GL_LINES);
				gpuVertex3f(0.2f, 0.0f, 0.0f);
				gpuVertex3f(1.0f, 0.0f, 0.0f);
				gpuEnd();

				postOrtho(ortho);
			}

			if ( (drawflags & MAN_ROT_Y) || (moving && (drawflags & MAN_ROT_X)) ) {
				preOrthoFront(ortho, rv3d->twmat, 0);
				set_manipulator_color(v3d, 'Y', colcode, 255);

				gpuBegin(GL_LINES);
				gpuVertex3f(0.0f, 0.2f, 0.0f);
				gpuVertex3f(0.0f, 1.0f, 0.0f);
				gpuEnd();

				postOrtho(ortho);
			}

			if ( (drawflags & MAN_ROT_Z) || (moving && (drawflags & MAN_ROT_Y)) ) {
				preOrthoFront(ortho, rv3d->twmat, 1);
				set_manipulator_color(v3d, 'Z', colcode, 255);

				gpuBegin(GL_LINES);
				gpuVertex3f(0.0f, 0.0f, 0.2f);
				gpuVertex3f(0.0f, 0.0f, 1.0f);
				gpuEnd();

				postOrtho(ortho);
			}
		}

		if (moving) {
			/* Z circle */
			if (drawflags & MAN_ROT_Z) {
				preOrthoFront(ortho, matt, 2);

				if (G.f & G_PICKSEL) {
					glLoadName(MAN_ROT_Z);
				}

				set_manipulator_color(v3d, 'Z', colcode, 255);
				gpuDrawFastCircleXY(1);
				postOrtho(ortho);
			}

			/* X circle */
			if (drawflags & MAN_ROT_X) {
				preOrthoFront(ortho, matt, 0);

				if (G.f & G_PICKSEL) {
					glLoadName(MAN_ROT_X);
				}

				gpuPushMatrix();
				gpuRotateRight('Y');
				set_manipulator_color(v3d, 'X', colcode, 255);
				gpuDrawFastCircleXY(1);
				gpuPopMatrix();
				postOrtho(ortho);
			}

			/* Y circle */
			if (drawflags & MAN_ROT_Y) {
				preOrthoFront(ortho, matt, 1);

				if (G.f & G_PICKSEL) {
					glLoadName(MAN_ROT_Y);
				}

				gpuPushMatrix();
				gpuRotateRight(-'X');
				set_manipulator_color(v3d, 'Y', colcode, 255);
				gpuDrawFastCircleXY(1);
				gpuPopMatrix();
				postOrtho(ortho);
			}

			if (arcs) {
				glDisable(GL_CLIP_PLANE0);
			}
		}

		/* Z handle on X axis */
		if (drawflags & MAN_ROT_Z) {
			preOrthoFront(ortho, rv3d->twmat, 2);

			if (G.f & G_PICKSEL) {
				glLoadName(MAN_ROT_Z);
			}

			set_manipulator_color(v3d, 'Z', colcode, 255);
			partial_doughnut(0.7f * cusize, 1.0f, 31, 33, 8, 64);

			postOrtho(ortho);
		}

		/* Y handle on X axis */
		if (drawflags & MAN_ROT_Y) {
			preOrthoFront(ortho, rv3d->twmat, 1);

			if (G.f & G_PICKSEL) {
				glLoadName(MAN_ROT_Y);
			}

			gpuPushMatrix();
			gpuRotateRight('X');
			gpuRotateRight('Z');

			set_manipulator_color(v3d, 'Y', colcode, 255);
			partial_doughnut(0.7f * cusize, 1.0f, 31, 33, 8, 64);

			gpuPopMatrix();
			postOrtho(ortho);
		}

		/* X handle on Z axis */
		if (drawflags & MAN_ROT_X) {
			preOrthoFront(ortho, rv3d->twmat, 0);

			if (G.f & G_PICKSEL) {
				glLoadName(MAN_ROT_X);
			}

			gpuPushMatrix();
			gpuRotateRight(-'Y');
			gpuRotateRight('Z');

			set_manipulator_color(v3d, 'X', colcode, 255);
			partial_doughnut(0.7f * cusize, 1.0f, 31, 33, 8, 64);

			gpuPopMatrix();
			postOrtho(ortho);
		}
	}

	/* restore */
	gpuLoadMatrix(rv3d->viewmat);

	if (v3d->zbuf) {
		glEnable(GL_DEPTH_TEST);
	}
}

static void drawsolidcube(float size)
{
	static float cube[8][3] = {
		{-1.0, -1.0, -1.0},
		{-1.0, -1.0,  1.0},
		{-1.0,  1.0,  1.0},
		{-1.0,  1.0, -1.0},
		{ 1.0, -1.0, -1.0},
		{ 1.0, -1.0,  1.0},
		{ 1.0,  1.0,  1.0},
		{ 1.0,  1.0, -1.0},
	};
	float n[3] = {0.0f};

	gpuPushMatrix();
	gpuScale(size, size, size);

	gpuBegin(GL_TRIANGLE_FAN);
	n[0] = -1.0;
	gpuNormal3fv(n);
	gpuVertex3fv(cube[0]); gpuVertex3fv(cube[1]); gpuVertex3fv(cube[2]); gpuVertex3fv(cube[3]);
	n[0] = 0;
	gpuEnd();

	gpuBegin(GL_TRIANGLE_FAN);
	n[1] = -1.0;
	gpuNormal3fv(n);
	gpuVertex3fv(cube[0]); gpuVertex3fv(cube[4]); gpuVertex3fv(cube[5]); gpuVertex3fv(cube[1]);
	n[1] = 0;
	gpuEnd();

	gpuBegin(GL_TRIANGLE_FAN);
	n[0] = 1.0;
	gpuNormal3fv(n);
	gpuVertex3fv(cube[4]); gpuVertex3fv(cube[7]); gpuVertex3fv(cube[6]); gpuVertex3fv(cube[5]);
	n[0] = 0;
	gpuEnd();

	gpuBegin(GL_TRIANGLE_FAN);
	n[1] = 1.0;
	gpuNormal3fv(n);
	gpuVertex3fv(cube[7]); gpuVertex3fv(cube[3]); gpuVertex3fv(cube[2]); gpuVertex3fv(cube[6]);
	n[1] = 0;
	gpuEnd();

	gpuBegin(GL_QUADS);
	n[2] = 1.0;
	gpuNormal3fv(n);
	gpuVertex3fv(cube[1]); gpuVertex3fv(cube[5]); gpuVertex3fv(cube[6]); gpuVertex3fv(cube[2]);
	n[2] = 0;
	gpuEnd();

	gpuBegin(GL_QUADS);
	n[2] = -1.0;
	gpuNormal3fv(n);
	gpuVertex3fv(cube[7]); gpuVertex3fv(cube[4]); gpuVertex3fv(cube[0]); gpuVertex3fv(cube[3]);
	gpuEnd();

	gpuPopMatrix();
}


static void draw_manipulator_scale(View3D *v3d, RegionView3D *rv3d, int moving, int drawflags, int combo, int colcode)
{
	float cywid = 0.25f * 0.01f * (float)U.tw_handlesize;
	float cusize = cywid * 0.75f, dz;

	/* when called while moving in mixed mode, do not draw when... */
	if ((drawflags & MAN_SCALE_C) == 0) return;

	glDisable(GL_DEPTH_TEST);

	/* not in combo mode */
	if ((combo & (V3D_MANIP_TRANSLATE | V3D_MANIP_ROTATE)) == 0) {
		float size, unitmat[4][4];
		int shift = 0; // XXX

		/* center circle, do not add to selection when shift is pressed (planar constraint)  */
		if ((G.f & G_PICKSEL) && shift == 0) glLoadName(MAN_SCALE_C);

		set_manipulator_color(v3d, 'C', colcode, 255);
		gpuPushMatrix();
		size = screen_aligned(rv3d, rv3d->twmat);
		unit_m4(unitmat);
		gpuDrawFastCircleXY(0.2f*size);
		gpuPopMatrix();

		dz = 1.0;
	}
	else dz = 1.0f - 4.0f * cusize;

	if (moving) {
		float matt[4][4];

		copy_m4_m4(matt, rv3d->twmat); // to copy the parts outside of [3][3]
		// XXX mul_m4_m3m4(matt, t->mat, rv3d->twmat);
		gpuMultMatrix(matt);
		glFrontFace(is_negative_m4(matt) ? GL_CW : GL_CCW);
	}
	else {
		gpuMultMatrix(rv3d->twmat);
		glFrontFace(is_negative_m4(rv3d->twmat) ? GL_CW : GL_CCW);
	}

	/* axis */

	/* in combo mode, this is always drawn as first type */
	draw_manipulator_axes(v3d, rv3d, colcode, drawflags & MAN_SCALE_X, drawflags & MAN_SCALE_Y, drawflags & MAN_SCALE_Z);

	/* Z cube */
	gpuTranslate(0.0, 0.0, dz);
	if (drawflags & MAN_SCALE_Z) {
		if (G.f & G_PICKSEL) glLoadName(MAN_SCALE_Z);
		set_manipulator_color(v3d, 'Z', colcode, axisBlendAngle(rv3d->twangle[2]));
		drawsolidcube(cusize);
	}

	/* X cube */
	gpuTranslate(dz, 0.0, -dz);
	if (drawflags & MAN_SCALE_X) {
		if (G.f & G_PICKSEL) glLoadName(MAN_SCALE_X);
		set_manipulator_color(v3d, 'X', colcode, axisBlendAngle(rv3d->twangle[0]));
		drawsolidcube(cusize);
	}

	/* Y cube */
	gpuTranslate(-dz, dz, 0.0);
	if (drawflags & MAN_SCALE_Y) {
		if (G.f & G_PICKSEL) glLoadName(MAN_SCALE_Y);
		set_manipulator_color(v3d, 'Y', colcode, axisBlendAngle(rv3d->twangle[1]));
		drawsolidcube(cusize);
	}

	/* if shiftkey, center point as last, for selectbuffer order */
	if (G.f & G_PICKSEL) {
		int shift = 0; // XXX

		if (shift) {
			gpuTranslate(0.0, -dz, 0.0);
			glLoadName(MAN_SCALE_C);
			gpuBegin(GL_POINTS);
			gpuVertex3f(0.0, 0.0, 0.0);
			gpuEnd();
		}
	}

	/* restore */
	gpuLoadMatrix(rv3d->viewmat);

	if (v3d->zbuf) glEnable(GL_DEPTH_TEST);
	glFrontFace(GL_CCW);
}

static void draw_manipulator_translate(View3D *v3d, RegionView3D *rv3d, int UNUSED(moving), int drawflags, int combo, int colcode)
{
	GPUprim3 prim = GPU_PRIM_LOFI_SHADELESS;

	float cylen = 0.01f * (float)U.tw_handlesize;
	float cywid = 0.25f * cylen, dz, size;
	int shift = 0; // XXX

	/* when called while moving in mixed mode, do not draw when... */
	if ((drawflags & MAN_TRANS_C) == 0) return;

	// XXX if (moving) gpuTranslate(t->vec[0], t->vec[1], t->vec[2]);
	glDisable(GL_DEPTH_TEST);

	/* center circle, do not add to selection when shift is pressed (planar constraint) */
	if ( (G.f & G_PICKSEL) && shift==0) {
		glLoadName(MAN_TRANS_C);
	}

	set_manipulator_color(v3d, 'C', colcode, 255);

	gpuPushMatrix();
	size = screen_aligned(rv3d, rv3d->twmat);
	gpuDrawFastCircleXY(0.2f*size);
	gpuPopMatrix();

	/* and now apply matrix, we move to local matrix drawing */
	gpuMultMatrix(rv3d->twmat);

	/* axis */
	glLoadName(-1);

	// translate drawn as last, only axis when no combo with scale, or for ghosting
	if ((combo & V3D_MANIP_SCALE) == 0 || colcode == MAN_GHOST)
		draw_manipulator_axes(v3d, rv3d, colcode, drawflags & MAN_TRANS_X, drawflags & MAN_TRANS_Y, drawflags & MAN_TRANS_Z);

	/* offset in combo mode, for rotate a bit more */
	if (combo & (V3D_MANIP_ROTATE)) {
		dz= 1.0f+2.0f*cylen;
	}
	else if (combo & (V3D_MANIP_SCALE)) {
		dz= 1.0f+0.5f*cylen;
	}
	else {
		dz= 1.0f;
	}

	prim.vsegs = 1;

	gpuBegin(GL_NOOP);
	gpuAppendCone(&prim, cywid, cylen);
	gpuEnd();

	/* Z Cone */

	gpuTranslate(0.0, 0.0, dz);

	if (drawflags & MAN_TRANS_Z) {
		if (G.f & G_PICKSEL) {
			glLoadName(MAN_TRANS_Z);
		}

		set_manipulator_color(v3d, 'Z', colcode, axisBlendAngle(rv3d->twangle[2]));

		gpuDrawElements(GL_TRIANGLES);
	}

	/* X Cone */

	gpuTranslate(dz, 0.0, -dz);

	if (drawflags & MAN_TRANS_X) {
		if (G.f & G_PICKSEL) {
			glLoadName(MAN_TRANS_X);
		}

		set_manipulator_color(v3d, 'X', colcode, axisBlendAngle(rv3d->twangle[0]));

		gpuPushMatrix();
		gpuRotateRight('Y');
		gpuDrawElements(GL_TRIANGLES);
		gpuPopMatrix();
	}

	/* Y Cone */

	gpuTranslate(-dz, dz, 0.0);

	if (drawflags & MAN_TRANS_Y) {
		if (G.f & G_PICKSEL) {
			glLoadName(MAN_TRANS_Y);
		}

		set_manipulator_color(v3d, 'Y', colcode, axisBlendAngle(rv3d->twangle[1]));

		gpuPushMatrix();
		gpuRotateRight(-'X');
		gpuDrawElements(GL_TRIANGLES);
		gpuPopMatrix();
	}

	gpuLoadMatrix(rv3d->viewmat);

	if (v3d->zbuf) {
		glEnable(GL_DEPTH_TEST);
	}
}

/* ********************************************* */

/* main call, does calc centers & orientation too */
/* uses global G.moving */
static int drawflags = 0xFFFF;       // only for the calls below, belongs in scene...?

void BIF_draw_manipulator(const bContext *C)
{
	ScrArea *sa = CTX_wm_area(C);
	ARegion *ar = CTX_wm_region(C);
	Scene *scene = CTX_data_scene(C);
	View3D *v3d = sa->spacedata.first;
	RegionView3D *rv3d = ar->regiondata;
	int totsel;

	if (!(v3d->twflag & V3D_USE_MANIPULATOR)) return;
//	if (G.moving && (G.moving & G_TRANSFORM_MANIP)==0) return;

	gpuImmediateFormat_V3(); // draw manipulator

//	if (G.moving==0) {
	{
		v3d->twflag &= ~V3D_DRAW_MANIPULATOR;

		totsel = calc_manipulator_stats(C);

		if (totsel==0) {
			gpuImmediateUnformat();
			return;
		}

		v3d->twflag |= V3D_DRAW_MANIPULATOR;

		/* now we can define center */
		switch (v3d->around) {
			case V3D_CENTER:
			case V3D_ACTIVE:
				rv3d->twmat[3][0] = (scene->twmin[0] + scene->twmax[0]) / 2.0f;
				rv3d->twmat[3][1] = (scene->twmin[1] + scene->twmax[1]) / 2.0f;
				rv3d->twmat[3][2] = (scene->twmin[2] + scene->twmax[2]) / 2.0f;
				if (v3d->around == V3D_ACTIVE && scene->obedit == NULL) {
					Object *ob = OBACT;
					if (ob && !(ob->mode & OB_MODE_POSE))
						copy_v3_v3(rv3d->twmat[3], ob->obmat[3]);
				}
				break;
			case V3D_LOCAL:
			case V3D_CENTROID:
				copy_v3_v3(rv3d->twmat[3], scene->twcent);
				break;
			case V3D_CURSOR:
				copy_v3_v3(rv3d->twmat[3], give_cursor(scene, v3d));
				break;
		}

		mul_mat3_m4_fl(rv3d->twmat, ED_view3d_pixel_size(rv3d, rv3d->twmat[3]) * U.tw_size * 5.0f);
	}

	test_manipulator_axis(C);
	drawflags = rv3d->twdrawflag;    /* set in calc_manipulator_stats */

	if (v3d->twflag & V3D_DRAW_MANIPULATOR) {

		glEnable(GL_BLEND);
		if (v3d->twtype & V3D_MANIP_ROTATE) {
			draw_manipulator_rotate(v3d, rv3d, 0 /* G.moving*/, drawflags, v3d->twtype);
		}
		if (v3d->twtype & V3D_MANIP_SCALE) {
			draw_manipulator_scale(v3d, rv3d, 0, drawflags, v3d->twtype, MAN_RGB);
		}
		if (v3d->twtype & V3D_MANIP_TRANSLATE) {
			draw_manipulator_translate(v3d, rv3d, 0, drawflags, v3d->twtype, MAN_RGB);
		}

		glDisable(GL_BLEND);
	}

	gpuImmediateUnformat();
}

static int manipulator_selectbuf(ScrArea *sa, ARegion *ar, const int mval[2], float hotspot)
{
	View3D *v3d = sa->spacedata.first;
	RegionView3D *rv3d = ar->regiondata;
	rctf rect;
	GLuint buffer[64];      // max 4 items per select, so large enuf
	short hits;
	extern void setwinmatrixview3d(ARegion *ar, View3D *v3d, rctf *rect); // XXX check a bit later on this... (ton)

	G.f |= G_PICKSEL;

	rect.xmin = mval[0] - hotspot;
	rect.xmax = mval[0] + hotspot;
	rect.ymin = mval[1] - hotspot;
	rect.ymax = mval[1] + hotspot;

	setwinmatrixview3d(ar, v3d, &rect);
	mult_m4_m4m4(rv3d->persmat, rv3d->winmat, rv3d->viewmat);

	glSelectBuffer(64, buffer);
	glRenderMode(GL_SELECT);
	glInitNames();  /* these two calls whatfor? It doesnt work otherwise */
	glPushName(-2);

	/* do the drawing */
	if (v3d->twtype & V3D_MANIP_ROTATE) {
		draw_manipulator_rotate(v3d, rv3d, 0, MAN_ROT_C & rv3d->twdrawflag, v3d->twtype);
	}
	if (v3d->twtype & V3D_MANIP_SCALE)
		draw_manipulator_scale(v3d, rv3d, 0, MAN_SCALE_C & rv3d->twdrawflag, v3d->twtype, MAN_RGB);
	if (v3d->twtype & V3D_MANIP_TRANSLATE)
		draw_manipulator_translate(v3d, rv3d, 0, MAN_TRANS_C & rv3d->twdrawflag, v3d->twtype, MAN_RGB);

	glPopName();
	hits = glRenderMode(GL_RENDER);

	G.f &= ~G_PICKSEL;
	setwinmatrixview3d(ar, v3d, NULL);
	mult_m4_m4m4(rv3d->persmat, rv3d->winmat, rv3d->viewmat);

	if (hits == 1) return buffer[3];
	else if (hits > 1) {
		GLuint val, dep, mindep = 0, mindeprot = 0, minval = 0, minvalrot = 0;
		int a;

		/* we compare the hits in buffer, but value centers highest */
		/* we also store the rotation hits separate (because of arcs) and return hits on other widgets if there are */

		for (a = 0; a < hits; a++) {
			dep = buffer[4 * a + 1];
			val = buffer[4 * a + 3];

			if (val == MAN_TRANS_C) return MAN_TRANS_C;
			else if (val == MAN_SCALE_C) return MAN_SCALE_C;
			else {
				if (val & MAN_ROT_C) {
					if (minvalrot == 0 || dep < mindeprot) {
						mindeprot = dep;
						minvalrot = val;
					}
				}
				else {
					if (minval == 0 || dep < mindep) {
						mindep = dep;
						minval = val;
					}
				}
			}
		}

		if (minval)
			return minval;
		else
			return minvalrot;
	}
	return 0;
}


/* return 0; nothing happened */
int BIF_do_manipulator(bContext *C, struct wmEvent *event, wmOperator *op)
{
	ScrArea *sa = CTX_wm_area(C);
	View3D *v3d = sa->spacedata.first;
	ARegion *ar = CTX_wm_region(C);
	int constraint_axis[3] = {0, 0, 0};
	int val;
	int shift = event->shift;

	if (!(v3d->twflag & V3D_USE_MANIPULATOR)) return 0;
	if (!(v3d->twflag & V3D_DRAW_MANIPULATOR)) return 0;

	/* Force orientation */
	RNA_enum_set(op->ptr, "constraint_orientation", v3d->twmode);

	gpuImmediateFormat_V3(); // DOODLE: do manipulator

	// find the hotspots first test narrow hotspot
	val = manipulator_selectbuf(sa, ar, event->mval, 0.5f * (float)U.tw_hotspot);
	if (val) {

		// drawflags still global, for drawing call above
		drawflags = manipulator_selectbuf(sa, ar, event->mval, 0.2f * (float)U.tw_hotspot);
		if (drawflags == 0) drawflags = val;

		if (drawflags & MAN_TRANS_C) {
			switch (drawflags) {
				case MAN_TRANS_C:
					break;
				case MAN_TRANS_X:
					if (shift) {
						constraint_axis[1] = 1;
						constraint_axis[2] = 1;
					}
					else
						constraint_axis[0] = 1;
					break;
				case MAN_TRANS_Y:
					if (shift) {
						constraint_axis[0] = 1;
						constraint_axis[2] = 1;
					}
					else
						constraint_axis[1] = 1;
					break;
				case MAN_TRANS_Z:
					if (shift) {
						constraint_axis[0] = 1;
						constraint_axis[1] = 1;
					}
					else
						constraint_axis[2] = 1;
					break;
			}
			RNA_boolean_set_array(op->ptr, "constraint_axis", constraint_axis);
			WM_operator_name_call(C, "TRANSFORM_OT_translate", WM_OP_INVOKE_DEFAULT, op->ptr);
			//wm_operator_invoke(C, WM_operatortype_find("TRANSFORM_OT_translate", 0), event, op->ptr, NULL, FALSE);
		}
		else if (drawflags & MAN_SCALE_C) {
			switch (drawflags) {
				case MAN_SCALE_X:
					if (shift) {
						constraint_axis[1] = 1;
						constraint_axis[2] = 1;
					}
					else
						constraint_axis[0] = 1;
					break;
				case MAN_SCALE_Y:
					if (shift) {
						constraint_axis[0] = 1;
						constraint_axis[2] = 1;
					}
					else
						constraint_axis[1] = 1;
					break;
				case MAN_SCALE_Z:
					if (shift) {
						constraint_axis[0] = 1;
						constraint_axis[1] = 1;
					}
					else
						constraint_axis[2] = 1;
					break;
			}
			RNA_boolean_set_array(op->ptr, "constraint_axis", constraint_axis);
			WM_operator_name_call(C, "TRANSFORM_OT_resize", WM_OP_INVOKE_DEFAULT, op->ptr);
			//wm_operator_invoke(C, WM_operatortype_find("TRANSFORM_OT_resize", 0), event, op->ptr, NULL, FALSE);
		}
		else if (drawflags == MAN_ROT_T) { /* trackball need special case, init is different */
			WM_operator_name_call(C, "TRANSFORM_OT_trackball", WM_OP_INVOKE_DEFAULT, op->ptr);
			//wm_operator_invoke(C, WM_operatortype_find("TRANSFORM_OT_trackball", 0), event, op->ptr, NULL, FALSE);
		}
		else if (drawflags & MAN_ROT_C) {
			switch (drawflags) {
				case MAN_ROT_X:
					constraint_axis[0] = 1;
					break;
				case MAN_ROT_Y:
					constraint_axis[1] = 1;
					break;
				case MAN_ROT_Z:
					constraint_axis[2] = 1;
					break;
			}
			RNA_boolean_set_array(op->ptr, "constraint_axis", constraint_axis);
			WM_operator_name_call(C, "TRANSFORM_OT_rotate", WM_OP_INVOKE_DEFAULT, op->ptr);
			//wm_operator_invoke(C, WM_operatortype_find("TRANSFORM_OT_rotate", 0), event, op->ptr, NULL, FALSE);
		}
	}
	/* after transform, restore drawflags */
	drawflags = 0xFFFF;

	gpuImmediateUnformat();

	return val;
}

