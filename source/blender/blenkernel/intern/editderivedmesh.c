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
 * The Original Code is Copyright (C) 2005 Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/editderivedmesh.c
 *  \ingroup bke
 */

#include <string.h>
#include <limits.h>
#include <math.h>

#include "BLI_utildefines.h"
#include "BLI_blenlib.h"
#include "BLI_edgehash.h"
#include "BLI_math.h"
#include "BLI_pbvh.h"

#include "BKE_cdderivedmesh.h"
#include "BKE_global.h"
#include "BKE_mesh.h"
#include "BKE_paint.h"


#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"

#include "MEM_guardedalloc.h"

#include "GPU_buffers.h"
#include "GPU_draw.h"
#include "GPU_extensions.h"
#include "GPU_material.h"
#include "GPU_compatibility.h"

/* bmesh */
#include "BKE_tessmesh.h"
#include "BLI_array.h"
#include "BLI_scanfill.h"

#include "bmesh.h"
/* end bmesh */

extern GLubyte stipple_quarttone[128]; /* glutil.c, bad level data */


BMEditMesh *BMEdit_Create(BMesh *bm, int do_tessellate)
{
	BMEditMesh *tm = MEM_callocN(sizeof(BMEditMesh), __func__);

	tm->bm = bm;
	if (do_tessellate) {
		BMEdit_RecalcTessellation(tm);
	}

	return tm;
}

BMEditMesh *BMEdit_Copy(BMEditMesh *tm)
{
	BMEditMesh *tm2 = MEM_callocN(sizeof(BMEditMesh), __func__);
	*tm2 = *tm;

	tm2->derivedCage = tm2->derivedFinal = NULL;

	tm2->bm = BM_mesh_copy(tm->bm);

	/* The tessellation is NOT calculated on the copy here,
	 * because currently all the callers of this function use
	 * it to make a backup copy of the BMEditMesh to restore
	 * it in the case of errors in an operation. For perf
	 * reasons, in that case it makes more sense to do the
	 * tessellation only when/if that copy ends up getting
	 * used.*/
	tm2->looptris = NULL;

	tm2->vert_index = NULL;
	tm2->edge_index = NULL;
	tm2->face_index = NULL;

	return tm2;
}

static void BMEdit_RecalcTessellation_intern(BMEditMesh *tm)
{
	/* use this to avoid locking pthread for _every_ polygon
	 * and calling the fill function */
#define USE_TESSFACE_SPEEDUP

	BMesh *bm = tm->bm;
	BMLoop *(*looptris)[3] = NULL;
	BLI_array_declare(looptris);
	BMIter iter, liter;
	BMFace *efa;
	BMLoop *l;
	int i = 0, j;

	ScanFillContext sf_ctx;

#if 0
	/* note, we could be clever and re-use this array but would need to ensure
	 * its realloced at some point, for now just free it */
	if (tm->looptris) MEM_freeN(tm->looptris);

	/* Use tm->tottri when set, this means no reallocs while transforming,
	 * (unless scanfill fails), otherwise... */
	/* allocate the length of totfaces, avoid many small reallocs,
	 * if all faces are tri's it will be correct, quads == 2x allocs */
	BLI_array_reserve(looptris, (tm->tottri && tm->tottri < bm->totface * 3) ? tm->tottri : bm->totface);
#else

	/* this means no reallocs for quad dominant models, for */
	if ( (tm->looptris != NULL) &&
	     (tm->tottri != 0) &&
	     /* (totrti <= bm->totface * 2) would be fine for all quads,
	      * but in case there are some ngons, still re-use the array */
	     (tm->tottri <= bm->totface * 3))
	{
		looptris = tm->looptris;
	}
	else {
		if (tm->looptris) MEM_freeN(tm->looptris);
		BLI_array_reserve(looptris, bm->totface);
	}

#endif

	BM_ITER_MESH (efa, &iter, bm, BM_FACES_OF_MESH) {
		/* don't consider two-edged faces */
		if (UNLIKELY(efa->len < 3)) {
			/* do nothing */
		}

#ifdef USE_TESSFACE_SPEEDUP

		/* no need to ensure the loop order, we know its ok */

		else if (efa->len == 3) {
			BLI_array_grow_one(looptris);
			BM_ITER_ELEM_INDEX (l, &liter, efa, BM_LOOPS_OF_FACE, j) {
				looptris[i][j] = l;
			}
			i += 1;
		}
		else if (efa->len == 4) {
			BMLoop *ltmp[4];
			BLI_array_grow_items(looptris, 2);

			BM_ITER_ELEM_INDEX (l, &liter, efa, BM_LOOPS_OF_FACE, j) {
				ltmp[j] = l;
			}

			looptris[i][0] = ltmp[0];
			looptris[i][1] = ltmp[1];
			looptris[i][2] = ltmp[2];
			i += 1;

			looptris[i][0] = ltmp[0];
			looptris[i][1] = ltmp[2];
			looptris[i][2] = ltmp[3];
			i += 1;
		}

#endif /* USE_TESSFACE_SPEEDUP */

		else {
			ScanFillVert *sf_vert, *sf_vert_last = NULL, *sf_vert_first = NULL;
			/* ScanFillEdge *e; */ /* UNUSED */
			ScanFillFace *sf_tri;
			int totfilltri;

			BLI_scanfill_begin(&sf_ctx);

			/* scanfill time */
			BM_ITER_ELEM_INDEX (l, &liter, efa, BM_LOOPS_OF_FACE, j) {
				/*mark order */
				BM_elem_index_set(l, j); /* set_loop */

				sf_vert = BLI_scanfill_vert_add(&sf_ctx, l->v->co);
				sf_vert->tmp.p = l;

				if (sf_vert_last) {
					/* e = */ BLI_scanfill_edge_add(&sf_ctx, sf_vert_last, sf_vert);
				}

				sf_vert_last = sf_vert;
				if (sf_vert_first == NULL) sf_vert_first = sf_vert;
			}

			/* complete the loop */
			BLI_scanfill_edge_add(&sf_ctx, sf_vert_first, sf_vert);

			totfilltri = BLI_scanfill_calc_ex(&sf_ctx, FALSE, efa->no);
			BLI_array_grow_items(looptris, totfilltri);

			for (sf_tri = sf_ctx.fillfacebase.first; sf_tri; sf_tri = sf_tri->next) {
				BMLoop *l1 = sf_tri->v1->tmp.p;
				BMLoop *l2 = sf_tri->v2->tmp.p;
				BMLoop *l3 = sf_tri->v3->tmp.p;

				if (BM_elem_index_get(l1) > BM_elem_index_get(l2)) { SWAP(BMLoop *, l1, l2); }
				if (BM_elem_index_get(l2) > BM_elem_index_get(l3)) { SWAP(BMLoop *, l2, l3); }
				if (BM_elem_index_get(l1) > BM_elem_index_get(l2)) { SWAP(BMLoop *, l1, l2); }

				looptris[i][0] = l1;
				looptris[i][1] = l2;
				looptris[i][2] = l3;
				i += 1;
			}

			BLI_scanfill_end(&sf_ctx);
		}
	}

	tm->tottri = i;
	tm->looptris = looptris;

#undef USE_TESSFACE_SPEEDUP

}

void BMEdit_RecalcTessellation(BMEditMesh *em)
{
	BMEdit_RecalcTessellation_intern(em);

	/* commented because editbmesh_build_data() ensures we get tessfaces */
#if 0
	if (em->derivedFinal && em->derivedFinal == em->derivedCage) {
		if (em->derivedFinal->recalcTessellation)
			em->derivedFinal->recalcTessellation(em->derivedFinal);
	}
	else if (em->derivedFinal) {
		if (em->derivedCage->recalcTessellation)
			em->derivedCage->recalcTessellation(em->derivedCage);
		if (em->derivedFinal->recalcTessellation)
			em->derivedFinal->recalcTessellation(em->derivedFinal);
	}
#endif
}

void BMEdit_UpdateLinkedCustomData(BMEditMesh *em)
{
	BMesh *bm = em->bm;
	int act;

	if (CustomData_has_layer(&bm->pdata, CD_MTEXPOLY)) {
		act = CustomData_get_active_layer(&bm->pdata, CD_MTEXPOLY);
		CustomData_set_layer_active(&bm->ldata, CD_MLOOPUV, act);

		act = CustomData_get_render_layer(&bm->pdata, CD_MTEXPOLY);
		CustomData_set_layer_render(&bm->ldata, CD_MLOOPUV, act);

		act = CustomData_get_clone_layer(&bm->pdata, CD_MTEXPOLY);
		CustomData_set_layer_clone(&bm->ldata, CD_MLOOPUV, act);

		act = CustomData_get_stencil_layer(&bm->pdata, CD_MTEXPOLY);
		CustomData_set_layer_stencil(&bm->ldata, CD_MLOOPUV, act);
	}
}

