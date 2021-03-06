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
 * The Original Code is Copyright (C) 2009 Blender Foundation.
 * All rights reserved.
 *
 *
 * Contributor(s): Blender Foundation
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/makesrna/intern/rna_object_api.c
 *  \ingroup RNA
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "RNA_define.h"

#include "DNA_object_types.h"
#include "DNA_modifier_types.h"

#include "rna_internal.h"  /* own include */

#ifdef RNA_RUNTIME
#include "BLI_math.h"

#include "BKE_main.h"
#include "BKE_global.h"
#include "BKE_context.h"
#include "BKE_report.h"
#include "BKE_object.h"
#include "BKE_mesh.h"
#include "BKE_DerivedMesh.h"
#include "BKE_bvhutils.h"

#include "BKE_customdata.h"
#include "BKE_anim.h"
#include "BKE_depsgraph.h"
#include "BKE_displist.h"
#include "BKE_font.h"
#include "BKE_mball.h"
#include "BKE_modifier.h"
#include "BKE_cdderivedmesh.h"

#include "DNA_mesh_types.h"
#include "DNA_scene_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_curve_types.h"
#include "DNA_modifier_types.h"
#include "DNA_constraint_types.h"
#include "DNA_view3d_types.h"

#include "MEM_guardedalloc.h"

