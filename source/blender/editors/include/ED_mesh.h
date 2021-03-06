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

/** \file ED_mesh.h
 *  \ingroup editors
 */

#ifndef __ED_MESH_H__
#define __ED_MESH_H__

#ifdef __cplusplus
extern "C" {
#endif

struct ID;
struct View3D;
struct ARegion;
struct bContext;
struct wmOperator;
struct wmWindowManager;
struct wmKeyConfig;
struct ReportList;
struct EditSelection;
struct ViewContext;
struct bDeformGroup;
struct MDeformWeight;
struct MDeformVert;
struct Scene;
struct Mesh;
struct MFace;
struct MEdge;
struct MVert;
struct MCol;
struct UvVertMap;
struct UvMapVert;
struct CustomData;
struct BMEditMesh;
struct BMEditSelection;
struct BMesh;
struct BMVert;
struct BMLoop;
struct MLoopCol;
struct BMEdge;
struct BMFace;
struct UvVertMap;
struct UvMapVert;
struct ToolSettings;
struct Material;
struct Object;
struct rcti;


/* editmesh_utils.c */
void           EDBM_verts_mirror_cache_begin(struct BMEditMesh *em, const short use_select); /* note, replaces EM_cache_x_mirror_vert in trunk */
void           EDBM_verts_mirror_apply(struct BMEditMesh *em, const int sel_from, const int sel_to);
struct BMVert *EDBM_verts_mirror_get(struct BMEditMesh *em, struct BMVert *v);
void           EDBM_verts_mirror_cache_clear(struct BMEditMesh *em, struct BMVert *v);
void           EDBM_verts_mirror_cache_end(struct BMEditMesh *em);

void EDBM_mesh_ensure_valid_dm_hack(struct Scene *scene, struct BMEditMesh *em);
void EDBM_mesh_normals_update(struct BMEditMesh *em);
void EDBM_mesh_clear(struct BMEditMesh *em);

void EDBM_selectmode_to_scene(struct bContext *C);
void EDBM_mesh_make(struct ToolSettings *ts, struct Scene *scene, struct Object *ob);
void EDBM_mesh_free(struct BMEditMesh *tm);
void EDBM_mesh_load(struct Object *ob);

void           EDBM_index_arrays_init(struct BMEditMesh *em, int forvert, int foredge, int forface);
void           EDBM_index_arrays_free(struct BMEditMesh *em);
struct BMVert *EDBM_vert_at_index(struct BMEditMesh *em, int index);
struct BMEdge *EDBM_edge_at_index(struct BMEditMesh *em, int index);
struct BMFace *EDBM_face_at_index(struct BMEditMesh *em, int index);

/* flushes based on the current select mode.  if in vertex select mode,
 * verts select/deselect edges and faces, if in edge select mode,
 * edges select/deselect faces and vertices, and in face select mode faces select/deselect
 * edges and vertices.*/
void EDBM_select_more(struct BMEditMesh *em);
void EDBM_select_less(struct BMEditMesh *em);

void EDBM_selectmode_flush_ex(struct BMEditMesh *em, const short selectmode);
void EDBM_selectmode_flush(struct BMEditMesh *em);

void EDBM_deselect_flush(struct BMEditMesh *em);
void EDBM_select_flush(struct BMEditMesh *em);

void undo_push_mesh(struct bContext *C, const char *name);

int  EDBM_vert_color_check(struct BMEditMesh *em);

void EDBM_mesh_hide(struct BMEditMesh *em, int swap);
void EDBM_mesh_reveal(struct BMEditMesh *em);

void EDBM_update_generic(struct bContext *C, struct BMEditMesh *em, const short do_tessface);

struct UvElementMap *EDBM_uv_element_map_create(struct BMEditMesh *em, int selected, int doIslands);
void                 EDBM_uv_element_map_free(struct UvElementMap *vmap);
struct UvElement *ED_uv_element_get(struct UvElementMap *map, struct BMFace *efa, struct BMLoop *l);

int              EDBM_mtexpoly_check(struct BMEditMesh *em);
struct MTexPoly *EDBM_mtexpoly_active_get(struct BMEditMesh *em, struct BMFace **r_act_efa, int sloppy, int selected);

void              EDBM_uv_vert_map_free(struct UvVertMap *vmap);
struct UvMapVert *EDBM_uv_vert_map_at_index(struct UvVertMap *vmap, unsigned int v);
struct UvVertMap *EDBM_uv_vert_map_create(struct BMEditMesh *em, int selected, int do_face_idx_array, const float limit[2]);

void EDBM_flag_enable_all(struct BMEditMesh *em, const char hflag);
void EDBM_flag_disable_all(struct BMEditMesh *em, const char hflag);


/* editmesh_select.c */
void EDBM_select_mirrored(struct Object *obedit, struct BMEditMesh *em, int extend);
void EDBM_automerge(struct Scene *scene, struct Object *ob, int update);

int  EDBM_backbuf_border_init(struct ViewContext *vc, short xmin, short ymin, short xmax, short ymax);
int  EDBM_backbuf_check(unsigned int index);
void EDBM_backbuf_free(void);

int  EDBM_backbuf_border_mask_init(struct ViewContext *vc, const int mcords[][2], short tot,
                                   short xmin, short ymin, short xmax, short ymax);
int  EDBM_backbuf_circle_init(struct ViewContext *vc, short xs, short ys, short rads);

struct BMVert *EDBM_vert_find_nearest(struct ViewContext *vc, float *r_dist, const short sel, const short strict);
struct BMEdge *EDBM_edge_find_nearest(struct ViewContext *vc, float *r_dist);
struct BMFace *EDBM_face_find_nearest(struct ViewContext *vc, float *r_dist);

int  EDBM_select_pick(struct bContext *C, const int mval[2], short extend, short deselect, short toggle);

void EDBM_selectmode_set(struct BMEditMesh *em);
void EDBM_selectmode_convert(struct BMEditMesh *em, short selectmode_old, short selectmode_new);

/* user access this */
int EDBM_selectmode_toggle(struct bContext *C, const short selectmode_new,
                           const int action, const int use_extend, const int use_expand);


void EDBM_deselect_by_material(struct BMEditMesh *em, const short index, const short select);

void EDBM_select_toggle_all(struct BMEditMesh *em);

void EDBM_select_swap(struct BMEditMesh *em); /* exported for UV */
int  EDBM_select_interior_faces(struct BMEditMesh *em);
void em_setup_viewcontext(struct bContext *C, struct ViewContext *vc);  /* rename? */

extern unsigned int bm_vertoffs, bm_solidoffs, bm_wireoffs;


/* mesh_ops.c */
void        ED_operatortypes_mesh(void);
void        ED_operatormacros_mesh(void);
void        ED_keymap_mesh(struct wmKeyConfig *keyconf);
void        ED_keymap_mesh(struct wmKeyConfig *keyconf);


/* spacetypes.c */
void        ED_spacetypes_init(void);


/* editmesh_tools.c (could be moved) */
void EMBM_project_snap_verts(struct bContext *C, struct ARegion *ar, struct BMEditMesh *em);


/* editface.c */
void paintface_flush_flags(struct Object *ob);
int  paintface_mouse_select(struct bContext *C, struct Object *ob, const int mval[2], int extend, int deselect, int toggle);
int  do_paintface_box_select(struct ViewContext *vc, struct rcti *rect, int select, int extend);
void paintface_deselect_all_visible(struct Object *ob, int action, short flush_flags);
void paintface_select_linked(struct bContext *C, struct Object *ob, int mval[2], int mode);
int  paintface_minmax(struct Object *ob, float r_min[3], float r_max[3]);

void paintface_hide(struct Object *ob, const int unselected);
void paintface_reveal(struct Object *ob);

void paintvert_deselect_all_visible(struct Object *ob, int action, short flush_flags);
void paintvert_flush_flags(struct Object *ob);

/* mirrtopo */
typedef struct MirrTopoStore_t {
	intptr_t *index_lookup;
	int prev_vert_tot;
	int prev_edge_tot;
	int prev_ob_mode;
} MirrTopoStore_t;

int  ED_mesh_mirrtopo_recalc_check(struct Mesh *me, const int ob_mode, MirrTopoStore_t *mesh_topo_store);
void ED_mesh_mirrtopo_init(struct Mesh *me, const int ob_mode, MirrTopoStore_t *mesh_topo_store,
                           const short skip_em_vert_array_init);
void ED_mesh_mirrtopo_free(MirrTopoStore_t *mesh_topo_store);


/* object_vgroup.c */
#define WEIGHT_REPLACE 1
#define WEIGHT_ADD 2
#define WEIGHT_SUBTRACT 3

struct bDeformGroup *ED_vgroup_add(struct Object *ob);
struct bDeformGroup *ED_vgroup_add_name(struct Object *ob, const char *name);
void                 ED_vgroup_delete(struct Object *ob, struct bDeformGroup *defgroup);
void                 ED_vgroup_clear(struct Object *ob);
void                 ED_vgroup_select_by_name(struct Object *ob, const char *name);
int                  ED_vgroup_data_create(struct ID *id);
int                  ED_vgroup_give_array(struct ID *id, struct MDeformVert **dvert_arr, int *dvert_tot);
int                  ED_vgroup_copy_array(struct Object *ob, struct Object *ob_from);
void                 ED_vgroup_mirror(struct Object *ob, const short mirror_weights, const short flip_vgroups, const short all_vgroups);

int                  ED_vgroup_object_is_edit_mode(struct Object *ob);

void                 ED_vgroup_vert_add(struct Object *ob, struct bDeformGroup *dg, int vertnum,  float weight, int assignmode);
void                 ED_vgroup_vert_remove(struct Object *ob, struct bDeformGroup *dg, int vertnum);
float                ED_vgroup_vert_weight(struct Object *ob, struct bDeformGroup *dg, int vertnum);


/* mesh_data.c */
// void ED_mesh_geometry_add(struct Mesh *mesh, struct ReportList *reports, int verts, int edges, int faces);
void ED_mesh_polys_add(struct Mesh *mesh, struct ReportList *reports, int count);
void ED_mesh_tessfaces_add(struct Mesh *mesh, struct ReportList *reports, int count);
void ED_mesh_edges_add(struct Mesh *mesh, struct ReportList *reports, int count);
void ED_mesh_loops_add(struct Mesh *mesh, struct ReportList *reports, int count);
void ED_mesh_vertices_add(struct Mesh *mesh, struct ReportList *reports, int count);

void ED_mesh_faces_remove(struct Mesh *mesh, struct ReportList *reports, int count);
void ED_mesh_edges_remove(struct Mesh *mesh, struct ReportList *reports, int count);
void ED_mesh_vertices_remove(struct Mesh *mesh, struct ReportList *reports, int count);

void ED_mesh_transform(struct Mesh *me, float *mat);
void ED_mesh_calc_normals(struct Mesh *me);
void ED_mesh_calc_tessface(struct Mesh *mesh);
void ED_mesh_material_link(struct Mesh *me, struct Material *ma);
void ED_mesh_update(struct Mesh *mesh, struct bContext *C, int calc_edges, int calc_tessface);

int ED_mesh_uv_texture_add(struct bContext *C, struct Mesh *me, const char *name, int active_set);
int ED_mesh_uv_texture_remove(struct bContext *C, struct Object *ob, struct Mesh *me);
int ED_mesh_uv_loop_reset(struct bContext *C, struct Mesh *me);
int ED_mesh_uv_loop_reset_ex(struct Mesh *me, const int layernum);
int ED_mesh_color_add(struct bContext *C, struct Scene *scene, struct Object *ob, struct Mesh *me, const char *name, int active_set);
int ED_mesh_color_remove(struct bContext *C, struct Object *ob, struct Mesh *me);
int ED_mesh_color_remove_named(struct bContext *C, struct Object *ob, struct Mesh *me, const char *name);

/* mesh backup */
typedef struct BMBackup {
	struct BMesh *bmcopy;
} BMBackup;

/* save a copy of the bmesh for restoring later */
struct BMBackup EDBM_redo_state_store(struct BMEditMesh *em);
/* restore a bmesh from backup */
void EDBM_redo_state_restore(struct BMBackup, struct BMEditMesh *em, int recalctess);
/* delete the backup, optionally flushing it to an editmesh */
void EDBM_redo_state_free(struct BMBackup *, struct BMEditMesh *em, int recalctess);


/* meshtools.c */
int         join_mesh_exec(struct bContext *C, struct wmOperator *op);
int         join_mesh_shapes_exec(struct bContext *C, struct wmOperator *op);

intptr_t    mesh_octree_table(struct Object *ob, struct BMEditMesh *em, const float co[3], char mode);
int         mesh_mirrtopo_table(struct Object *ob, char mode);

/* retrieves mirrored cache vert, or NULL if there isn't one.
 * note: calling this without ensuring the mirror cache state
 * is bad.*/
int            mesh_get_x_mirror_vert(struct Object *ob, int index);
struct BMVert *editbmesh_get_x_mirror_vert(struct Object *ob, struct BMEditMesh *em, struct BMVert *eve, const float co[3], int index);
int           *mesh_get_x_mirror_faces(struct Object *ob, struct BMEditMesh *em);

int ED_mesh_pick_vert(struct bContext *C, struct Mesh *me, const int mval[2], unsigned int *index, int size);
int ED_mesh_pick_face(struct bContext *C, struct Mesh *me, const int mval[2], unsigned int *index, int size);
int ED_mesh_pick_face_vert(struct bContext *C, struct Mesh *me, struct Object *ob, const int mval[2], unsigned int *index, int size);

#define ED_MESH_PICK_DEFAULT_VERT_SIZE 50
#define ED_MESH_PICK_DEFAULT_FACE_SIZE 3

#include "../mesh/editmesh_bvh.h"

#ifdef __cplusplus
}
#endif

#endif /* __ED_MESH_H__ */