/*does not free the BMEditMesh struct itself*/
void BMEdit_Free(BMEditMesh *em)
{
	if (em->derivedFinal) {
		if (em->derivedFinal != em->derivedCage) {
			em->derivedFinal->needsFree = 1;
			em->derivedFinal->release(em->derivedFinal);
		}
		em->derivedFinal = NULL;
	}
	if (em->derivedCage) {
		em->derivedCage->needsFree = 1;
		em->derivedCage->release(em->derivedCage);
		em->derivedCage = NULL;
	}

	if (em->looptris) MEM_freeN(em->looptris);

	if (em->vert_index) MEM_freeN(em->vert_index);
	if (em->edge_index) MEM_freeN(em->edge_index);
	if (em->face_index) MEM_freeN(em->face_index);

	if (em->bm)
		BM_mesh_free(em->bm);
}

/*
 * ok, basic design:
 *
 * the bmesh derivedmesh exposes the mesh as triangles.  it stores pointers
 * to three loops per triangle.  the derivedmesh stores a cache of tessellations
 * for each face.  this cache will smartly update as needed (though at first
 * it'll simply be more brute force).  keeping track of face/edge counts may
 * be a small problbm.
 *
 * this won't be the most efficient thing, considering that internal edges and
 * faces of tessellations are exposed.  looking up an edge by index in particular
 * is likely to be a little slow.
 */

typedef struct EditDerivedBMesh {
	DerivedMesh dm;

	Object *ob;
	BMEditMesh *tc;

	float (*vertexCos)[3];
	float (*vertexNos)[3];
	float (*polyNos)[3];

	/* private variables, for number of verts/edges/faces
	 * within the above hash/table members */
	int tv, te, tf;
} EditDerivedBMesh;

static void emDM_calcNormals(DerivedMesh *UNUSED(dm))
{
	/* Nothing to do: normals are already calculated and stored on the
	 * BMVerts and BMFaces */
}

static void emDM_recalcTessellation(DerivedMesh *UNUSED(dm))
{
	/* do nothing */
}

static void emDM_foreachMappedVert(DerivedMesh *dm,
                                   void (*func)(void *userData, int index, const float co[3], const float no_f[3], const short no_s[3]),
                                   void *userData)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;
	BMVert *eve;
	BMIter iter;
	int i;

	if (bmdm->vertexCos) {
		BM_ITER_MESH_INDEX (eve, &iter, bmdm->tc->bm, BM_VERTS_OF_MESH, i) {
			func(userData, i, bmdm->vertexCos[i], bmdm->vertexNos[i], NULL);
		}
	}
	else {
		BM_ITER_MESH_INDEX (eve, &iter, bmdm->tc->bm, BM_VERTS_OF_MESH, i) {
			func(userData, i, eve->co, eve->no, NULL);
		}
	}
}
static void emDM_foreachMappedEdge(DerivedMesh *dm,
                                   void (*func)(void *userData, int index, const float v0co[3], const float v1co[3]),
                                   void *userData)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;
	BMEdge *eed;
	BMIter iter;
	int i;

	if (bmdm->vertexCos) {

		BM_mesh_elem_index_ensure(bmdm->tc->bm, BM_VERT);

		BM_ITER_MESH_INDEX (eed, &iter, bmdm->tc->bm, BM_EDGES_OF_MESH, i) {
			func(userData, i,
			     bmdm->vertexCos[BM_elem_index_get(eed->v1)],
			     bmdm->vertexCos[BM_elem_index_get(eed->v2)]);
		}
	}
	else {
		BM_ITER_MESH_INDEX (eed, &iter, bmdm->tc->bm, BM_EDGES_OF_MESH, i) {
			func(userData, i, eed->v1->co, eed->v2->co);
		}
	}
}

static void emDM_drawMappedEdges(DerivedMesh *dm,
                                 DMSetDrawOptions setDrawOptions,
                                 void *userData)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;
	BMEdge *eed;
	BMIter iter;
	int i;

	gpuBegin(GL_LINES);

	if (bmdm->vertexCos) {
		BM_mesh_elem_index_ensure(bmdm->tc->bm, BM_VERT);

		BM_ITER_MESH_INDEX (eed, &iter, bmdm->tc->bm, BM_EDGES_OF_MESH, i) {
			if (!setDrawOptions || (setDrawOptions(userData, i) != DM_DRAW_OPTION_SKIP)) {
				gpuVertex3fv(bmdm->vertexCos[BM_elem_index_get(eed->v1)]);
				gpuVertex3fv(bmdm->vertexCos[BM_elem_index_get(eed->v2)]);
			}
		}
	}
	else {
		BM_ITER_MESH_INDEX (eed, &iter, bmdm->tc->bm, BM_EDGES_OF_MESH, i) {
			if (!setDrawOptions || (setDrawOptions(userData, i) != DM_DRAW_OPTION_SKIP)) {
				gpuVertex3fv(eed->v1->co);
				gpuVertex3fv(eed->v2->co);
			}
		}
	}

	gpuEnd();
}
static void emDM_drawEdges(DerivedMesh *dm,
                           int UNUSED(drawLooseEdges),
                           int UNUSED(drawAllEdges))
{
	emDM_drawMappedEdges(dm, NULL, NULL);
}

static void emDM_drawMappedEdgesInterp(DerivedMesh *dm,
                                       DMSetDrawOptions setDrawOptions,
                                       DMSetDrawInterpOptions setDrawInterpOptions,
                                       void *userData)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;
	BMEdge *eed;
	BMIter iter;
	int i;

	if (bmdm->vertexCos) {

		BM_mesh_elem_index_ensure(bmdm->tc->bm, BM_VERT);

		gpuBegin(GL_LINES);
		BM_ITER_MESH_INDEX (eed, &iter, bmdm->tc->bm, BM_EDGES_OF_MESH, i) {
			if (!setDrawOptions || (setDrawOptions(userData, i) != DM_DRAW_OPTION_SKIP)) {
				setDrawInterpOptions(userData, i, 0.0);
				gpuVertex3fv(bmdm->vertexCos[BM_elem_index_get(eed->v1)]);
				setDrawInterpOptions(userData, i, 1.0);
				gpuVertex3fv(bmdm->vertexCos[BM_elem_index_get(eed->v2)]);
			}
		}
		gpuEnd();
	}
	else {
		gpuBegin(GL_LINES);
		BM_ITER_MESH_INDEX (eed, &iter, bmdm->tc->bm, BM_EDGES_OF_MESH, i) {
			if (!setDrawOptions || (setDrawOptions(userData, i) != DM_DRAW_OPTION_SKIP)) {
				setDrawInterpOptions(userData, i, 0.0);
				gpuVertex3fv(eed->v1->co);
				setDrawInterpOptions(userData, i, 1.0);
				gpuVertex3fv(eed->v2->co);
			}
		}
		gpuEnd();
	}
}

static void emDM_drawUVEdges(DerivedMesh *dm)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;
	BMEditMesh *em = bmdm->tc;
	BMFace *efa;
	BMIter iter;

	gpuImmediateFormat_V3();

	gpuBegin(GL_LINES);
	BM_ITER_MESH (efa, &iter, em->bm, BM_FACES_OF_MESH) {
		BMIter liter;
		BMLoop *l;
		MLoopUV *lastluv = NULL, *firstluv = NULL;

		if (BM_elem_flag_test(efa, BM_ELEM_HIDDEN))
			continue;

		BM_ITER_ELEM (l, &liter, efa, BM_LOOPS_OF_FACE) {
			MLoopUV *luv = CustomData_bmesh_get(&em->bm->ldata, l->head.data, CD_MLOOPUV);

			if (luv) {
				if (lastluv)
					gpuVertex2fv(luv->uv);

				gpuVertex2fv(luv->uv);

				lastluv = luv;

				if (!firstluv)
					firstluv = luv;
			}
		}

		if (lastluv) {
			gpuVertex2fv(lastluv->uv);
			gpuVertex2fv(firstluv->uv);
		}
	}
	gpuEnd();

	gpuImmediateUnformat();
}

static void emDM__calcFaceCent(BMFace *efa, float cent[3], float (*vertexCos)[3])
{
	BMIter liter;
	BMLoop *l;
	int tot = 0;

	zero_v3(cent);

	/*simple (and stupid) median (average) based method :/ */

	if (vertexCos) {
		BM_ITER_ELEM (l, &liter, efa, BM_LOOPS_OF_FACE) {
			add_v3_v3(cent, vertexCos[BM_elem_index_get(l->v)]);
			tot++;
		}
	}
	else {
		BM_ITER_ELEM (l, &liter, efa, BM_LOOPS_OF_FACE) {
			add_v3_v3(cent, l->v->co);
			tot++;
		}
	}

	if (tot == 0) return;
	mul_v3_fl(cent, 1.0f / (float)tot);
}