/* copied from Mesh_getFromObject and adapted to RNA interface */
/* settings: 0 - preview, 1 - render */
static Mesh *rna_Object_to_mesh(Object *ob, ReportList *reports, Scene *sce, int apply_modifiers, int settings)
{
	Mesh *tmpmesh;
	Curve *tmpcu = NULL, *copycu;
	Object *tmpobj = NULL;
	int render = settings == eModifierMode_Render, i;
	int cage = !apply_modifiers;

	/* perform the mesh extraction based on type */
	switch (ob->type) {
		case OB_FONT:
		case OB_CURVE:
		case OB_SURF:
		{
			ListBase dispbase = {NULL, NULL};
			DerivedMesh *derivedFinal = NULL;
			int uv_from_orco;

			int (*orco_index)[4] = NULL;
			float (*orco)[3] = NULL;

			/* copies object and modifiers (but not the data) */
			tmpobj = BKE_object_copy_with_caches(ob);
			tmpcu = (Curve *)tmpobj->data;
			tmpcu->id.us--;

			/* if getting the original caged mesh, delete object modifiers */
			if (cage)
				BKE_object_free_modifiers(tmpobj);

			/* copies the data */
			copycu = tmpobj->data = BKE_curve_copy((Curve *) ob->data);

			/* temporarily set edit so we get updates from edit mode, but
			 * also because for text datablocks copying it while in edit
			 * mode gives invalid data structures */
			copycu->editfont = tmpcu->editfont;
			copycu->editnurb = tmpcu->editnurb;

			/* get updated display list, and convert to a mesh */
			BKE_displist_make_curveTypes_forRender(sce, tmpobj, &dispbase, &derivedFinal, FALSE);

			copycu->editfont = NULL;
			copycu->editnurb = NULL;

			tmpobj->derivedFinal = derivedFinal;

			uv_from_orco = (tmpcu->flag & CU_UV_ORCO) != 0;

			if (uv_from_orco) {
				/* before curve conversion */
				orco = (float (*)[3])BKE_curve_make_orco(sce, tmpobj);
			}

			/* convert object type to mesh */
			BKE_mesh_from_nurbs_displist(tmpobj, &dispbase, uv_from_orco ? (int **)&orco_index : NULL);

			tmpmesh = tmpobj->data;

			if (uv_from_orco && orco && orco_index) {
				const char *uvname = "Orco";
				/* add UV's */
				MTexPoly *mtpoly  = CustomData_add_layer_named(&tmpmesh->pdata, CD_MTEXPOLY, CD_DEFAULT, NULL, tmpmesh->totpoly, uvname);
				MLoopUV *mloopuvs = CustomData_add_layer_named(&tmpmesh->ldata, CD_MLOOPUV,  CD_DEFAULT, NULL, tmpmesh->totloop, uvname);

				BKE_mesh_nurbs_to_mdata_orco(tmpmesh->mpoly, tmpmesh->totpoly,
				                             tmpmesh->mloop, mloopuvs,
				                             orco, orco_index);

				(void)mtpoly;
			}

			if (orco_index) {
				MEM_freeN(orco_index);
			}
			if (orco) {
				MEM_freeN(orco);
			}

			BKE_displist_free(&dispbase);

			/* BKE_mesh_from_nurbs changes the type to a mesh, check it worked */
			if (tmpobj->type != OB_MESH) {
				BKE_libblock_free_us(&(G.main->object), tmpobj);
				BKE_report(reports, RPT_ERROR, "Cannot convert curve to mesh (does the curve have any segments?)");
				return NULL;
			}

			BKE_libblock_free_us(&G.main->object, tmpobj);
			break;
		}

		case OB_MBALL:
		{
			/* metaballs don't have modifiers, so just convert to mesh */
			Object *basis_ob = BKE_mball_basis_find(sce, ob);
			/* todo, re-generatre for render-res */
			/* metaball_polygonize(scene, ob) */

			if (ob != basis_ob)
				return NULL;  /* only do basis metaball */
			
			tmpmesh = BKE_mesh_add("Mesh");
			/* BKE_mesh_add gives us a user count we don't need */
			tmpmesh->id.us--;

			if (render) {
				ListBase disp = {NULL, NULL};
				BKE_displist_make_mball_forRender(sce, ob, &disp);
				BKE_mesh_from_metaball(&disp, tmpmesh);
				BKE_displist_free(&disp);
			}
			else
				BKE_mesh_from_metaball(&ob->disp, tmpmesh);

			break;

		}
		case OB_MESH:
			/* copies object and modifiers (but not the data) */
			if (cage) {
				/* copies the data */
				tmpmesh = BKE_mesh_copy(ob->data);
				/* if not getting the original caged mesh, get final derived mesh */
			}
			else {
				/* Make a dummy mesh, saves copying */
				DerivedMesh *dm;
				/* CustomDataMask mask = CD_MASK_BAREMESH|CD_MASK_MTFACE|CD_MASK_MCOL; */
				CustomDataMask mask = CD_MASK_MESH; /* this seems more suitable, exporter,
			                                         * for example, needs CD_MASK_MDEFORMVERT */

				/* Write the display mesh into the dummy mesh */
				if (render)
					dm = mesh_create_derived_render(sce, ob, mask);
				else
					dm = mesh_create_derived_view(sce, ob, mask);
		
				tmpmesh = BKE_mesh_add("Mesh");
				DM_to_mesh(dm, tmpmesh, ob);
				dm->release(dm);
			}

			/* BKE_mesh_add/copy gives us a user count we don't need */
			tmpmesh->id.us--;

			break;
		default:
			BKE_report(reports, RPT_ERROR, "Object does not have geometry data");
			return NULL;
	}

	/* Copy materials to new mesh */
	switch (ob->type) {
		case OB_SURF:
		case OB_FONT:
		case OB_CURVE:
			tmpmesh->totcol = tmpcu->totcol;

			/* free old material list (if it exists) and adjust user counts */
			if (tmpcu->mat) {
				for (i = tmpcu->totcol; i-- > 0; ) {
					/* are we an object material or data based? */

					tmpmesh->mat[i] = ob->matbits[i] ? ob->mat[i] : tmpcu->mat[i];

					if (tmpmesh->mat[i]) {
						tmpmesh->mat[i]->id.us++;
					}
				}
			}
			break;

#if 0
		/* Crashes when assigning the new material, not sure why */
		case OB_MBALL:
			tmpmb = (MetaBall *)ob->data;
			tmpmesh->totcol = tmpmb->totcol;

			/* free old material list (if it exists) and adjust user counts */
			if (tmpmb->mat) {
				for (i = tmpmb->totcol; i-- > 0; ) {
					tmpmesh->mat[i] = tmpmb->mat[i]; /* CRASH HERE ??? */
					if (tmpmesh->mat[i]) {
						tmpmb->mat[i]->id.us++;
					}
				}
			}
			break;
#endif

		case OB_MESH:
			if (!cage) {
				Mesh *origmesh = ob->data;
				tmpmesh->flag = origmesh->flag;
				tmpmesh->mat = MEM_dupallocN(origmesh->mat);
				tmpmesh->totcol = origmesh->totcol;
				tmpmesh->smoothresh = origmesh->smoothresh;
				if (origmesh->mat) {
					for (i = origmesh->totcol; i-- > 0; ) {
						/* are we an object material or data based? */
						tmpmesh->mat[i] = ob->matbits[i] ? ob->mat[i] : origmesh->mat[i];

						if (tmpmesh->mat[i]) {
							tmpmesh->mat[i]->id.us++;
						}
					}
				}
			}
			break;
	} /* end copy materials */

	/* cycles and exporters rely on this still */
	BKE_mesh_tessface_ensure(tmpmesh);

	/* make sure materials get updated in objects */
	test_object_materials(&tmpmesh->id);

	return tmpmesh;
}

