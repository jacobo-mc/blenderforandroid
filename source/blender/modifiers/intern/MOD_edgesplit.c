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
 * along with this program; if not, write to the Free Software  Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2005 by the Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Daniel Dunbar
 *                 Ton Roosendaal,
 *                 Ben Batt,
 *                 Brecht Van Lommel,
 *                 Campbell Barton
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 */

/** \file blender/modifiers/intern/MOD_edgesplit.c
 *  \ingroup modifiers
 */


/* EdgeSplit modifier: Splits edges in the mesh according to sharpness flag
 * or edge angle (can be used to achieve autosmoothing) */

#include "BLI_utildefines.h"
#include "BLI_math.h"

#include "MEM_guardedalloc.h"

#include "BKE_cdderivedmesh.h"
#include "BKE_modifier.h"
#include "BKE_mesh.h"

#include "bmesh.h"

#include "DNA_object_types.h"

/* EdgeSplit */
/* EdgeSplit modifier: Splits edges in the mesh according to sharpness flag
 * or edge angle (can be used to achieve autosmoothing)
 *
 * note: this code is very close to MOD_bevel.c
 */

#define EDGE_MARK  1

static DerivedMesh *doEdgeSplit(DerivedMesh *dm, EdgeSplitModifierData *emd, Object *UNUSED(ob))
{
	DerivedMesh *result;
	BMesh *bm;
	BMIter iter;
	BMEdge *e;
	float threshold = cosf((emd->split_angle + 0.00001f) * (float)M_PI / 180.0f);

	bm = DM_to_bmesh(dm);
	BM_mesh_elem_toolflags_ensure(bm);
	
	if (emd->flags & MOD_EDGESPLIT_FROMANGLE) {
		BM_ITER_MESH (e, &iter, bm, BM_EDGES_OF_MESH) {
			/* check for 1 edge having 2 face users */
			BMLoop *l1, *l2;
			if ((l1 = e->l) &&
			    (l2 = e->l->radial_next) != l1)
			{
				if (/* 3+ faces on this edge, always split */
				    UNLIKELY(l1 != l2->radial_next) ||
				    /* 2 face edge - check angle*/
				    (dot_v3v3(l1->f->no, l2->f->no) < threshold))
				{
					BMO_elem_flag_enable(bm, e, EDGE_MARK);
				}
			}
		}
	}
	
	if (emd->flags & MOD_EDGESPLIT_FROMFLAG) {
		BM_ITER_MESH (e, &iter, bm, BM_EDGES_OF_MESH) {
			/* check for 2 or more edge users */
			if ((e->l) &&
			    (e->l->next != e->l))
			{
				if (!BM_elem_flag_test(e, BM_ELEM_SMOOTH)) {
					BMO_elem_flag_enable(bm, e, EDGE_MARK);
				}
			}
		}
	}
	
	BMO_op_callf(bm, BMO_FLAG_DEFAULTS,
	             "split_edges edges=%fe", EDGE_MARK);

	/* BM_mesh_validate(bm); */ /* for troubleshooting */

	result = CDDM_from_bmesh(bm, TRUE);
	BM_mesh_free(bm);
	
	return result;
}

static void initData(ModifierData *md)
{
	EdgeSplitModifierData *emd = (EdgeSplitModifierData *) md;

	/* default to 30-degree split angle, sharpness from both angle & flag */
	emd->split_angle = 30;
	emd->flags = MOD_EDGESPLIT_FROMANGLE | MOD_EDGESPLIT_FROMFLAG;
}

static void copyData(ModifierData *md, ModifierData *target)
{
	EdgeSplitModifierData *emd = (EdgeSplitModifierData *) md;
	EdgeSplitModifierData *temd = (EdgeSplitModifierData *) target;

	temd->split_angle = emd->split_angle;
	temd->flags = emd->flags;
}

static DerivedMesh *edgesplitModifier_do(EdgeSplitModifierData *emd,
                                         Object *ob, DerivedMesh *dm)
{
	if (!(emd->flags & (MOD_EDGESPLIT_FROMANGLE | MOD_EDGESPLIT_FROMFLAG)))
		return dm;

	return doEdgeSplit(dm, emd, ob);
}

static DerivedMesh *applyModifier(ModifierData *md, Object *ob, DerivedMesh *derivedData,
                                  ModifierApplyFlag UNUSED(flag))
{
	DerivedMesh *result;
	EdgeSplitModifierData *emd = (EdgeSplitModifierData *) md;

	result = edgesplitModifier_do(emd, ob, derivedData);

	if (result != derivedData)
		CDDM_calc_normals(result);

	return result;
}

static DerivedMesh *applyModifierEM(ModifierData *md, Object *ob,
                                    struct BMEditMesh *UNUSED(editData),
                                    DerivedMesh *derivedData)
{
	return applyModifier(md, ob, derivedData, MOD_APPLY_USECACHE);
}


ModifierTypeInfo modifierType_EdgeSplit = {
	/* name */              "EdgeSplit",
	/* structName */        "EdgeSplitModifierData",
	/* structSize */        sizeof(EdgeSplitModifierData),
	/* type */              eModifierTypeType_Constructive,
	/* flags */             eModifierTypeFlag_AcceptsMesh |
	                        eModifierTypeFlag_AcceptsCVs |
	                        eModifierTypeFlag_SupportsMapping |
	                        eModifierTypeFlag_SupportsEditmode |
	                        eModifierTypeFlag_EnableInEditmode,

	/* copyData */          copyData,
	/* deformVerts */       NULL,
	/* deformMatrices */    NULL,
	/* deformVertsEM */     NULL,
	/* deformMatricesEM */  NULL,
	/* applyModifier */     applyModifier,
	/* applyModifierEM */   applyModifierEM,
	/* initData */          initData,
	/* requiredDataMask */  NULL,
	/* freeData */          NULL,
	/* isDisabled */        NULL,
	/* updateDepgraph */    NULL,
	/* dependsOnTime */     NULL,
	/* dependsOnNormals */	NULL,
	/* foreachObjectLink */ NULL,
	/* foreachIDLink */     NULL,
	/* foreachTexLink */    NULL,
};