static void emDM_foreachMappedFaceCenter(DerivedMesh *dm,
                                         void (*func)(void *userData, int index, const float co[3], const float no[3]),
                                         void *userData)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;
	float (*polyNos)[3] = NULL;
	BMFace *efa;
	BMIter iter;
	float cent[3];
	int i;

	/* ensure for face center calculation */
	if (bmdm->vertexCos) {
		BM_mesh_elem_index_ensure(bmdm->tc->bm, BM_VERT);
		polyNos = bmdm->polyNos;

		BLI_assert(polyNos != NULL);
	}

	BM_ITER_MESH_INDEX (efa, &iter, bmdm->tc->bm, BM_FACES_OF_MESH, i) {
		emDM__calcFaceCent(efa, cent, bmdm->vertexCos);
		func(userData, i, cent, polyNos ? polyNos[i] : efa->no);
	}
}

static void emDM_drawMappedFaces(
	DerivedMesh *dm,
	DMSetDrawOptions setDrawOptions,
	DMSetMaterial setMaterial,
	DMCompareDrawOptions compareDrawOptions,
	void *userData,
	DMDrawFlag flag)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;
	BMFace *efa;
	struct BMLoop *(*looptris)[3] = bmdm->tc->looptris;
	const int tottri = bmdm->tc->tottri;
	const int lasttri = tottri - 1; /* compare agasint this a lot */
	DMDrawOption draw_option;
	int useNormals = flag & DM_DRAW_USE_NORMALS;
	int i, flush;

	/* GL_NOOP is used to detect if drawing has started or not */
	GLenum poly_prev  = GL_NOOP;
	GLenum shade_prev = GL_NOOP;

	(void)setMaterial; /* UNUSED */

	/* currently unused -- each original face is handled separately */
	(void)compareDrawOptions;

		if (flag & DM_DRAW_USE_COLORS)
			gpuImmediateFormat_C4_V3();
		else
		if (flag & DM_DRAW_USE_COLORS)
			gpuImmediateFormat_C4_N3_V3();
		else
	if (bmdm->vertexCos) {
		/* add direct access */
		float (*vertexCos)[3] = bmdm->vertexCos;
		float (*vertexNos)[3] = bmdm->vertexNos;
		float (*polyNos)[3]   = bmdm->polyNos;
		// int *triPolyMap = bmdm->triPolyMap;

		BM_mesh_elem_index_ensure(bmdm->tc->bm, BM_VERT | BM_FACE);

		for (i = 0; i < tottri; i++) {
			BMLoop **l = looptris[i];
			int drawSmooth;

			efa = l[0]->f;
			drawSmooth = (flag & DM_DRAW_ALWAYS_SMOOTH) ? 1 : BM_elem_flag_test(efa, BM_ELEM_SMOOTH);

			draw_option = (!setDrawOptions ?
			               DM_DRAW_OPTION_NORMALLY :
			               setDrawOptions(userData, BM_elem_index_get(efa)));
			if (draw_option != DM_DRAW_OPTION_SKIP) {
				const GLenum poly_type = GL_TRIANGLES; /* BMESH NOTE, this is odd but keep it for now to match trunk */
				if (draw_option == DM_DRAW_OPTION_STIPPLE) { /* enabled with stipple */

					if (poly_prev != GL_NOOP) gpuEnd();
					poly_prev = GL_NOOP; /* force gpuBegin */

					glEnable(GL_POLYGON_STIPPLE);
					glPolygonStipple(stipple_quarttone);
				}

				if (!useNormals) {
					if (poly_type != poly_prev) {
						if (poly_prev != GL_NOOP) gpuEnd();
						gpuBegin((poly_prev = poly_type)); /* BMesh: will always be GL_TRIANGLES */
					}
					gpuVertex3fv(vertexCos[BM_elem_index_get(l[0]->v)]);
					gpuVertex3fv(vertexCos[BM_elem_index_get(l[1]->v)]);
					gpuVertex3fv(vertexCos[BM_elem_index_get(l[2]->v)]);
				}
				else {
					const GLenum shade_type = drawSmooth ? GL_SMOOTH : GL_FLAT;
					if (shade_type != shade_prev) {
						if (poly_prev != GL_NOOP) gpuEnd();
						glShadeModel((shade_prev = shade_type)); /* same as below but switch shading */
						gpuBegin((poly_prev = poly_type)); /* BMesh: will always be GL_TRIANGLES */
					}
					if (poly_type != poly_prev) {
						if (poly_prev != GL_NOOP) gpuEnd();
						gpuBegin((poly_prev = poly_type)); /* BMesh: will always be GL_TRIANGLES */
					}

					if (!drawSmooth) {
						gpuNormal3fv(polyNos[BM_elem_index_get(efa)]);
						gpuVertex3fv(vertexCos[BM_elem_index_get(l[0]->v)]);
						gpuVertex3fv(vertexCos[BM_elem_index_get(l[1]->v)]);
						gpuVertex3fv(vertexCos[BM_elem_index_get(l[2]->v)]);
					}
					else {
						gpuNormal3fv(vertexNos[BM_elem_index_get(l[0]->v)]);
						gpuVertex3fv(vertexCos[BM_elem_index_get(l[0]->v)]);
						gpuNormal3fv(vertexNos[BM_elem_index_get(l[1]->v)]);
						gpuVertex3fv(vertexCos[BM_elem_index_get(l[1]->v)]);
						gpuNormal3fv(vertexNos[BM_elem_index_get(l[2]->v)]);
						gpuVertex3fv(vertexCos[BM_elem_index_get(l[2]->v)]);
					}
				}

				flush = (draw_option == DM_DRAW_OPTION_STIPPLE);
				if (useNormals && !flush && (i != lasttri))
					flush |= efa->mat_nr != looptris[i + 1][0]->f->mat_nr;  /* TODO, make this neater */

				if (flush) {
					gpuEnd();
					poly_prev = GL_NOOP; /* force gpuBegin */

					glDisable(GL_POLYGON_STIPPLE);
				}
			}
		}
	}
	else {
		BM_mesh_elem_index_ensure(bmdm->tc->bm, BM_FACE);

		poly_prev = GL_TRIANGLES;
		gpuBegin(GL_TRIANGLES);

		for (i = 0; i < tottri; i++) {
			BMLoop **l = looptris[i];
			int drawSmooth;

			efa = l[0]->f;
			drawSmooth = (flag & DM_DRAW_ALWAYS_SMOOTH) ? 1 : BM_elem_flag_test(efa, BM_ELEM_SMOOTH);

			draw_option = (!setDrawOptions ?
			               DM_DRAW_OPTION_NORMALLY :
			               setDrawOptions(userData, BM_elem_index_get(efa)));
			if (draw_option != DM_DRAW_OPTION_SKIP) {
				const GLenum poly_type = GL_TRIANGLES; /* BMESH NOTE, this is odd but keep it for now to match trunk */
				if (draw_option == DM_DRAW_OPTION_STIPPLE) { /* enabled with stipple */

					if (poly_prev != GL_NOOP) gpuEnd();
					poly_prev = GL_NOOP; /* force gpuBegin */

					glEnable(GL_POLYGON_STIPPLE);
					glPolygonStipple(stipple_quarttone);
				}

				if (!useNormals) {
					if (poly_type != poly_prev) {
						if (poly_prev != GL_NOOP) gpuEnd();
						gpuBegin((poly_prev = poly_type)); /* BMesh: will always be GL_TRIANGLES */
					}
					gpuVertex3fv(l[0]->v->co);
					gpuVertex3fv(l[1]->v->co);
					gpuVertex3fv(l[2]->v->co);
				}
				else {
					const GLenum shade_type = drawSmooth ? GL_SMOOTH : GL_FLAT;
					if (shade_type != shade_prev) {
						if (poly_prev != GL_ZERO) gpuEnd();
						glShadeModel((shade_prev = shade_type)); /* same as below but switch shading */
						gpuBegin((poly_prev = poly_type)); /* BMesh: will always be GL_TRIANGLES */
					}
					if (poly_type != poly_prev) {
						if (poly_prev != GL_NOOP) gpuEnd();
						gpuBegin((poly_prev = poly_type)); /* BMesh: will always be GL_TRIANGLES */
					}

					if (!drawSmooth) {
						gpuNormal3fv(efa->no);
						gpuVertex3fv(l[0]->v->co);
						gpuVertex3fv(l[1]->v->co);
						gpuVertex3fv(l[2]->v->co);
					}
					else {
						gpuNormal3fv(l[0]->v->no);
						gpuVertex3fv(l[0]->v->co);
						gpuNormal3fv(l[1]->v->no);
						gpuVertex3fv(l[1]->v->co);
						gpuNormal3fv(l[2]->v->no);
						gpuVertex3fv(l[2]->v->co);
					}
				}

				flush = (draw_option == DM_DRAW_OPTION_STIPPLE);
				if (useNormals && !flush && (i != lasttri)) {
					flush |= efa->mat_nr != looptris[i + 1][0]->f->mat_nr; /* TODO, make this neater */
				}

				if (flush) {
					gpuEnd();

					glDisable(GL_POLYGON_STIPPLE);

					poly_prev = GL_TRIANGLES;
					gpuBegin(GL_TRIANGLES);
				}
			}
		}
	}

	/* if non zero we know a face was rendered */
	if (poly_prev != GL_NOOP) gpuEnd();
}

