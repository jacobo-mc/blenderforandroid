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

/** \file blender/editors/space_view3d/view3d_intern.h
 *  \ingroup spview3d
 */

#ifndef __VIEW3D_INTERN_H__
#define __VIEW3D_INTERN_H__

#include "ED_view3d.h"

/* internal exports only */

struct ARegion;
struct ARegionType;
struct BoundBox;
struct DerivedMesh;
struct Object;
struct SmokeDomainSettings;
struct ViewContext;
struct bAnimVizSettings;
struct bContext;
struct bMotionPath;
struct bPoseChannel;
struct bScreen;
struct wmNDOFMotionData;
struct wmOperatorType;
struct wmWindowManager;

/* drawing flags: */
enum {
	DRAW_PICKING     = (1 << 0),
	DRAW_CONSTCOLOR  = (1 << 1),
	DRAW_SCENESET    = (1 << 2)
};

/* draw_mesh_fancy/draw_mesh_textured draw_flags */
enum {
	DRAW_MODIFIERS_PREVIEW  = (1 << 0),
	DRAW_FACE_SELECT        = (1 << 1)
};

/* view3d_header.c */
void VIEW3D_OT_layers(struct wmOperatorType *ot);

/* view3d_ops.c */
void view3d_operatortypes(void);

/* view3d_edit.c */
void VIEW3D_OT_zoom(struct wmOperatorType *ot);
void VIEW3D_OT_dolly(struct wmOperatorType *ot);
void VIEW3D_OT_zoom_camera_1_to_1(struct wmOperatorType *ot);
void VIEW3D_OT_move(struct wmOperatorType *ot);
void VIEW3D_OT_rotate(struct wmOperatorType *ot);
void VIEW3D_OT_ndof_orbit(struct wmOperatorType *ot);
void VIEW3D_OT_ndof_pan(struct wmOperatorType *ot);
void VIEW3D_OT_ndof_all(struct wmOperatorType *ot);
void VIEW3D_OT_view_all(struct wmOperatorType *ot);
void VIEW3D_OT_viewnumpad(struct wmOperatorType *ot);
void VIEW3D_OT_view_selected(struct wmOperatorType *ot);
void VIEW3D_OT_view_lock_clear(struct wmOperatorType *ot);
void VIEW3D_OT_view_lock_to_active(struct wmOperatorType *ot);
void VIEW3D_OT_view_center_cursor(struct wmOperatorType *ot);
void VIEW3D_OT_view_center_camera(struct wmOperatorType *ot);
void VIEW3D_OT_view_pan(struct wmOperatorType *ot);
void VIEW3D_OT_view_persportho(struct wmOperatorType *ot);
void VIEW3D_OT_background_image_add(struct wmOperatorType *ot);
void VIEW3D_OT_background_image_remove(struct wmOperatorType *ot);
void VIEW3D_OT_view_orbit(struct wmOperatorType *ot);
void VIEW3D_OT_clip_border(struct wmOperatorType *ot);
void VIEW3D_OT_cursor3d(struct wmOperatorType *ot);
void VIEW3D_OT_manipulator(struct wmOperatorType *ot);
void VIEW3D_OT_enable_manipulator(struct wmOperatorType *ot);
void VIEW3D_OT_render_border(struct wmOperatorType *ot);
void VIEW3D_OT_clear_render_border(struct wmOperatorType *ot);
void VIEW3D_OT_zoom_border(struct wmOperatorType *ot);

void view3d_boxview_copy(ScrArea *sa, ARegion *ar);
void ndof_to_quat(struct wmNDOFMotionData *ndof, float q[4]);
float ndof_to_axis_angle(struct wmNDOFMotionData *ndof, float axis[3]);

/* view3d_fly.c */
void view3d_keymap(struct wmKeyConfig *keyconf);
void VIEW3D_OT_fly(struct wmOperatorType *ot);

/* drawanim.c */
void draw_motion_paths_init(View3D *v3d, struct ARegion *ar);
void draw_motion_path_instance(Scene *scene,
                               struct Object *ob, struct bPoseChannel *pchan,
                               struct bAnimVizSettings *avs, struct bMotionPath *mpath);
void draw_motion_paths_cleanup(View3D *v3d);



/* drawobject.c */
void draw_object(Scene *scene, struct ARegion *ar, View3D *v3d, Base *base, const short dflag);
int draw_glsl_material(Scene *scene, struct Object *ob, View3D *v3d, const short dt);
void draw_object_instance(Scene *scene, View3D *v3d, RegionView3D *rv3d, struct Object *ob, const short dt, int outline);
void draw_object_backbufsel(Scene *scene, View3D *v3d, RegionView3D *rv3d, struct Object *ob);
void drawaxes(float size, char drawtype);

void view3d_cached_text_draw_begin(void);
void view3d_cached_text_draw_add(const float co[3], const char *str, short xoffs, short flag, const unsigned char col[4]);
void view3d_cached_text_draw_end(View3D * v3d, ARegion * ar, int depth_write, float mat[][4]);

enum {
	V3D_CACHE_TEXT_ZBUF         = (1 << 0),
	V3D_CACHE_TEXT_WORLDSPACE   = (1 << 1),
	V3D_CACHE_TEXT_ASCII        = (1 << 2),
	V3D_CACHE_TEXT_GLOBALSPACE  = (1 << 3),
	V3D_CACHE_TEXT_LOCALCLIP    = (1 << 4)
};