/* mostly a copy from convertblender.c */
static void dupli_render_particle_set(Scene *scene, Object *ob, int level, int enable)
{
	/* ugly function, but we need to set particle systems to their render
	 * settings before calling object_duplilist, to get render level duplis */
	Group *group;
	GroupObject *go;
	ParticleSystem *psys;
	DerivedMesh *dm;
	float mat[4][4];

	unit_m4(mat);

	if (level >= MAX_DUPLI_RECUR)
		return;
	
	if (ob->transflag & OB_DUPLIPARTS) {
		for (psys = ob->particlesystem.first; psys; psys = psys->next) {
			if (ELEM(psys->part->ren_as, PART_DRAW_OB, PART_DRAW_GR)) {
				if (enable)
					psys_render_set(ob, psys, mat, mat, 1, 1, 0.f);
				else
					psys_render_restore(ob, psys);
			}
		}

		if (enable) {
			/* this is to make sure we get render level duplis in groups:
			 * the derivedmesh must be created before init_render_mesh,
			 * since object_duplilist does dupliparticles before that */
			dm = mesh_create_derived_render(scene, ob, CD_MASK_BAREMESH | CD_MASK_MTFACE | CD_MASK_MCOL);
			dm->release(dm);

			for (psys = ob->particlesystem.first; psys; psys = psys->next)
				psys_get_modifier(ob, psys)->flag &= ~eParticleSystemFlag_psys_updated;
		}
	}

	if (ob->dup_group == NULL) return;
	group = ob->dup_group;

	for (go = group->gobject.first; go; go = go->next)
		dupli_render_particle_set(scene, go->ob, level + 1, enable);
}
/* When no longer needed, duplilist should be freed with Object.free_duplilist */
static void rna_Object_create_duplilist(Object *ob, ReportList *reports, Scene *sce, int settings)
{
	int for_render = settings == eModifierMode_Render;

	if (!(ob->transflag & OB_DUPLI)) {
		BKE_report(reports, RPT_ERROR, "Object does not have duplis");
		return;
	}

	/* free duplilist if a user forgets to */
	if (ob->duplilist) {
		BKE_report(reports, RPT_WARNING, "Object.dupli_list has not been freed");

		free_object_duplilist(ob->duplilist);
		ob->duplilist = NULL;
	}
	if (G.is_rendering)
		dupli_render_particle_set(sce, ob, 0, 1);
	ob->duplilist = object_duplilist(sce, ob, for_render);
	if (G.is_rendering)
		dupli_render_particle_set(sce, ob, 0, 0);
	/* ob->duplilist should now be freed with Object.free_duplilist */
}

static void rna_Object_free_duplilist(Object *ob)
{
	if (ob->duplilist) {
		free_object_duplilist(ob->duplilist);
		ob->duplilist = NULL;
	}
}