static void bmdm_get_tri_tex(BMesh *bm, BMLoop **ls, MLoopUV *luv[3], MLoopCol *lcol[3],
                             int has_uv, int has_col)
{
	if (has_uv) {
		luv[0] = CustomData_bmesh_get(&bm->ldata, ls[0]->head.data, CD_MLOOPUV);
		luv[1] = CustomData_bmesh_get(&bm->ldata, ls[1]->head.data, CD_MLOOPUV);
		luv[2] = CustomData_bmesh_get(&bm->ldata, ls[2]->head.data, CD_MLOOPUV);
	}

	if (has_col) {
		lcol[0] = CustomData_bmesh_get(&bm->ldata, ls[0]->head.data, CD_MLOOPCOL);
		lcol[1] = CustomData_bmesh_get(&bm->ldata, ls[1]->head.data, CD_MLOOPCOL);
		lcol[2] = CustomData_bmesh_get(&bm->ldata, ls[2]->head.data, CD_MLOOPCOL);
	}


}

static void emDM_drawFacesTex_common(DerivedMesh *dm,
                                     DMSetDrawOptionsTex drawParams,
                                     DMSetDrawOptions drawParamsMapped,
                                     DMCompareDrawOptions compareDrawOptions,
                                     void *userData)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;
	BMEditMesh *em = bmdm->tc;
	BMesh *bm = bmdm->tc->bm;
	float (*vertexCos)[3] = bmdm->vertexCos;
	float (*vertexNos)[3] = bmdm->vertexNos;
	BMFace *efa;
	MLoopUV *luv[3], dummyluv = {{0}};
	MLoopCol *lcol[3] = {NULL}, dummylcol = {0};
	int i, has_vcol = CustomData_has_layer(&bm->ldata, CD_MLOOPCOL);
	int has_uv = CustomData_has_layer(&bm->pdata, CD_MTEXPOLY);

	(void) compareDrawOptions;

	luv[0] = luv[1] = luv[2] = &dummyluv;

	dummylcol.r = dummylcol.g = dummylcol.b = dummylcol.a = 255;

	/* always use smooth shading even for flat faces, else vertex colors wont interpolate */
	glShadeModel(GL_SMOOTH);

	BM_mesh_elem_index_ensure(bm, BM_FACE);

	gpuImmediateFormat_T2_C4_N3_V3();

	if (vertexCos) {
		BM_mesh_elem_index_ensure(bm, BM_VERT);

		for (i = 0; i < em->tottri; i++) {
			BMLoop **ls = em->looptris[i];
			MTexPoly *tp = has_uv ? CustomData_bmesh_get(&bm->pdata, ls[0]->f->head.data, CD_MTEXPOLY) : NULL;
			MTFace mtf = {{{0}}};
			/*unsigned char *cp = NULL;*/ /*UNUSED*/
			int drawSmooth = BM_elem_flag_test(ls[0]->f, BM_ELEM_SMOOTH);
			DMDrawOption draw_option;

			efa = ls[0]->f;

			if (has_uv) {
				ME_MTEXFACE_CPY(&mtf, tp);
			}

			if (drawParams)
				draw_option = drawParams(&mtf, has_vcol, efa->mat_nr);
			else if (drawParamsMapped)
				draw_option = drawParamsMapped(userData, BM_elem_index_get(efa));
			else
				draw_option = DM_DRAW_OPTION_NORMALLY;

			if (draw_option != DM_DRAW_OPTION_SKIP) {

				gpuBegin(GL_TRIANGLES);
				if (!drawSmooth) {
					gpuNormal3fv(bmdm->polyNos[BM_elem_index_get(efa)]);

					bmdm_get_tri_tex(bm, ls, luv, lcol, has_uv, has_vcol);

					gpuTexCoord2fv(luv[0]->uv);
					if (lcol[0])
						gpuColor3ubv((const GLubyte *)&(lcol[0]->r));
					gpuVertex3fv(vertexCos[BM_elem_index_get(ls[0]->v)]);

					gpuTexCoord2fv(luv[1]->uv);
					if (lcol[1])
						gpuColor3ubv((const GLubyte *)&(lcol[1]->r));
					gpuVertex3fv(vertexCos[BM_elem_index_get(ls[1]->v)]);

					gpuTexCoord2fv(luv[2]->uv);
					if (lcol[2])
						gpuColor3ubv((const GLubyte *)&(lcol[2]->r));
					gpuVertex3fv(vertexCos[BM_elem_index_get(ls[2]->v)]);
				}
				else {
					bmdm_get_tri_tex(bm, ls, luv, lcol, has_uv, has_vcol);

					gpuTexCoord2fv(luv[0]->uv);
					if (lcol[0])
						gpuColor3ubv((const GLubyte *)&(lcol[0]->r));
					gpuNormal3fv(vertexNos[BM_elem_index_get(ls[0]->v)]);
					gpuVertex3fv(vertexCos[BM_elem_index_get(ls[0]->v)]);

					gpuTexCoord2fv(luv[1]->uv);
					if (lcol[1])
						gpuColor3ubv((const GLubyte *)&(lcol[1]->r));
					gpuNormal3fv(vertexNos[BM_elem_index_get(ls[1]->v)]);
					gpuVertex3fv(vertexCos[BM_elem_index_get(ls[1]->v)]);

					gpuTexCoord2fv(luv[2]->uv);
					if (lcol[2])
						gpuColor3ubv((const GLubyte *)&(lcol[2]->r));
					gpuNormal3fv(vertexNos[BM_elem_index_get(ls[2]->v)]);
					gpuVertex3fv(vertexCos[BM_elem_index_get(ls[2]->v)]);
				}
				gpuEnd();
			}
		}
	}
	else {
		BM_mesh_elem_index_ensure(bm, BM_VERT);

		for (i = 0; i < em->tottri; i++) {
			BMLoop **ls = em->looptris[i];
			MTexPoly *tp = has_uv ? CustomData_bmesh_get(&bm->pdata, ls[0]->f->head.data, CD_MTEXPOLY) : NULL;
			MTFace mtf = {{{0}}};
			/*unsigned char *cp = NULL;*/ /*UNUSED*/
			int drawSmooth = BM_elem_flag_test(ls[0]->f, BM_ELEM_SMOOTH);
			DMDrawOption draw_option;

			efa = ls[0]->f;

			if (has_uv) {
				ME_MTEXFACE_CPY(&mtf, tp);
			}

			if (drawParams)
				draw_option = drawParams(&mtf, has_vcol, efa->mat_nr);
			else if (drawParamsMapped)
				draw_option = drawParamsMapped(userData, BM_elem_index_get(efa));
			else
				draw_option = DM_DRAW_OPTION_NORMALLY;

			if (draw_option != DM_DRAW_OPTION_SKIP) {

				gpuBegin(GL_TRIANGLES);
				if (!drawSmooth) {
					gpuNormal3fv(efa->no);

					bmdm_get_tri_tex(bm, ls, luv, lcol, has_uv, has_vcol);

					if (luv[0])
						gpuTexCoord2fv(luv[0]->uv);
					if (lcol[0])
						gpuColor3ubv((const GLubyte *)&(lcol[0]->r));
					gpuVertex3fv(ls[0]->v->co);

					if (luv[1])
						gpuTexCoord2fv(luv[1]->uv);
					if (lcol[1])
						gpuColor3ubv((const GLubyte *)&(lcol[1]->r));
					gpuVertex3fv(ls[1]->v->co);

					if (luv[2])
						gpuTexCoord2fv(luv[2]->uv);
					if (lcol[2])
						gpuColor3ubv((const GLubyte *)&(lcol[2]->r));
					gpuVertex3fv(ls[2]->v->co);
				}
				else {
					bmdm_get_tri_tex(bm, ls, luv, lcol, has_uv, has_vcol);

					if (luv[0])
						gpuTexCoord2fv(luv[0]->uv);
					if (lcol[0])
						gpuColor3ubv((const GLubyte *)&(lcol[0]->r));
					gpuNormal3fv(ls[0]->v->no);
					gpuVertex3fv(ls[0]->v->co);

					if (luv[1])
						gpuTexCoord2fv(luv[1]->uv);
					if (lcol[1])
						gpuColor3ubv((const GLubyte *)&(lcol[1]->r));
					gpuNormal3fv(ls[1]->v->no);
					gpuVertex3fv(ls[1]->v->co);

					if (luv[2])
						gpuTexCoord2fv(luv[2]->uv);
					if (lcol[2])
						gpuColor3ubv((const GLubyte *)&(lcol[2]->r));
					gpuNormal3fv(ls[2]->v->no);
					gpuVertex3fv(ls[2]->v->co);
				}
				gpuEnd();
			}
		}
	}

	gpuImmediateUnformat();

	glShadeModel(GL_FLAT);
}