/* drawarmature.c */
int draw_armature(Scene *scene, View3D *v3d, ARegion *ar, Base *base,
                  const short dt, const short dflag, const unsigned char ob_wire_col[4],
                  const short is_outline);

/* drawmesh.c */
void draw_mesh_textured(Scene *scene, View3D *v3d, RegionView3D *rv3d,
                        struct Object *ob, struct DerivedMesh *dm, int draw_flags);
void draw_mesh_paint(View3D *v3d, RegionView3D *rv3d,
                     struct Object *ob, struct DerivedMesh *dm, int draw_flags);

/* view3d_draw.c */
void view3d_main_area_draw(const struct bContext *C, struct ARegion *ar);
void draw_depth(Scene *scene, struct ARegion *ar, View3D *v3d, int (*func)(void *));
void draw_depth_gpencil(Scene *scene, ARegion *ar, View3D *v3d);
void ED_view3d_after_add(ListBase *lb, Base *base, const short dflag);

void view3d_update_depths_rect(struct ARegion *ar, struct ViewDepths *d, struct rcti *rect);
float view3d_depth_near(struct ViewDepths *d);

/* view3d_select.c */
void VIEW3D_OT_select(struct wmOperatorType *ot);
void VIEW3D_OT_select_circle(struct wmOperatorType *ot);
void VIEW3D_OT_select_border(struct wmOperatorType *ot);
void VIEW3D_OT_select_lasso(struct wmOperatorType *ot);
void VIEW3D_OT_select_menu(struct wmOperatorType *ot);

void VIEW3D_OT_smoothview(struct wmOperatorType *ot);
void VIEW3D_OT_camera_to_view(struct wmOperatorType *ot);
void VIEW3D_OT_camera_to_view_selected(struct wmOperatorType *ot);
void VIEW3D_OT_object_as_camera(struct wmOperatorType *ot);
void VIEW3D_OT_localview(struct wmOperatorType *ot);
void VIEW3D_OT_game_start(struct wmOperatorType *ot);


int ED_view3d_boundbox_clip(RegionView3D * rv3d, float obmat[][4], struct BoundBox *bb);

void view3d_smooth_view(struct bContext *C, struct View3D *v3d, struct ARegion *ar, struct Object *, struct Object *,
                        float *ofs, float *quat, float *dist, float *lens);

void setwinmatrixview3d(ARegion *ar, View3D *v3d, rctf *rect);
void setviewmatrixview3d(Scene *scene, View3D *v3d, RegionView3D *rv3d);

void fly_modal_keymap(struct wmKeyConfig *keyconf);
void viewrotate_modal_keymap(struct wmKeyConfig *keyconf);
void viewmove_modal_keymap(struct wmKeyConfig *keyconf);
void viewzoom_modal_keymap(struct wmKeyConfig *keyconf);
void viewdolly_modal_keymap(struct wmKeyConfig *keyconf);

/* view3d_buttons.c */
void VIEW3D_OT_properties(struct wmOperatorType *ot);
void view3d_buttons_register(struct ARegionType *art);

/* view3d_toolbar.c */
void VIEW3D_OT_toolshelf(struct wmOperatorType *ot);
void view3d_toolshelf_register(struct ARegionType *art);
void view3d_tool_props_register(struct ARegionType *art);

/* view3d_snap.c */
int ED_view3d_minmax_verts(struct Object *obedit, float min[3], float max[3]);

void VIEW3D_OT_snap_selected_to_grid(struct wmOperatorType *ot);
void VIEW3D_OT_snap_selected_to_cursor(struct wmOperatorType *ot);
void VIEW3D_OT_snap_cursor_to_grid(struct wmOperatorType *ot);
void VIEW3D_OT_snap_cursor_to_center(struct wmOperatorType *ot);
void VIEW3D_OT_snap_cursor_to_selected(struct wmOperatorType *ot);
void VIEW3D_OT_snap_cursor_to_active(struct wmOperatorType *ot);

/* space_view3d.c */
ARegion *view3d_has_buttons_region(ScrArea *sa);
ARegion *view3d_has_tools_region(ScrArea *sa);

extern const char *view3d_context_dir[]; /* doc access */

/* draw_volume.c */
void draw_smoke_volume(struct SmokeDomainSettings *sds, struct Object *ob,
                       struct GPUTexture *tex, float min[3], float max[3],
                       int res[3], float dx, float base_scale, float viewnormal[3],
                       struct GPUTexture *tex_shadow, struct GPUTexture *tex_flame);

//#define SMOKE_DEBUG_VELOCITY
//#define SMOKE_DEBUG_HEAT

#ifdef SMOKE_DEBUG_VELOCITY
void draw_smoke_velocity(struct SmokeDomainSettings *domain, struct Object *ob);
#endif
#ifdef SMOKE_DEBUG_HEAT
void draw_smoke_heat(struct SmokeDomainSettings *domain, struct Object *ob);
#endif

/* workaround for trivial but noticeable camera bug caused by imprecision
 * between view border calculation in 2D/3D space, workaround for bug [#28037].
 * without this deifne we get the old behavior which is to try and align them
 * both which _mostly_ works fine, but when the camera moves beyond ~1000 in
 * any direction it starts to fail */
#define VIEW3D_CAMERA_BORDER_HACK
#ifdef VIEW3D_CAMERA_BORDER_HACK
extern unsigned char view3d_camera_border_hack_col[3];
extern short view3d_camera_border_hack_test;
#endif

#endif /* __VIEW3D_INTERN_H__ */