static PointerRNA rna_Object_shape_key_add(Object *ob, bContext *C, ReportList *reports,
                                           const char *name, int from_mix)
{
	Scene *scene = CTX_data_scene(C);
	KeyBlock *kb = NULL;

	if ((kb = BKE_object_insert_shape_key(scene, ob, name, from_mix))) {
		PointerRNA keyptr;

		RNA_pointer_create((ID *)ob->data, &RNA_ShapeKey, kb, &keyptr);
		WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, ob);
		
		return keyptr;
	}
	else {
		BKE_reportf(reports, RPT_ERROR, "Object '%s' does not support shapes", ob->id.name + 2);
		return PointerRNA_NULL;
	}
}

static int rna_Object_is_visible(Object *ob, Scene *sce)
{
	return !(ob->restrictflag & OB_RESTRICT_VIEW) && (ob->lay & sce->lay);
}

#if 0
static void rna_Mesh_assign_verts_to_group(Object *ob, bDeformGroup *group, int *indices, int totindex,
                                           float weight, int assignmode)
{
	if (ob->type != OB_MESH) {
		BKE_report(reports, RPT_ERROR, "Object should be of mesh type");
		return;
	}

	Mesh *me = (Mesh *)ob->data;
	int group_index = BLI_findlink(&ob->defbase, group);
	if (group_index == -1) {
		BKE_report(reports, RPT_ERROR, "No vertex groups assigned to mesh");
		return;
	}

	if (assignmode != WEIGHT_REPLACE && assignmode != WEIGHT_ADD && assignmode != WEIGHT_SUBTRACT) {
		BKE_report(reports, RPT_ERROR, "Bad assignment mode");
		return;
	}

	/* makes a set of dVerts corresponding to the mVerts */
	if (!me->dvert)
		create_dverts(&me->id);

	/* loop list adding verts to group  */
	for (i = 0; i < totindex; i++) {
		if (i < 0 || i >= me->totvert) {
			BKE_report(reports, RPT_ERROR, "Bad vertex index in list");
			return;
		}

		add_vert_defnr(ob, group_index, i, weight, assignmode);
	}
}
#endif

/* BMESH_TODO, return polygon index, not tessface */
static void rna_Object_ray_cast(Object *ob, ReportList *reports, float ray_start[3], float ray_end[3],
                                float r_location[3], float r_normal[3], int *index)
{
	BVHTreeFromMesh treeData = {NULL};
	
	if (ob->derivedFinal == NULL) {
		BKE_reportf(reports, RPT_ERROR, "Object '%s' has no mesh data to be used for ray casting", ob->id.name + 2);
		return;
	}

	/* no need to managing allocation or freeing of the BVH data. this is generated and freed as needed */
	bvhtree_from_mesh_faces(&treeData, ob->derivedFinal, 0.0f, 4, 6);

	if (treeData.tree == NULL) {
		BKE_reportf(reports, RPT_ERROR, "Object '%s' could not create internal data for ray casting", ob->id.name + 2);
		return;
	}
	else {
		BVHTreeRayHit hit;
		float ray_nor[3], dist;
		sub_v3_v3v3(ray_nor, ray_end, ray_start);

		dist = hit.dist = normalize_v3(ray_nor);
		hit.index = -1;
		
		if (BLI_bvhtree_ray_cast(treeData.tree, ray_start, ray_nor, 0.0f, &hit,
		                         treeData.raycast_callback, &treeData) != -1)
		{
			if (hit.dist <= dist) {
				copy_v3_v3(r_location, hit.co);
				copy_v3_v3(r_normal, hit.no);
				*index = hit.index;
				return;
			}
		}
	}

	zero_v3(r_location);
	zero_v3(r_normal);
	*index = -1;
}