static void emDM_drawFacesTex(DerivedMesh *dm,
                              DMSetDrawOptionsTex setDrawOptions,
                              DMCompareDrawOptions compareDrawOptions,
                              void *userData)
{
	emDM_drawFacesTex_common(dm, setDrawOptions, NULL, compareDrawOptions, userData);
}

static void emDM_drawMappedFacesTex(DerivedMesh *dm,
                                    DMSetDrawOptions setDrawOptions,
                                    DMCompareDrawOptions compareDrawOptions,
                                    void *userData)
{
	emDM_drawFacesTex_common(dm, NULL, setDrawOptions, compareDrawOptions, userData);
}

static void emdm_format_attrib_vertex(DMVertexAttribs *attribs)
{
	int b;
	GLint attribMap_f[16];
	GLint attribSize_f[16];
	GLint attrib_f = 0;
	GLint attribMap_ub[16];
	GLint attribSize_ub[16];
	GLint attrib_ub = 0;

	/* orco texture coordinates */
	if (attribs->totorco) {
		attribMap_f[attrib_f] = attribs->orco.gl_index;
		attribSize_f[attrib_f] = 3;
		attrib_f++;
	}

	/* uv texture coordinates */
	for (b = 0; b < attribs->tottface; b++) {
		attribMap_f[attrib_f] = attribs->tface[b].gl_index;
		attribSize_f[attrib_f] = 2;
		attrib_f++;
	}

	/* vertex colors */
	for (b = 0; b < attribs->totmcol; b++) {
		attribMap_ub[attrib_ub] = attribs->mcol[b].gl_index;
		attribSize_ub[attrib_ub] = 4;
		attrib_ub++;
	}

	/* tangent for normal mapping */
	if (attribs->tottang) {
		attribMap_f[attrib_f] = attribs->tang.gl_index;
		attribSize_f[attrib_f] = 3;
		attrib_f++;
	}

	gpuImmediateFormatReset();

	gpuImmediateElementSizes(3, 3, 0);

	gpuImmediateFloatAttribCount(attrib_f);
	gpuImmediateFloatAttribIndexMap(attribMap_f);
	gpuImmediateFloatAttribSizes(attribSize_f);

	gpuImmediateUbyteAttribCount(attrib_ub);
	gpuImmediateUbyteAttribIndexMap(attribMap_ub);
	gpuImmediateUbyteAttribSizes(attribMap_ub);

	gpuImmediateLock();
}

static void emdm_unformat_attrib_vertex(void)
{
	gpuImmediateUnlock();
}

static void emDM_drawMappedFacesGLSL(DerivedMesh *dm,
                                     DMSetMaterial setMaterial,
                                     DMSetDrawOptions setDrawOptions,
                                     void *userData)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;
	BMesh *bm = bmdm->tc->bm;
	BMEditMesh *em = bmdm->tc;
	float (*vertexCos)[3] = bmdm->vertexCos;
	float (*vertexNos)[3] = bmdm->vertexNos;
	BMFace *efa;
	BMLoop **ltri;
	DMVertexAttribs attribs;
	GPUVertexAttribs gattribs;

	int i, b, matnr, new_matnr, do_draw;

	do_draw = FALSE;
	matnr = -1;

	memset(&attribs, 0, sizeof(attribs));

	emdm_format_attrib_vertex(&attribs); /* XXX: jwilkins, just to make this easy to write for now */

	/* always use smooth shading even for flat faces, else vertex colors wont interpolate */
	glShadeModel(GL_SMOOTH);
	BM_mesh_elem_index_ensure(bm, BM_VERT | BM_FACE);

#define PASSATTRIB(loop, eve, vert) {                                               \
		if (attribs.totorco) {                                                      \
			float *orco = attribs.orco.array[BM_elem_index_get(eve)];               \
			gpuVertexAttrib3fv(attribs.orco.gl_index, orco);                        \
		}                                                                           \
		for (b = 0; b < attribs.tottface; b++) {                                    \
			MLoopUV *_luv = CustomData_bmesh_get_n(&bm->ldata, loop->head.data,     \
			                                       CD_MLOOPUV, b);                  \
			gpuVertexAttrib2fv(attribs.tface[b].gl_index, _luv->uv);                \
		}                                                                           \
		for (b = 0; b < attribs.totmcol; b++) {                                     \
			MLoopCol *_cp = CustomData_bmesh_get_n(&bm->ldata, loop->head.data,     \
			                                       CD_MLOOPCOL, b);                 \
			GLubyte _col[4];                                                        \
			_col[0] = _cp->b; _col[1] = _cp->g; _col[2] = _cp->r; _col[3] = _cp->a; \
			gpuVertexAttrib4ubv(attribs.mcol[b].gl_index, _col);                    \
		}                                                                           \
		if (attribs.tottang) {                                                      \
			float *tang = attribs.tang.array[i * 4 + vert];                         \
			gpuVertexAttrib3fv(attribs.tang.gl_index, tang);                        \
		}                                                                           \
	} (void)0


	for (i = 0, ltri = em->looptris[0]; i < em->tottri; i++, ltri += 3) {
		int drawSmooth;

		efa = ltri[0]->f;
		drawSmooth = BM_elem_flag_test(efa, BM_ELEM_SMOOTH);

		if (setDrawOptions && (setDrawOptions(userData, BM_elem_index_get(efa)) == DM_DRAW_OPTION_SKIP))
			continue;

		new_matnr = efa->mat_nr + 1;
		if (new_matnr != matnr) {
			do_draw = setMaterial(matnr = new_matnr, &gattribs);
			if (do_draw) {
				emdm_unformat_attrib_vertex();

				DM_vertex_attributes_from_gpu(dm, &gattribs, &attribs);

				emdm_format_attrib_vertex(&attribs);
			}
		}

		if (do_draw) {
			gpuBegin(GL_TRIANGLES);
			if (!drawSmooth) {
				if (vertexCos) gpuNormal3fv(bmdm->polyNos[BM_elem_index_get(efa)]);
				else gpuNormal3fv(efa->no);

				PASSATTRIB(ltri[0], ltri[0]->v, 0);
				if (vertexCos) gpuVertex3fv(vertexCos[BM_elem_index_get(ltri[0]->v)]);
				else gpuVertex3fv(ltri[0]->v->co);

				PASSATTRIB(ltri[1], ltri[1]->v, 1);
				if (vertexCos) gpuVertex3fv(vertexCos[BM_elem_index_get(ltri[1]->v)]);
				else gpuVertex3fv(ltri[1]->v->co);

				PASSATTRIB(ltri[2], ltri[2]->v, 2);
				if (vertexCos) gpuVertex3fv(vertexCos[BM_elem_index_get(ltri[2]->v)]);
				else gpuVertex3fv(ltri[2]->v->co);
			}
			else {
				PASSATTRIB(ltri[0], ltri[0]->v, 0);
				if (vertexCos) {
					gpuNormal3fv(vertexNos[BM_elem_index_get(ltri[0]->v)]);
					gpuVertex3fv(vertexCos[BM_elem_index_get(ltri[0]->v)]);
				}
				else {
					gpuNormal3fv(ltri[0]->v->no);
					gpuVertex3fv(ltri[0]->v->co);
				}

				PASSATTRIB(ltri[1], ltri[1]->v, 1);
				if (vertexCos) {
					gpuNormal3fv(vertexNos[BM_elem_index_get(ltri[1]->v)]);
					gpuVertex3fv(vertexCos[BM_elem_index_get(ltri[1]->v)]);
				}
				else {
					gpuNormal3fv(ltri[1]->v->no);
					gpuVertex3fv(ltri[1]->v->co);
				}

				PASSATTRIB(ltri[2], ltri[2]->v, 2);
				if (vertexCos) {
					gpuNormal3fv(vertexNos[BM_elem_index_get(ltri[2]->v)]);
					gpuVertex3fv(vertexCos[BM_elem_index_get(ltri[2]->v)]);
				}
				else {
					gpuNormal3fv(ltri[2]->v->no);
					gpuVertex3fv(ltri[2]->v->co);
				}
			}
			gpuEnd();
		}
	}

	emdm_unformat_attrib_vertex();
#undef PASSATTRIB
}

static void emDM_drawFacesGLSL(DerivedMesh *dm,
                               int (*setMaterial)(int, void *attribs))
{
	dm->drawMappedFacesGLSL(dm, setMaterial, NULL, NULL);
}

static void emDM_drawMappedFacesMat(DerivedMesh *dm,
                                    void (*setMaterial)(void *userData, int, void *attribs),
                                    int (*setFace)(void *userData, int index), void *userData)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;
	BMesh *bm = bmdm->tc->bm;
	BMEditMesh *em = bmdm->tc;
	float (*vertexCos)[3] = bmdm->vertexCos;
	float (*vertexNos)[3] = bmdm->vertexNos;
	BMFace *efa;
	BMLoop **ltri;
	DMVertexAttribs attribs = {{{0}}};
	GPUVertexAttribs gattribs;
	int i, b, matnr, new_matnr;

	matnr = -1;

	/* always use smooth shading even for flat faces, else vertex colors wont interpolate */
	glShadeModel(GL_SMOOTH);

	BM_mesh_elem_index_ensure(bm, BM_VERT | BM_FACE);

#define PASSATTRIB(loop, eve, vert) {                                               \
		if (attribs.totorco) {                                                      \
			float *orco = attribs.orco.array[BM_elem_index_get(eve)];               \
			if (attribs.orco.gl_texco)                                              \
				gpuTexCoord3fv(orco);                                               \
			else                                                                    \
				gpuVertexAttrib3fv(attribs.orco.gl_index, orco);                    \
		}                                                                           \
		for (b = 0; b < attribs.tottface; b++) {                                    \
			MLoopUV *_luv = CustomData_bmesh_get_n(&bm->ldata, loop->head.data,     \
			                                       CD_MLOOPUV, b);                  \
			if (attribs.tface[b].gl_texco)                                          \
				gpuTexCoord2fv(_luv->uv);                                           \
			else                                                                    \
				gpuVertexAttrib2fv(attribs.tface[b].gl_index, _luv->uv);            \
		}                                                                           \
		for (b = 0; b < attribs.totmcol; b++) {                                     \
			MLoopCol *_cp = CustomData_bmesh_get_n(&bm->ldata, loop->head.data,     \
			                                       CD_MLOOPCOL, b);                 \
			GLubyte _col[4];                                                        \
			_col[0] = _cp->b; _col[1] = _cp->g; _col[2] = _cp->r; _col[3] = _cp->a; \
			gpuVertexAttrib4ubv(attribs.mcol[b].gl_index, _col);                    \
		}                                                                           \
		if (attribs.tottang) {                                                      \
			float *tang = attribs.tang.array[i * 4 + vert];                         \
			gpuVertexAttrib4fv(attribs.tang.gl_index, tang);                        \
		}                                                                           \
	} (void)0

	for (i = 0, ltri = em->looptris[0]; i < em->tottri; i++, ltri += 3) {
		int drawSmooth;

		efa = ltri[0]->f;
		drawSmooth = BM_elem_flag_test(efa, BM_ELEM_SMOOTH);

		/* face hiding */
		if (setFace && !setFace(userData, BM_elem_index_get(efa)))
			continue;

		/* material */
		new_matnr = efa->mat_nr + 1;
		if (new_matnr != matnr) {
			setMaterial(userData, matnr = new_matnr, &gattribs);
			DM_vertex_attributes_from_gpu(dm, &gattribs, &attribs);
		}

		/* face */
		gpuBegin(GL_TRIANGLES);
		if (!drawSmooth) {
			if (vertexCos) gpuNormal3fv(bmdm->polyNos[BM_elem_index_get(efa)]);
			else gpuNormal3fv(efa->no);

			PASSATTRIB(ltri[0], ltri[0]->v, 0);
			if (vertexCos) gpuVertex3fv(vertexCos[BM_elem_index_get(ltri[0]->v)]);
			else gpuVertex3fv(ltri[0]->v->co);

			PASSATTRIB(ltri[1], ltri[1]->v, 1);
			if (vertexCos) gpuVertex3fv(vertexCos[BM_elem_index_get(ltri[1]->v)]);
			else gpuVertex3fv(ltri[1]->v->co);

			PASSATTRIB(ltri[2], ltri[2]->v, 2);
			if (vertexCos) gpuVertex3fv(vertexCos[BM_elem_index_get(ltri[2]->v)]);
			else gpuVertex3fv(ltri[2]->v->co);

		}
		else {
			PASSATTRIB(ltri[0], ltri[0]->v, 0);
			if (vertexCos) {
				gpuNormal3fv(vertexNos[BM_elem_index_get(ltri[0]->v)]);
				gpuVertex3fv(vertexCos[BM_elem_index_get(ltri[0]->v)]);
			}
			else {
				gpuNormal3fv(ltri[0]->v->no);
				gpuVertex3fv(ltri[0]->v->co);
			}

			PASSATTRIB(ltri[1], ltri[1]->v, 1);
			if (vertexCos) {
				gpuNormal3fv(vertexNos[BM_elem_index_get(ltri[1]->v)]);
				gpuVertex3fv(vertexCos[BM_elem_index_get(ltri[1]->v)]);
			}
			else {
				gpuNormal3fv(ltri[1]->v->no);
				gpuVertex3fv(ltri[1]->v->co);
			}

			PASSATTRIB(ltri[2], ltri[2]->v, 2);
			if (vertexCos) {
				gpuNormal3fv(vertexNos[BM_elem_index_get(ltri[2]->v)]);
				gpuVertex3fv(vertexCos[BM_elem_index_get(ltri[2]->v)]);
			}
			else {
				gpuNormal3fv(ltri[2]->v->no);
				gpuVertex3fv(ltri[2]->v->co);
			}
		}
		gpuEnd();
	}
#undef PASSATTRIB
}

static void emDM_getMinMax(DerivedMesh *dm, float min_r[3], float max_r[3])
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;
	BMVert *eve;
	BMIter iter;
	int i;

	if (bmdm->tc->bm->totvert) {
		if (bmdm->vertexCos) {
			BM_ITER_MESH_INDEX (eve, &iter, bmdm->tc->bm, BM_VERTS_OF_MESH, i) {
				minmax_v3v3_v3(min_r, max_r, bmdm->vertexCos[i]);
			}
		}
		else {
			BM_ITER_MESH (eve, &iter, bmdm->tc->bm, BM_VERTS_OF_MESH) {
				minmax_v3v3_v3(min_r, max_r, eve->co);
			}
		}
	}
	else {
		zero_v3(min_r);
		zero_v3(max_r);
	}
}
static int emDM_getNumVerts(DerivedMesh *dm)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;

	return bmdm->tc->bm->totvert;
}

static int emDM_getNumEdges(DerivedMesh *dm)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;

	return bmdm->tc->bm->totedge;
}

static int emDM_getNumTessFaces(DerivedMesh *dm)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;

	return bmdm->tc->tottri;
}

static int emDM_getNumLoops(DerivedMesh *dm)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;

	return bmdm->tc->bm->totloop;
}

static int emDM_getNumPolys(DerivedMesh *dm)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;

	return bmdm->tc->bm->totface;
}

static int bmvert_to_mvert(BMesh *bm, BMVert *ev, MVert *vert_r)
{
	copy_v3_v3(vert_r->co, ev->co);

	normal_float_to_short_v3(vert_r->no, ev->no);

	vert_r->flag = BM_vert_flag_to_mflag(ev);

	if (CustomData_has_layer(&bm->vdata, CD_BWEIGHT)) {
		vert_r->bweight = (unsigned char) (BM_elem_float_data_get(&bm->vdata, ev, CD_BWEIGHT) * 255.0f);
	}

	return 1;
}

static void emDM_getVert(DerivedMesh *dm, int index, MVert *vert_r)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;
	BMVert *ev;

	if (index < 0 || index >= bmdm->tv) {
		printf("error in emDM_getVert.\n");
		return;
	}

	// ev = EDBM_vert_at_index(bmdm->tc, index);
	ev = BM_vert_at_index(bmdm->tc->bm, index); /* warning, does list loop, _not_ ideal */

	bmvert_to_mvert(bmdm->tc->bm, ev, vert_r);
	if (bmdm->vertexCos)
		copy_v3_v3(vert_r->co, bmdm->vertexCos[index]);
}