static void rna_Object_closest_point_on_mesh(Object *ob, ReportList *reports, float point_co[3], float max_dist,
                                             float n_location[3], float n_normal[3], int *index)
{
	BVHTreeFromMesh treeData = {NULL};
	
	if (ob->derivedFinal == NULL) {
		BKE_reportf(reports, RPT_ERROR, "Object '%s' has no mesh data to be used for finding nearest point",
		            ob->id.name + 2);
		return;
	}

	/* no need to managing allocation or freeing of the BVH data. this is generated and freed as needed */
	bvhtree_from_mesh_faces(&treeData, ob->derivedFinal, 0.0f, 4, 6);

	if (treeData.tree == NULL) {
		BKE_reportf(reports, RPT_ERROR, "Object '%s' could not create internal data for finding nearest point",
		            ob->id.name + 2);
		return;
	}
	else {
		BVHTreeNearest nearest;

		nearest.index = -1;
		nearest.dist = max_dist * max_dist;

		if (BLI_bvhtree_find_nearest(treeData.tree, point_co, &nearest, treeData.nearest_callback, &treeData) != -1) {
			copy_v3_v3(n_location, nearest.co);
			copy_v3_v3(n_normal, nearest.no);
			*index = nearest.index;
			return;
		}
	}

	zero_v3(n_location);
	zero_v3(n_normal);
	*index = -1;
}

/* ObjectBase */

static void rna_ObjectBase_layers_from_view(Base *base, View3D *v3d)
{
	base->lay = base->object->lay = v3d->lay;
}

static int rna_Object_is_modified(Object *ob, Scene *scene, int settings)
{
	return BKE_object_is_modified(scene, ob) & settings;
}

static int rna_Object_is_deform_modified(Object *ob, Scene *scene, int settings)
{
	return BKE_object_is_deform_modified(scene, ob) & settings;
}

#ifndef NDEBUG
void rna_Object_dm_info(struct Object *ob, int type, char *result)
{
	DerivedMesh *dm = NULL;
	int dm_release = FALSE;
	char *ret = NULL;

	result[0] = '\0';

	switch (type) {
		case 0:
			if (ob->type == OB_MESH) {
				dm = CDDM_from_mesh(ob->data, ob);
				ret = DM_debug_info(dm);
				dm_release = TRUE;
			}
			break;
		case 1:
			dm = ob->derivedDeform;
			break;
		case 2:
			dm = ob->derivedFinal;
			break;
	}

	if (dm) {
		ret = DM_debug_info(dm);
		if (dm_release) {
			dm->release(dm);
		}
		if (ret) {
			strcpy(result, ret);
			MEM_freeN(ret);
		}
	}
}
#endif /* NDEBUG */

#else