static void emDM_getEdge(DerivedMesh *dm, int index, MEdge *edge_r)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;
	BMesh *bm = bmdm->tc->bm;
	BMEdge *e;

	if (index < 0 || index >= bmdm->te) {
		printf("error in emDM_getEdge.\n");
		return;
	}

	// e = EDBM_edge_at_index(bmdm->tc, index);
	e = BM_edge_at_index(bmdm->tc->bm, index); /* warning, does list loop, _not_ ideal */

	if (CustomData_has_layer(&bm->edata, CD_BWEIGHT)) {
		edge_r->bweight = (unsigned char) (BM_elem_float_data_get(&bm->edata, e, CD_BWEIGHT) * 255.0f);
	}

	if (CustomData_has_layer(&bm->edata, CD_CREASE)) {
		edge_r->crease = (unsigned char) (BM_elem_float_data_get(&bm->edata, e, CD_CREASE) * 255.0f);
	}

	edge_r->flag = BM_edge_flag_to_mflag(e);

	edge_r->v1 = BM_elem_index_get(e->v1);
	edge_r->v2 = BM_elem_index_get(e->v2);
}

static void emDM_getTessFace(DerivedMesh *dm, int index, MFace *face_r)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;
	BMFace *ef;
	BMLoop **l;

	if (index < 0 || index >= bmdm->tf) {
		printf("error in emDM_getTessFace.\n");
		return;
	}

	l = bmdm->tc->looptris[index];

	ef = l[0]->f;

	face_r->mat_nr = (unsigned char) ef->mat_nr;
	face_r->flag = BM_face_flag_to_mflag(ef);

	face_r->v1 = BM_elem_index_get(l[0]->v);
	face_r->v2 = BM_elem_index_get(l[1]->v);
	face_r->v3 = BM_elem_index_get(l[2]->v);
	face_r->v4 = 0;

	test_index_face(face_r, NULL, 0, 3);
}

static void emDM_copyVertArray(DerivedMesh *dm, MVert *vert_r)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;
	BMesh *bm = bmdm->tc->bm;
	BMVert *eve;
	BMIter iter;
	const int has_bweight = CustomData_has_layer(&bm->vdata, CD_BWEIGHT);

	if (bmdm->vertexCos) {
		int i;

		BM_ITER_MESH_INDEX (eve, &iter, bm, BM_VERTS_OF_MESH, i) {
			copy_v3_v3(vert_r->co, bmdm->vertexCos[i]);
			normal_float_to_short_v3(vert_r->no, eve->no);
			vert_r->flag = BM_vert_flag_to_mflag(eve);

			if (has_bweight) {
				vert_r->bweight = (unsigned char) (BM_elem_float_data_get(&bm->vdata, eve, CD_BWEIGHT) * 255.0f);
			}
			vert_r++;
		}
	}
	else {
		BM_ITER_MESH (eve, &iter, bm, BM_VERTS_OF_MESH) {
			copy_v3_v3(vert_r->co, eve->co);
			normal_float_to_short_v3(vert_r->no, eve->no);
			vert_r->flag = BM_vert_flag_to_mflag(eve);

			if (has_bweight) {
				vert_r->bweight = (unsigned char) (BM_elem_float_data_get(&bm->vdata, eve, CD_BWEIGHT) * 255.0f);
			}
			vert_r++;
		}
	}
}

static void emDM_copyEdgeArray(DerivedMesh *dm, MEdge *edge_r)
{
	BMesh *bm = ((EditDerivedBMesh *)dm)->tc->bm;
	BMEdge *eed;
	BMIter iter;
	const int has_bweight = CustomData_has_layer(&bm->edata, CD_BWEIGHT);
	const int has_crease = CustomData_has_layer(&bm->edata, CD_CREASE);

	BM_mesh_elem_index_ensure(bm, BM_VERT);

	BM_ITER_MESH (eed, &iter, bm, BM_EDGES_OF_MESH) {
		if (has_bweight) {
			edge_r->bweight = (unsigned char) (BM_elem_float_data_get(&bm->edata, eed, CD_BWEIGHT) * 255.0f);
		}

		if (has_crease) {
			edge_r->crease = (unsigned char) (BM_elem_float_data_get(&bm->edata, eed, CD_CREASE) * 255.0f);
		}

		edge_r->flag = BM_edge_flag_to_mflag(eed);

		edge_r->v1 = BM_elem_index_get(eed->v1);
		edge_r->v2 = BM_elem_index_get(eed->v2);

		edge_r++;
	}
}

static void emDM_copyTessFaceArray(DerivedMesh *dm, MFace *face_r)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;
	BMesh *bm = bmdm->tc->bm;
	BMFace *ef;
	BMLoop **l;
	int i;

	BM_mesh_elem_index_ensure(bm, BM_VERT);

	for (i = 0; i < bmdm->tc->tottri; i++, face_r++) {
		l = bmdm->tc->looptris[i];
		ef = l[0]->f;

		face_r->mat_nr = (unsigned char) ef->mat_nr;

		face_r->flag = BM_face_flag_to_mflag(ef);

		face_r->v1 = BM_elem_index_get(l[0]->v);
		face_r->v2 = BM_elem_index_get(l[1]->v);
		face_r->v3 = BM_elem_index_get(l[2]->v);
		face_r->v4 = 0;

		test_index_face(face_r, NULL, 0, 3);
	}
}

static void emDM_copyLoopArray(DerivedMesh *dm, MLoop *loop_r)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;
	BMesh *bm = bmdm->tc->bm;
	BMIter iter, liter;
	BMFace *efa;
	BMLoop *l;

	BM_mesh_elem_index_ensure(bm, BM_VERT | BM_EDGE);

	BM_ITER_MESH (efa, &iter, bm, BM_FACES_OF_MESH) {
		BM_ITER_ELEM (l, &liter, efa, BM_LOOPS_OF_FACE) {
			loop_r->v = BM_elem_index_get(l->v);
			loop_r->e = BM_elem_index_get(l->e);
			loop_r++;
		}
	}
}

static void emDM_copyPolyArray(DerivedMesh *dm, MPoly *poly_r)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;
	BMesh *bm = bmdm->tc->bm;
	BMIter iter;
	BMFace *efa;
	int i;

	i = 0;
	BM_ITER_MESH (efa, &iter, bm, BM_FACES_OF_MESH) {
		poly_r->flag = BM_face_flag_to_mflag(efa);
		poly_r->loopstart = i;
		poly_r->totloop = efa->len;
		poly_r->mat_nr = efa->mat_nr;

		poly_r++;
		i += efa->len;
	}
}

static void *emDM_getTessFaceDataArray(DerivedMesh *dm, int type)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;
	BMesh *bm = bmdm->tc->bm;
	void *datalayer;

	datalayer = DM_get_tessface_data_layer(dm, type);
	if (datalayer)
		return datalayer;

	/* layers are store per face for editmesh, we convert to a temporary
	 * data layer array in the derivedmesh when these are requested */
	if (type == CD_MTFACE || type == CD_MCOL) {
		const int type_from = (type == CD_MTFACE) ? CD_MTEXPOLY : CD_MLOOPCOL;
		int index;
		char *data, *bmdata;
		index = CustomData_get_layer_index(&bm->pdata, type_from);

		if (index != -1) {
			/* offset = bm->pdata.layers[index].offset; */ /* UNUSED */
			const int size = CustomData_sizeof(type);
			int i, j;

			DM_add_tessface_layer(dm, type, CD_CALLOC, NULL);
			index = CustomData_get_layer_index(&dm->faceData, type);
			dm->faceData.layers[index].flag |= CD_FLAG_TEMPORARY;

			data = datalayer = DM_get_tessface_data_layer(dm, type);

			if (type == CD_MTFACE) {
				for (i = 0; i < bmdm->tc->tottri; i++, data += size) {
					BMFace *efa = bmdm->tc->looptris[i][0]->f;
					bmdata = CustomData_bmesh_get(&bm->pdata, efa->head.data, CD_MTEXPOLY);
					ME_MTEXFACE_CPY(((MTFace *)data), ((MTexPoly *)bmdata));
					for (j = 0; j < 3; j++) {
						bmdata = CustomData_bmesh_get(&bm->ldata, bmdm->tc->looptris[i][j]->head.data, CD_MLOOPUV);
						copy_v2_v2(((MTFace *)data)->uv[j], ((MLoopUV *)bmdata)->uv);
					}
				}
			}
			else {
				for (i = 0; i < bmdm->tc->tottri; i++, data += size) {
					for (j = 0; j < 3; j++) {
						bmdata = CustomData_bmesh_get(&bm->ldata, bmdm->tc->looptris[i][j]->head.data, CD_MLOOPCOL);
						MESH_MLOOPCOL_TO_MCOL(((MLoopCol *)bmdata), (((MCol *)data) + j));
					}
				}
			}
		}
	}

	return datalayer;
}

static void emDM_getVertCos(DerivedMesh *dm, float (*cos_r)[3])
{
	EditDerivedBMesh *emdm = (EditDerivedBMesh *)dm;
	BMVert *eve;
	BMIter iter;
	int i;

	if (emdm->vertexCos) {
		BM_ITER_MESH_INDEX (eve, &iter, emdm->tc->bm, BM_VERTS_OF_MESH, i) {
			copy_v3_v3(cos_r[i], emdm->vertexCos[i]);
		}
	}
	else {
		BM_ITER_MESH_INDEX (eve, &iter, emdm->tc->bm, BM_VERTS_OF_MESH, i) {
			copy_v3_v3(cos_r[i], eve->co);
		}
	}
}

static void emDM_release(DerivedMesh *dm)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;

	if (DM_release(dm)) {
		if (bmdm->vertexCos) {
			MEM_freeN(bmdm->vertexCos);
			MEM_freeN(bmdm->vertexNos);
			MEM_freeN(bmdm->polyNos);
		}

		MEM_freeN(bmdm);
	}
}

static CustomData *bmDm_getVertDataLayout(DerivedMesh *dm)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;

	return &bmdm->tc->bm->vdata;
}

static CustomData *bmDm_getEdgeDataLayout(DerivedMesh *dm)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;

	return &bmdm->tc->bm->edata;
}

static CustomData *bmDm_getTessFaceDataLayout(DerivedMesh *dm)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;

	return &bmdm->dm.faceData;
}

static CustomData *bmDm_getLoopDataLayout(DerivedMesh *dm)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;

	return &bmdm->tc->bm->ldata;
}

static CustomData *bmDm_getPolyDataLayout(DerivedMesh *dm)
{
	EditDerivedBMesh *bmdm = (EditDerivedBMesh *)dm;

	return &bmdm->tc->bm->pdata;
}


DerivedMesh *getEditDerivedBMesh(BMEditMesh *em,
                                 Object *UNUSED(ob),
                                 float (*vertexCos)[3])
{
	EditDerivedBMesh *bmdm = MEM_callocN(sizeof(*bmdm), __func__);
	BMesh *bm = em->bm;

	bmdm->tc = em;

	DM_init((DerivedMesh *)bmdm, DM_TYPE_EDITBMESH, em->bm->totvert,
	        em->bm->totedge, em->tottri, em->bm->totloop, em->bm->totface);

	bmdm->dm.getVertCos = emDM_getVertCos;
	bmdm->dm.getMinMax = emDM_getMinMax;

	bmdm->dm.getVertDataLayout = bmDm_getVertDataLayout;
	bmdm->dm.getEdgeDataLayout = bmDm_getEdgeDataLayout;
	bmdm->dm.getTessFaceDataLayout = bmDm_getTessFaceDataLayout;
	bmdm->dm.getLoopDataLayout = bmDm_getLoopDataLayout;
	bmdm->dm.getPolyDataLayout = bmDm_getPolyDataLayout;

	bmdm->dm.getNumVerts = emDM_getNumVerts;
	bmdm->dm.getNumEdges = emDM_getNumEdges;
	bmdm->dm.getNumTessFaces = emDM_getNumTessFaces;
	bmdm->dm.getNumLoops = emDM_getNumLoops;
	bmdm->dm.getNumPolys = emDM_getNumPolys;

	bmdm->dm.getVert = emDM_getVert;
	bmdm->dm.getEdge = emDM_getEdge;
	bmdm->dm.getTessFace = emDM_getTessFace;
	bmdm->dm.copyVertArray = emDM_copyVertArray;
	bmdm->dm.copyEdgeArray = emDM_copyEdgeArray;
	bmdm->dm.copyTessFaceArray = emDM_copyTessFaceArray;
	bmdm->dm.copyLoopArray = emDM_copyLoopArray;
	bmdm->dm.copyPolyArray = emDM_copyPolyArray;

	bmdm->dm.getTessFaceDataArray = emDM_getTessFaceDataArray;

	bmdm->dm.calcNormals = emDM_calcNormals;
	bmdm->dm.recalcTessellation = emDM_recalcTessellation;

	bmdm->dm.foreachMappedVert = emDM_foreachMappedVert;
	bmdm->dm.foreachMappedEdge = emDM_foreachMappedEdge;
	bmdm->dm.foreachMappedFaceCenter = emDM_foreachMappedFaceCenter;

	bmdm->dm.drawEdges = emDM_drawEdges;
	bmdm->dm.drawMappedEdges = emDM_drawMappedEdges;
	bmdm->dm.drawMappedEdgesInterp = emDM_drawMappedEdgesInterp;
	bmdm->dm.drawMappedFaces = emDM_drawMappedFaces;
	bmdm->dm.drawMappedFacesTex = emDM_drawMappedFacesTex;
	bmdm->dm.drawMappedFacesGLSL = emDM_drawMappedFacesGLSL;
	bmdm->dm.drawMappedFacesMat = emDM_drawMappedFacesMat;
	bmdm->dm.drawFacesTex = emDM_drawFacesTex;
	bmdm->dm.drawFacesGLSL = emDM_drawFacesGLSL;
	bmdm->dm.drawUVEdges = emDM_drawUVEdges;

	bmdm->dm.release = emDM_release;

	bmdm->vertexCos = vertexCos;

	if (CustomData_has_layer(&bm->vdata, CD_MDEFORMVERT)) {
		BMIter iter;
		BMVert *eve;
		int i;

		DM_add_vert_layer(&bmdm->dm, CD_MDEFORMVERT, CD_CALLOC, NULL);

		BM_ITER_MESH_INDEX (eve, &iter, bmdm->tc->bm, BM_VERTS_OF_MESH, i) {
			DM_set_vert_data(&bmdm->dm, i, CD_MDEFORMVERT,
			                 CustomData_bmesh_get(&bm->vdata, eve->head.data, CD_MDEFORMVERT));
		}
	}

	if (CustomData_has_layer(&bm->vdata, CD_MVERT_SKIN)) {
		BMIter iter;
		BMVert *eve;
		int i;

		DM_add_vert_layer(&bmdm->dm, CD_MVERT_SKIN, CD_CALLOC, NULL);

		BM_ITER_MESH_INDEX (eve, &iter, bmdm->tc->bm, BM_VERTS_OF_MESH, i) {
			DM_set_vert_data(&bmdm->dm, i, CD_MVERT_SKIN,
			                 CustomData_bmesh_get(&bm->vdata, eve->head.data,
			                                      CD_MVERT_SKIN));
		}
	}

	if (vertexCos) {
		BMFace *efa;
		BMVert *eve;
		BMIter fiter;
		BMIter viter;
		int i;

		BM_mesh_elem_index_ensure(bm, BM_VERT);

		bmdm->vertexNos = MEM_callocN(sizeof(*bmdm->vertexNos) * bm->totvert, "bmdm_vno");
		bmdm->polyNos = MEM_mallocN(sizeof(*bmdm->polyNos) * bm->totface, "bmdm_pno");

		BM_ITER_MESH_INDEX (efa, &fiter, bm, BM_FACES_OF_MESH, i) {
			BM_elem_index_set(efa, i); /* set_inline */
			BM_face_normal_update_vcos(bm, efa, bmdm->polyNos[i], (float const (*)[3])vertexCos);
		}
		bm->elem_index_dirty &= ~BM_FACE;

		BM_ITER_MESH_INDEX (eve, &viter, bm, BM_VERTS_OF_MESH, i) {
			float *no = bmdm->vertexNos[i];
			BM_ITER_ELEM (efa, &fiter, eve, BM_FACES_OF_VERT) {
				add_v3_v3(no, bmdm->polyNos[BM_elem_index_get(efa)]);
			}

			/* following Mesh convention; we use vertex coordinate itself
			 * for normal in this case */
			if (normalize_v3(no) == 0.0f) {
				copy_v3_v3(no, vertexCos[i]);
				normalize_v3(no);
			}
		}
	}

	return (DerivedMesh *)bmdm;
}

/**
 * \brief Return the BMEditMesh for a given object
 *
 * \note this function assumes this is a mesh object,
 * don't add NULL data check here. caller must do that
 */
BMEditMesh *BMEdit_FromObject(Object *ob)
{
	BLI_assert(ob->type == OB_MESH);
	/* sanity check */
#ifndef NDEBUG
	if (((Mesh *)ob->data)->edit_btmesh) {
		BLI_assert(((Mesh *)ob->data)->edit_btmesh->ob == ob);
	}
#endif
	return ((Mesh *)ob->data)->edit_btmesh;
}