void RNA_api_object(StructRNA *srna)
{
	FunctionRNA *func;
	PropertyRNA *parm;

	static EnumPropertyItem mesh_type_items[] = {
		{eModifierMode_Realtime, "PREVIEW", 0, "Preview", "Apply modifier preview settings"},
		{eModifierMode_Render, "RENDER", 0, "Render", "Apply modifier render settings"},
		{0, NULL, 0, NULL, NULL}
	};

#ifndef NDEBUG
	static EnumPropertyItem mesh_dm_info_items[] = {
		{0, "SOURCE", 0, "Source", "Source mesh"},
		{1, "DEFORM", 0, "Deform", "Objects deform mesh"},
		{2, "FINAL", 0, "Final", "Objects final mesh"},
		{0, NULL, 0, NULL, NULL}
	};
#endif

	/* mesh */
	func = RNA_def_function(srna, "to_mesh", "rna_Object_to_mesh");
	RNA_def_function_ui_description(func, "Create a Mesh datablock with modifiers applied");
	RNA_def_function_flag(func, FUNC_USE_REPORTS);
	parm = RNA_def_pointer(func, "scene", "Scene", "", "Scene within which to evaluate modifiers");
	RNA_def_property_flag(parm, PROP_REQUIRED | PROP_NEVER_NULL);
	parm = RNA_def_boolean(func, "apply_modifiers", 0, "", "Apply modifiers");
	RNA_def_property_flag(parm, PROP_REQUIRED);
	parm = RNA_def_enum(func, "settings", mesh_type_items, 0, "", "Modifier settings to apply");
	RNA_def_property_flag(parm, PROP_REQUIRED);
	parm = RNA_def_pointer(func, "mesh", "Mesh", "",
	                       "Mesh created from object, remove it if it is only used for export");
	RNA_def_function_return(func, parm);

	/* duplis */
	func = RNA_def_function(srna, "dupli_list_create", "rna_Object_create_duplilist");
	RNA_def_function_ui_description(func, "Create a list of dupli objects for this object, needs to "
	                                "be freed manually with free_dupli_list to restore the "
	                                "objects real matrix and layers");
	parm = RNA_def_pointer(func, "scene", "Scene", "", "Scene within which to evaluate duplis");
	RNA_def_property_flag(parm, PROP_REQUIRED | PROP_NEVER_NULL);
	RNA_def_enum(func, "settings", mesh_type_items, 0, "", "Generate texture coordinates for rendering");
	RNA_def_function_flag(func, FUNC_USE_REPORTS);

	func = RNA_def_function(srna, "dupli_list_clear", "rna_Object_free_duplilist");
	RNA_def_function_ui_description(func, "Free the list of dupli objects");

	/* Armature */
	func = RNA_def_function(srna, "find_armature", "modifiers_isDeformedByArmature");
	RNA_def_function_ui_description(func, "Find armature influencing this object as a parent or via a modifier");
	parm = RNA_def_pointer(func, "ob_arm", "Object", "", "Armature object influencing this object or NULL");
	RNA_def_function_return(func, parm);

	/* Shape key */
	func = RNA_def_function(srna, "shape_key_add", "rna_Object_shape_key_add");
	RNA_def_function_ui_description(func, "Add shape key to an object");
	RNA_def_function_flag(func, FUNC_USE_CONTEXT | FUNC_USE_REPORTS);
	RNA_def_string(func, "name", "Key", 0, "", "Unique name for the new keylock"); /* optional */
	RNA_def_boolean(func, "from_mix", 1, "", "Create new shape from existing mix of shapes");
	parm = RNA_def_pointer(func, "key", "ShapeKey", "", "New shape keyblock");
	RNA_def_property_flag(parm, PROP_RNAPTR);
	RNA_def_function_return(func, parm);

	/* Ray Cast */
	func = RNA_def_function(srna, "ray_cast", "rna_Object_ray_cast");
	RNA_def_function_ui_description(func, "Cast a ray onto in object space");
	RNA_def_function_flag(func, FUNC_USE_REPORTS);
	
	/* ray start and end */
	parm = RNA_def_float_vector(func, "start", 3, NULL, -FLT_MAX, FLT_MAX, "", "", -1e4, 1e4);
	RNA_def_property_flag(parm, PROP_REQUIRED);
	parm = RNA_def_float_vector(func, "end", 3, NULL, -FLT_MAX, FLT_MAX, "", "", -1e4, 1e4);
	RNA_def_property_flag(parm, PROP_REQUIRED);

	/* return location and normal */
	parm = RNA_def_float_vector(func, "location", 3, NULL, -FLT_MAX, FLT_MAX, "Location",
	                            "The hit location of this ray cast", -1e4, 1e4);
	RNA_def_property_flag(parm, PROP_THICK_WRAP);
	RNA_def_function_output(func, parm);
	parm = RNA_def_float_vector(func, "normal", 3, NULL, -FLT_MAX, FLT_MAX, "Normal",
	                            "The face normal at the ray cast hit location", -1e4, 1e4);
	RNA_def_property_flag(parm, PROP_THICK_WRAP);
	RNA_def_function_output(func, parm);
	
	parm = RNA_def_int(func, "index", 0, 0, 0, "", "The face index, -1 when no intersection is found", 0, 0);
	RNA_def_function_output(func, parm);

	/* Nearest Point */
	func = RNA_def_function(srna, "closest_point_on_mesh", "rna_Object_closest_point_on_mesh");
	RNA_def_function_ui_description(func, "Find the nearest point on the object");
	RNA_def_function_flag(func, FUNC_USE_REPORTS);

	/* location of point for test and max distance */
	parm = RNA_def_float_vector(func, "point", 3, NULL, -FLT_MAX, FLT_MAX, "", "", -1e4, 1e4);
	RNA_def_property_flag(parm, PROP_REQUIRED);
	/* default is sqrt(FLT_MAX) */
	RNA_def_float(func, "max_dist", 1.844674352395373e+19, 0.0, FLT_MAX, "", "", 0.0, FLT_MAX);

	/* return location and normal */
	parm = RNA_def_float_vector(func, "location", 3, NULL, -FLT_MAX, FLT_MAX, "Location",
	                            "The location on the object closest to the point", -1e4, 1e4);
	RNA_def_property_flag(parm, PROP_THICK_WRAP);
	RNA_def_function_output(func, parm);
	parm = RNA_def_float_vector(func, "normal", 3, NULL, -FLT_MAX, FLT_MAX, "Normal",
	                            "The face normal at the closest point", -1e4, 1e4);
	RNA_def_property_flag(parm, PROP_THICK_WRAP);
	RNA_def_function_output(func, parm);

	parm = RNA_def_int(func, "index", 0, 0, 0, "", "The face index, -1 when no closest point is found", 0, 0);
	RNA_def_function_output(func, parm);

	/* View */
	func = RNA_def_function(srna, "is_visible", "rna_Object_is_visible");
	RNA_def_function_ui_description(func, "Determine if object is visible in a given scene");
	parm = RNA_def_pointer(func, "scene", "Scene", "", "");
	RNA_def_property_flag(parm, PROP_REQUIRED | PROP_NEVER_NULL);
	parm = RNA_def_boolean(func, "result", 0, "", "Object visibility");
	RNA_def_function_return(func, parm);

	/* utility function for checking if the object is modified */
	func = RNA_def_function(srna, "is_modified", "rna_Object_is_modified");
	RNA_def_function_ui_description(func, "Determine if this object is modified from the base mesh data");
	parm = RNA_def_pointer(func, "scene", "Scene", "", "");
	RNA_def_property_flag(parm, PROP_REQUIRED | PROP_NEVER_NULL);
	parm = RNA_def_enum(func, "settings", mesh_type_items, 0, "", "Modifier settings to apply");
	RNA_def_property_flag(parm, PROP_REQUIRED);
	parm = RNA_def_boolean(func, "result", 0, "", "Object visibility");
	RNA_def_function_return(func, parm);

	func = RNA_def_function(srna, "is_deform_modified", "rna_Object_is_deform_modified");
	RNA_def_function_ui_description(func, "Determine if this object is modified by a deformation from the base mesh data");
	parm = RNA_def_pointer(func, "scene", "Scene", "", "");
	RNA_def_property_flag(parm, PROP_REQUIRED | PROP_NEVER_NULL);
	parm = RNA_def_enum(func, "settings", mesh_type_items, 0, "", "Modifier settings to apply");
	RNA_def_property_flag(parm, PROP_REQUIRED);
	parm = RNA_def_boolean(func, "result", 0, "", "Object visibility");
	RNA_def_function_return(func, parm);

#ifndef NDEBUG
	/* mesh */
	func = RNA_def_function(srna, "dm_info", "rna_Object_dm_info");
	RNA_def_function_ui_description(func, "Returns a string for derived mesh data");

	parm = RNA_def_enum(func, "type", mesh_dm_info_items, 0, "", "Modifier settings to apply");
	RNA_def_property_flag(parm, PROP_REQUIRED);
	/* weak!, no way to return dynamic string type */
	parm = RNA_def_string(func, "result", "", 16384, "result", "");
	RNA_def_property_flag(parm, PROP_THICK_WRAP); /* needed for string return value */
	RNA_def_function_output(func, parm);
#endif /* NDEBUG */
}


void RNA_api_object_base(StructRNA *srna)
{
	FunctionRNA *func;
	PropertyRNA *parm;

	func = RNA_def_function(srna, "layers_from_view", "rna_ObjectBase_layers_from_view");
	RNA_def_function_ui_description(func,
	                                "Sets the object layers from a 3D View (use when adding an object in local view)");
	parm = RNA_def_pointer(func, "view", "SpaceView3D", "", "");
	RNA_def_property_flag(parm, PROP_REQUIRED | PROP_NEVER_NULL);
}

#endif

