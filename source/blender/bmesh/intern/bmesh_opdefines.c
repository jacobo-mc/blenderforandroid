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
 * Contributor(s): Joseph Eagar, Geoffrey Bantle, Campbell Barton
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/bmesh/intern/bmesh_opdefines.c
 *  \ingroup bmesh
 *
 * BMesh operator definitions.
 *
 * This file defines (and documents) all bmesh operators (bmops).
 *
 * Do not rename any operator or slot names! otherwise you must go
 * through the code and find all references to them!
 *
 * A word on slot names:
 *
 * For geometry input slots, the following are valid names:
 * - verts
 * - edges
 * - faces
 * - edgefacein
 * - vertfacein
 * - vertedgein
 * - vertfacein
 * - geom
 *
 * The basic rules are, for single-type geometry slots, use the plural of the
 * type name (e.g. edges).  for double-type slots, use the two type names plus
 * "in" (e.g. edgefacein).  for three-type slots, use geom.
 *
 * for output slots, for single-type geometry slots, use the type name plus "out",
 * (e.g. verts.out), for double-type slots, use the two type names plus "out",
 * (e.g. vertfaces.out), for three-type slots, use geom.  note that you can also
 * use more esohteric names (e.g. geom_skirt.out) so long as the comment next to the
 * slot definition tells you what types of elements are in it.
 *
 */

#include "BLI_utildefines.h"

#include "bmesh.h"
#include "intern/bmesh_private.h"

/* ok, I'm going to write a little docgen script. so all
 * bmop comments must conform to the following template/rules:
 *
 * template (py quotes used because nested comments don't work
 * on all C compilers):
 *
 * """
 * Region Extend.
 *
 * paragraph1, Extends bleh bleh bleh.
 * Bleh Bleh bleh.
 *
 * Another paragraph.
 *
 * Another paragraph.
 * """
 *
 * so the first line is the "title" of the bmop.
 * subsequent line blocks separated by blank lines
 * are paragraphs.  individual descriptions of slots
 * would be extracted from comments
 * next to them, e.g.
 *
 * {BMO_OP_SLOT_ELEMENT_BUF, "geom.out"}, //output slot, boundary region
 *
 * the doc generator would automatically detect the presence of "output slot"
 * and flag the slot as an output.  the same happens for "input slot".  also
 * note that "edges", "faces", "verts", "loops", and "geometry" are valid
 * substitutions for "slot".
 *
 * note that slots default to being input slots.
 */

/*
 * Vertex Smooth
 *
 * Smooths vertices by using a basic vertex averaging scheme.
 */
static BMOpDefine bmo_smooth_vert_def = {
	"smooth_vert",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts"},    /* input vertices */
	 {BMO_OP_SLOT_BOOL, "mirror_clip_x"},   /* set vertices close to the x axis before the operation to 0 */
	 {BMO_OP_SLOT_BOOL, "mirror_clip_y"},   /* set vertices close to the y axis before the operation to 0 */
	 {BMO_OP_SLOT_BOOL, "mirror_clip_z"},   /* set vertices close to the z axis before the operation to 0 */
	 {BMO_OP_SLOT_FLT,  "clip_dist"},       /* clipping threshod for the above three slots */
	 {BMO_OP_SLOT_BOOL, "use_axis_x"},      /* smooth vertices along X axis */
	 {BMO_OP_SLOT_BOOL, "use_axis_y"},      /* smooth vertices along Y axis */
	 {BMO_OP_SLOT_BOOL, "use_axis_z"},      /* smooth vertices along Z axis */
	{0},
	},
	{{0}},  /* no output */
	bmo_smooth_vert_exec,
	0
};

/*
 * Vertext Smooth Laplacian 
 * Smooths vertices by using Laplacian smoothing propose by.
 * Desbrun, et al. Implicit Fairing of Irregular Meshes using Diffusion and Curvature Flow
 */
static BMOpDefine bmo_smooth_laplacian_vert_def = {
	"smooth_laplacian_vert",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts"},    /* input vertices */
	 {BMO_OP_SLOT_FLT, "lambda"},           /* lambda param */
	 {BMO_OP_SLOT_FLT, "lambda_border"},    /* lambda param in border */
	 {BMO_OP_SLOT_BOOL, "use_x"},           /* Smooth object along X axis */
	 {BMO_OP_SLOT_BOOL, "use_y"},           /* Smooth object along Y axis */
	 {BMO_OP_SLOT_BOOL, "use_z"},           /* Smooth object along Z axis */
	 {BMO_OP_SLOT_BOOL, "preserve_volume"}, /* Apply volume preservation after smooth */
	{0},
	},
	{{0}},  /* no output */
	bmo_smooth_laplacian_vert_exec,
	0
};

/*
 * Right-Hand Faces
 *
 * Computes an "outside" normal for the specified input faces.
 */

static BMOpDefine bmo_recalc_face_normals_def = {
	"recalc_face_normals",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces"},
	 {BMO_OP_SLOT_BOOL, "use_flip"},        /* internal flag, used by bmesh_rationalize_normals */
	 {0},
	},
	{{0}},  /* no output */
	bmo_recalc_face_normals_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES,
};

/*
 * Region Extend
 *
 * used to implement the select more/less tools.
 * this puts some geometry surrounding regions of
 * geometry in geom into geom.out.
 *
 * if usefaces is 0 then geom.out spits out verts and edges,
 * otherwise it spits out faces.
 */
static BMOpDefine bmo_region_extend_def = {
	"region_extend",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom"},     /* input geometry */
	 {BMO_OP_SLOT_BOOL, "use_constrict"},   /* find boundary inside the regions, not outside. */
	 {BMO_OP_SLOT_BOOL, "use_faces"},       /* extend from faces instead of edges */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom.out"}, /* output slot, computed boundary geometry. */
	 {0},
	},
	bmo_region_extend_exec,
	0
};

/*
 * Edge Rotate
 *
 * Rotates edges topologically.  Also known as "spin edge" to some people.
 * Simple example: [/] becomes [|] then [\].
 */
static BMOpDefine bmo_rotate_edges_def = {
	"rotate_edges",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges"},    /* input edges */
	 {BMO_OP_SLOT_BOOL, "use_ccw"},         /* rotate edge counter-clockwise if true, othewise clockwise */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges.out"}, /* newly spun edges */
	 {0},
	},
	bmo_rotate_edges_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES
};

/*
 * Reverse Faces
 *
 * Reverses the winding (vertex order) of faces.  This has the effect of
 * flipping the normal.
 */
static BMOpDefine bmo_reverse_faces_def = {
	"reverse_faces",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces"},    /* input faces */
	 {0},
	},
	{{0}},  /* no output */
	bmo_reverse_faces_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES,
};

/*
 * Edge Bisect
 *
 * Splits input edges (but doesn't do anything else).
 * This creates a 2-valence vert.
 */
static BMOpDefine bmo_bisect_edges_def = {
	"bisect_edges",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges"}, /* input edges */
	 {BMO_OP_SLOT_INT, "cuts"}, /* number of cuts */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom_split.out"}, /* newly created vertices and edges */
	 {0},
	},
	bmo_bisect_edges_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES
};

/*
 * Mirror
 *
 * Mirrors geometry along an axis.  The resulting geometry is welded on using
 * merge_dist.  Pairs of original/mirrored vertices are welded using the merge_dist
 * parameter (which defines the minimum distance for welding to happen).
 */

static BMOpDefine bmo_mirror_def = {
	"mirror",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom"},     /* input geometry */
	 {BMO_OP_SLOT_MAT,         "mat"},      /* matrix defining the mirror transformation */
	 {BMO_OP_SLOT_FLT, "merge_dist"},       /* maximum distance for merging.  does no merging if 0. */
	 {BMO_OP_SLOT_INT,         "axis"},     /* the axis to use, 0, 1, or 2 for x, y, z */
	 {BMO_OP_SLOT_BOOL,        "mirror_u"}, /* mirror UVs across the u axis */
	 {BMO_OP_SLOT_BOOL,        "mirror_v"}, /* mirror UVs across the v axis */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom.out"}, /* output geometry, mirrored */
	 {0},
	},
	bmo_mirror_exec,
	0,
};

/*
 * Find Doubles
 *
 * Takes input verts and find vertices they should weld to.  Outputs a
 * mapping slot suitable for use with the weld verts bmop.
 *
 * If keep_verts is used, vertices outside that set can only be merged
 * with vertices in that set.
 */
static BMOpDefine bmo_find_doubles_def = {
	"find_doubles",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts"}, /* input vertices */
	 {BMO_OP_SLOT_ELEMENT_BUF, "keep_verts"}, /* list of verts to keep */
	 {BMO_OP_SLOT_FLT,         "dist"}, /* minimum distance */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_MAPPING, "targetmap.out"},
	 {0},
	},
	bmo_find_doubles_exec,
	0,
};

/*
 * Remove Doubles
 *
 * Finds groups of vertices closer then dist and merges them together,
 * using the weld verts bmop.
 */
static BMOpDefine bmo_remove_doubles_def = {
	"remove_doubles",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts"}, /* input verts */
	 {BMO_OP_SLOT_FLT,         "dist"}, /* minimum distance */
	 {0},
	},
	{{0}},  /* no output */
	bmo_remove_doubles_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES,
};

/*
 * Auto Merge
 *
 * Finds groups of vertices closer then dist and merges them together,
 * using the weld verts bmop.  The merges must go from a vert not in
 * verts to one in verts.
 */
static BMOpDefine bmo_automerge_def = {
	"automerge",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts"}, /* input verts */
	 {BMO_OP_SLOT_FLT,         "dist"}, /* minimum distance */
	 {0},
	},
	{{0}},  /* no output */
	bmo_automerge_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES,
};

/*
 * Collapse Connected
 *
 * Collapses connected vertices
 */
static BMOpDefine bmo_collapse_def = {
	"collapse",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges"}, /* input edge */
	 {0},
	},
	{{0}},  /* no output */
	bmo_collapse_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES,
};


/*
 * Facedata point Merge
 *
 * Merge uv/vcols at a specific vertex.
 */
static BMOpDefine bmo_pointmerge_facedata_def = {
	"pointmerge_facedata",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts"},    /* input vertices */
	 {BMO_OP_SLOT_ELEMENT_BUF, "snapv"},    /* snap vertex */
	 {0},
	},
	{{0}},  /* no output */
	bmo_pointmerge_facedata_exec,
	0,
};

/*
 * Average Vertices Facevert Data
 *
 * Merge uv/vcols associated with the input vertices at
 * the bounding box center. (I know, it's not averaging but
 * the vert_snap_to_bb_center is just too long).
 */
static BMOpDefine bmo_average_vert_facedata_def = {
	"average_vert_facedata",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts"}, /* input vertice */
	 {0},
	},
	{{0}},  /* no output */
	bmo_average_vert_facedata_exec,
	0,
};

/*
 * Point Merge
 *
 * Merge verts together at a point.
 */
static BMOpDefine bmo_pointmerge_def = {
	"pointmerge",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts"}, /* input vertice */
	 {BMO_OP_SLOT_VEC,         "merge_co"},
	 {0},
	},
	{{0}},  /* no output */
	bmo_pointmerge_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES,
};

/*
 * Collapse Connected UVs
 *
 * Collapses connected UV vertices.
 */
static BMOpDefine bmo_collapse_uvs_def = {
	"collapse_uvs",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges"}, /* input edge */
	 {0},
	},
	{{0}},  /* no output */
	bmo_collapse_uvs_exec,
	0,
};

/*
 * Weld Verts
 *
 * Welds verts together (kindof like remove doubles, merge, etc, all of which
 * use or will use this bmop).  You pass in mappings from vertices to the vertices
 * they weld with.
 */
static BMOpDefine bmo_weld_verts_def = {
	"weld_verts",
	/* slots_in */
	{{BMO_OP_SLOT_MAPPING, "targetmap"}, /* maps welded vertices to verts they should weld to */
	 {0},
	},
	{{0}},  /* no output */
	bmo_weld_verts_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES,
};

/*
 * Make Vertex
 *
 * Creates a single vertex; this bmop was necessary
 * for click-create-vertex.
 */
static BMOpDefine bmo_create_vert_def = {
	"create_vert",
	/* slots_in */
	{{BMO_OP_SLOT_VEC, "co"},  /* the coordinate of the new vert */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "vert.out"},  /* the new vert */
	 {0},
	},
	bmo_create_vert_exec,
	0,
};

/*
 * Join Triangles
 *
 * Tries to intelligently join triangles according
 * to various settings and stuff.
 */
static BMOpDefine bmo_join_triangles_def = {
	"join_triangles",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces"},    /* input geometry. */
	 {BMO_OP_SLOT_BOOL, "cmp_sharp"},
	 {BMO_OP_SLOT_BOOL, "cmp_uvs"},
	 {BMO_OP_SLOT_BOOL, "cmp_vcols"},
	 {BMO_OP_SLOT_BOOL, "cmp_materials"},
	 {BMO_OP_SLOT_FLT, "limit"},
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces.out"},  /* joined faces */
	 {0},
	},
	bmo_join_triangles_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES,
};

/*
 * Contextual Create
 *
 * This is basically fkey, it creates
 * new faces from vertices, makes stuff from edge nets,
 * makes wire edges, etc.  It also dissolves
 * faces.
 *
 * Three verts become a triangle, four become a quad.  Two
 * become a wire edge.
 */
static BMOpDefine bmo_contextual_create_def = {
	"contextual_create",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom"},     /* input geometry. */
	 {BMO_OP_SLOT_INT,         "mat_nr"},   /* material to use */
	 {BMO_OP_SLOT_BOOL,        "use_smooth"}, /* smooth to use */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces.out"}, /* newly-made face(s) */
	/* note, this is for stand-alone edges only, not edges which are apart of newly created faces */
	 {BMO_OP_SLOT_ELEMENT_BUF, "edges.out"}, /* newly-made edge(s) */
	 {0},
	},
	bmo_contextual_create_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES,
};

/*
 * Bridge edge loops with faces
 */
static BMOpDefine bmo_bridge_loops_def = {
	"bridge_loops",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges"}, /* input edge */
	 {BMO_OP_SLOT_BOOL,        "use_merge"},
	 {BMO_OP_SLOT_FLT,         "merge_factor"},
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces.out"}, /* new faces */
	 {0},
	},
	bmo_bridge_loops_exec,
	0,
};

static BMOpDefine bmo_edgenet_fill_def = {
	"edgenet_fill",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges"}, /* input edge */
	 {BMO_OP_SLOT_MAPPING,     "restrict"}, /* restricts edges to groups.  maps edges to integer */
	 {BMO_OP_SLOT_BOOL,        "use_restrict"},
	 {BMO_OP_SLOT_BOOL,        "use_fill_check"},
	 {BMO_OP_SLOT_ELEMENT_BUF, "exclude_faces"}, /* list of faces to ignore for manifold check */
	 {BMO_OP_SLOT_INT,         "mat_nr"},      /* material to use */
	 {BMO_OP_SLOT_BOOL,        "use_smooth"},  /* material to use */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_MAPPING,     "face_groupmap.out"}, /* maps new faces to the group numbers they came from */
	 {BMO_OP_SLOT_ELEMENT_BUF, "faces.out"},     /* new face */
	 {0},
	},
	bmo_edgenet_fill_exec,
	0,
};

/*
 * Edgenet Prepare
 *
 * Identifies several useful edge loop cases and modifies them so
 * they'll become a face when edgenet_fill is called.  The cases covered are:
 *
 * - One single loop; an edge is added to connect the ends
 * - Two loops; two edges are added to connect the endpoints (based on the
 *   shortest distance between each endpont).
 */
static BMOpDefine bmo_edgenet_prepare_def = {
	"edgenet_prepare",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges"},    /* input edges */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges.out"},  /* new edges */
	 {0},
	},
	bmo_edgenet_prepare,
	0,
};

/*
 * Rotate
 *
 * Rotate vertices around a center, using a 3x3 rotation
 * matrix.  Equivalent of the old rotateflag function.
 */
static BMOpDefine bmo_rotate_def = {
	"rotate",
	/* slots_in */
	{{BMO_OP_SLOT_VEC, "cent"},  /* center of rotation */
	 {BMO_OP_SLOT_MAT, "mat"},   /* matrix defining rotation */
	 {BMO_OP_SLOT_ELEMENT_BUF, "verts"},  /* input vertices */
	 {0},
	},
	{{0}},  /* no output */
	bmo_rotate_exec,
	0,
};

/*
 * Translate
 *
 * Translate vertices by an offset.  Equivalent of the
 * old translateflag function.
 */
static BMOpDefine bmo_translate_def = {
	"translate",
	/* slots_in */
	{{BMO_OP_SLOT_VEC, "vec"},  /* translation offset */
	 {BMO_OP_SLOT_ELEMENT_BUF, "verts"},  /* input vertices */
	 {0},
	},
	{{0}},  /* no output */
	bmo_translate_exec,
	0,
};

/*
 * Scale
 *
 * Scales vertices by an offset.
 */
static BMOpDefine bmo_scale_def = {
	"scale",
	/* slots_in */
	{{BMO_OP_SLOT_VEC, "vec"},  /* scale factor */
	 {BMO_OP_SLOT_ELEMENT_BUF, "verts"},  /* input vertices */
	 {0},
	},
	{{0}},  /* no output */
	bmo_scale_exec,
	0,
};


/*
 * Transform
 *
 * Transforms a set of vertices by a matrix.  Multiplies
 * the vertex coordinates with the matrix.
 */
static BMOpDefine bmo_transform_def = {
	"transform",
	/* slots_in */
	{{BMO_OP_SLOT_MAT, "mat"},  /* transform matrix */
	 {BMO_OP_SLOT_ELEMENT_BUF, "verts"},  /* input vertices */
	 {0},
	},
	{{0}},  /* no output */
	bmo_transform_exec,
	0,
};

/*
 * Object Load BMesh
 *
 * Loads a bmesh into an object/mesh.  This is a "private"
 * bmop.
 */
static BMOpDefine bmo_object_load_bmesh_def = {
	"object_load_bmesh",
	/* slots_in */
	{{BMO_OP_SLOT_PTR, "scene"},
	 {BMO_OP_SLOT_PTR, "object"},
	 {0},
	},
	{{0}},  /* no output */
	bmo_object_load_bmesh_exec,
	0,
};


/*
 * BMesh to Mesh
 *
 * Converts a bmesh to a Mesh.  This is reserved for exiting editmode.
 */
static BMOpDefine bmo_bmesh_to_mesh_def = {
	"bmesh_to_mesh",
	/* slots_in */
	{{BMO_OP_SLOT_PTR, "mesh"},    /* pointer to a mesh structure to fill in */
	 {BMO_OP_SLOT_PTR, "object"},  /* pointer to an object structure */
	 {BMO_OP_SLOT_BOOL, "skip_tessface"},  /* don't calculate mfaces */
	 {0},
	},
	{{0}},  /* no output */
	bmo_bmesh_to_mesh_exec,
	0,
};

/*
 * Mesh to BMesh
 *
 * Load the contents of a mesh into the bmesh.  this bmop is private, it's
 * reserved exclusively for entering editmode.
 */
static BMOpDefine bmo_mesh_to_bmesh_def = {
	"mesh_to_bmesh",
	/* slots_in */
	{{BMO_OP_SLOT_PTR, "mesh"},    /* pointer to a Mesh structure */
	 {BMO_OP_SLOT_PTR, "object"},  /* pointer to an Object structure */
	 {BMO_OP_SLOT_BOOL, "use_shapekey"},  /* load active shapekey coordinates into verts */
	 {0},
	},
	{{0}},  /* no output */
	bmo_mesh_to_bmesh_exec,
	0
};

/*
 * Individual Face Extrude
 *
 * Extrudes faces individually.
 */
static BMOpDefine bmo_extrude_discrete_faces_def = {
	"extrude_discrete_faces",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces"},     /* input faces */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces.out"},   /* output faces */
	 {BMO_OP_SLOT_ELEMENT_BUF, "geom_skirt.out"},  /* output skirt geometry, faces and edges */
	 {0},
	},
	bmo_extrude_discrete_faces_exec,
	0
};

/*
 * Extrude Only Edges
 *
 * Extrudes Edges into faces, note that this is very simple, there's no fancy
 * winged extrusion.
 */
static BMOpDefine bmo_extrude_edge_only_def = {
	"extrude_edge_only",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges"},    /* input vertices */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom.out"},  /* output geometry */
	 {0},
	},
	bmo_extrude_edge_only_exec,
	0
};

/*
 * Individual Vertex Extrude
 *
 * Extrudes wire edges from vertices.
 */
static BMOpDefine bmo_extrude_vert_indiv_def = {
	"extrude_vert_indiv",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts"},    /* input vertices */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges.out"},  /* output wire edges */
	 {BMO_OP_SLOT_ELEMENT_BUF, "verts.out"},  /* output vertices */
	 {0},
	},
	bmo_extrude_vert_indiv_exec,
	0
};

static BMOpDefine bmo_connect_verts_def = {
	"connect_verts",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts"},
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges.out"},
	 {0},
	},
	bmo_connect_verts_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES
};

static BMOpDefine bmo_extrude_face_region_def = {
	"extrude_face_region",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom"},     /* edges and faces */
	 {BMO_OP_SLOT_MAPPING, "edges_exclude"},
	 {BMO_OP_SLOT_BOOL, "use_keep_orig"},   /* keep original geometry */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom.out"},
	 {0},
	},
	bmo_extrude_face_region_exec,
	0
};

static BMOpDefine bmo_dissolve_verts_def = {
	"dissolve_verts",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts"},
	 {0},
	},
	{{0}},  /* no output */
	bmo_dissolve_verts_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES
};

static BMOpDefine bmo_dissolve_edges_def = {
	"dissolve_edges",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges"},
	 {BMO_OP_SLOT_BOOL, "use_verts"},  /* dissolve verts left between only 2 edges. */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "region.out"},
	 {0},
	},
	bmo_dissolve_edges_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES
};

static BMOpDefine bmo_dissolve_edge_loop_def = {
	"dissolve_edge_loop",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges"},
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "region.out"},
	 {0},
	},
	bmo_dissolve_edgeloop_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES
};

static BMOpDefine bmo_dissolve_faces_def = {
	"dissolve_faces",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces"},
	 {BMO_OP_SLOT_BOOL, "use_verts"},  /* dissolve verts left between only 2 edges. */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "region.out"},
	 {0},
	},
	bmo_dissolve_faces_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES
};

static BMOpDefine bmo_dissolve_limit_def = {
	"dissolve_limit",
	/* slots_in */
	{{BMO_OP_SLOT_FLT, "angle_limit"}, /* total rotation angle (degrees) */
	 {BMO_OP_SLOT_BOOL, "use_dissolve_boundaries"},
	 {BMO_OP_SLOT_ELEMENT_BUF, "verts"},
	 {BMO_OP_SLOT_ELEMENT_BUF, "edges"},
	 {0},
	},
	{{0}},  /* no output */
	bmo_dissolve_limit_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES
};

static BMOpDefine bmo_triangulate_def = {
	"triangulate",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces"},
	 {BMO_OP_SLOT_BOOL, "use_beauty"},
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges.out"},
	 {BMO_OP_SLOT_ELEMENT_BUF, "faces.out"},
	 {BMO_OP_SLOT_MAPPING, "facemap.out"},
	 {0},
	},
	bmo_triangulate_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES
};

static BMOpDefine bmo_unsubdivide_def = {
	"unsubdivide",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts"}, /* input vertices */
	 {BMO_OP_SLOT_INT, "iterations"},
	 {0},
	},
	{{0}},  /* no output */
	bmo_unsubdivide_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES
};

static BMOpDefine bmo_subdivide_edges_def = {
	"subdivide_edges",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges"},
	 {BMO_OP_SLOT_FLT, "smooth"},
	 {BMO_OP_SLOT_FLT, "fractal"},
	 {BMO_OP_SLOT_FLT, "along_normal"},
	 {BMO_OP_SLOT_INT, "cuts"},
	 {BMO_OP_SLOT_INT, "seed"},
	 {BMO_OP_SLOT_MAPPING, "custompatterns"},
	 {BMO_OP_SLOT_MAPPING, "edgepercents"},

	 {BMO_OP_SLOT_INT,  "quad_corner_type"}, /* quad corner type, see bmesh_operators.h */
	 {BMO_OP_SLOT_BOOL, "use_gridfill"},   /* fill in fully-selected faces with a grid */
	 {BMO_OP_SLOT_BOOL, "use_singleedge"}, /* tessellate the case of one edge selected in a quad or triangle */
	 {BMO_OP_SLOT_BOOL, "use_sphere"},     /* for making new primitives only */
	 {0},
	},
	/* slots_out */
	{/* these next three can have multiple types of elements in them */
	 {BMO_OP_SLOT_ELEMENT_BUF, "geom_inner.out"},
	 {BMO_OP_SLOT_ELEMENT_BUF, "geom_split.out"},
	 {BMO_OP_SLOT_ELEMENT_BUF, "geom.out"}, /* contains all output geometr */
	 {0},
	},
	bmo_subdivide_edges_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES
};

static BMOpDefine bmo_delete_def = {
	"delete",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom"},
	 {BMO_OP_SLOT_INT, "context"},
	 {0},
	},
	{{0}},  /* no output */
	bmo_delete_exec,
	0
};

static BMOpDefine bmo_duplicate_def = {
	"duplicate",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom"},
	 {BMO_OP_SLOT_PTR, "dest"}, /* destination bmesh, if NULL will use current on */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom_orig.out"},
	 {BMO_OP_SLOT_ELEMENT_BUF, "geom.out"},
	/* facemap maps from source faces to dupe
	 * faces, and from dupe faces to source faces */
	 {BMO_OP_SLOT_MAPPING, "facemap.out"},
	 {BMO_OP_SLOT_MAPPING, "boundarymap.out"},
	 {BMO_OP_SLOT_MAPPING, "isovertmap.out"},
	{0},
	},
	bmo_duplicate_exec,
	0
};

static BMOpDefine bmo_split_def = {
	"split",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom"},
	 {BMO_OP_SLOT_PTR, "dest"},             /* destination bmesh, if NULL will use current one */
	 {BMO_OP_SLOT_BOOL, "use_only_faces"},  /* when enabled. don't duplicate loose verts/edges */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom.out"},
	 {BMO_OP_SLOT_MAPPING, "boundarymap.out"},
	 {BMO_OP_SLOT_MAPPING, "isovertmap.out"},
	 {0},
	},
	bmo_split_exec,
	0
};

/*
 * Spin
 *
 * Extrude or duplicate geometry a number of times,
 * rotating and possibly translating after each step
 */
static BMOpDefine bmo_spin_def = {
	"spin",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom"},
	 {BMO_OP_SLOT_VEC, "cent"},             /* rotation center */
	 {BMO_OP_SLOT_VEC, "axis"},             /* rotation axis */
	 {BMO_OP_SLOT_VEC, "dvec"},             /* translation delta per step */
	 {BMO_OP_SLOT_FLT, "angle"},            /* total rotation angle (degrees) */
	 {BMO_OP_SLOT_INT, "steps"},            /* number of steps */
	 {BMO_OP_SLOT_BOOL, "use_duplicate"},   /* duplicate or extrude? */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom_last.out"}, /* result of last step */
	 {0},
	},
	bmo_spin_exec,
	0
};


/*
 * Similar faces search
 *
 * Find similar faces (area/material/perimeter, ...).
 */
static BMOpDefine bmo_similar_faces_def = {
	"similar_faces",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces"},    /* input faces */
	 {BMO_OP_SLOT_INT, "type"},             /* type of selection */
	 {BMO_OP_SLOT_FLT, "thresh"},           /* threshold of selection */
	 {BMO_OP_SLOT_INT, "compare"},          /* comparison method */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces.out"},  /* output faces */
	 {0},
	},
	bmo_similar_faces_exec,
	0
};

/*
 * Similar edges search
 *
 *  Find similar edges (length, direction, edge, seam, ...).
 */
static BMOpDefine bmo_similar_edges_def = {
	"similar_edges",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges"},    /* input edges */
	 {BMO_OP_SLOT_INT, "type"},             /* type of selection */
	 {BMO_OP_SLOT_FLT, "thresh"},           /* threshold of selection */
	 {BMO_OP_SLOT_INT, "compare"},          /* comparison method */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges.out"},  /* output edges */
	 {0},
	},
	bmo_similar_edges_exec,
	0
};

/*
 * Similar vertices search
 *
 * Find similar vertices (normal, face, vertex group, ...).
 */
static BMOpDefine bmo_similar_verts_def = {
	"similar_verts",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts"},    /* input vertices */
	 {BMO_OP_SLOT_INT, "type"},             /* type of selection */
	 {BMO_OP_SLOT_FLT, "thresh"},           /* threshold of selection */
	 {BMO_OP_SLOT_INT, "compare"},          /* comparison method */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts.out"},  /* output vertices */
	 {0},
	},
	bmo_similar_verts_exec,
	0
};

/*
 * uv rotation
 * cycle the uvs
 */
static BMOpDefine bmo_rotate_uvs_def = {
	"rotate_uvs",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces"},    /* input faces */
	 {BMO_OP_SLOT_BOOL, "use_ccw"},         /* rotate counter-clockwise if true, othewise clockwise */
	 {0},
	},
	/* slots_out */
	{{0}},  /* no output */
	bmo_rotate_uvs_exec,
	0
};

/*
 * uv reverse
 * reverse the uvs
 */
static BMOpDefine bmo_reverse_uvs_def = {
	"reverse_uvs",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces"},    /* input faces */
	 {0},
	},
	{{0}},  /* no output */
	bmo_reverse_uvs_exec,
	0
};

/*
 * color rotation
 * cycle the colors
 */
static BMOpDefine bmo_rotate_colors_def = {
	"rotate_colors",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces"},    /* input faces */
	 {BMO_OP_SLOT_BOOL, "use_ccw"},         /* rotate counter-clockwise if true, othewise clockwise */
	 {0},
	},
	{{0}},  /* no output */
	bmo_rotate_colors_exec,
	0
};

/*
 * color reverse
 * reverse the colors
 */
static BMOpDefine bmo_reverse_colors_def = {
	"reverse_colors",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces"},    /* input faces */
	 {0},
	},
	{{0}},  /* no output */
	bmo_reverse_colors_exec,
	0
};

/*
 * Similar vertices search
 *
 * Find similar vertices (normal, face, vertex group, ...).
 */
static BMOpDefine bmo_shortest_path_def = {
	"shortest_path",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "startv"},   /* start vertex */
	 {BMO_OP_SLOT_ELEMENT_BUF, "endv"},     /* end vertex */
	 {BMO_OP_SLOT_INT, "type"},             /* type of selection */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts.out"}, /* output vertices */
	 {0},
	},
	bmo_shortest_path_exec,
	0
};

/*
 * Edge Split
 *
 * Disconnects faces along input edges.
 */
static BMOpDefine bmo_split_edges_def = {
	"split_edges",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges"},    /* input edges */
	 /* needed for vertex rip so we can rip only half an edge at a boundary wich would otherwise split off */
	 {BMO_OP_SLOT_ELEMENT_BUF, "verts"},    /* optional tag verts, use to have greater control of splits */
	 {BMO_OP_SLOT_BOOL,        "use_verts"}, /* use 'verts' for splitting, else just find verts to split from edges */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges.out"}, /* old output disconnected edges */
	 {0},
	},
	bmo_split_edges_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES
};

/*
 * Create Grid
 *
 * Creates a grid with a variable number of subdivisions
 */
static BMOpDefine bmo_create_grid_def = {
	"create_grid",
	/* slots_in */
	{{BMO_OP_SLOT_INT,         "x_segments"}, /* number of x segments */
	 {BMO_OP_SLOT_INT,         "y_segments"}, /* number of y segments */
	 {BMO_OP_SLOT_FLT,         "size"},     /* size of the grid */
	 {BMO_OP_SLOT_MAT,         "mat"},      /* matrix to multiply the new geometry with */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts.out"}, /* output verts */
	 {0},
	},
	bmo_create_grid_exec,
	0,
};

/*
 * Create UV Sphere
 *
 * Creates a grid with a variable number of subdivisions
 */
static BMOpDefine bmo_create_uvsphere_def = {
	"create_uvsphere",
	/* slots_in */
	{{BMO_OP_SLOT_INT,         "u_segments"}, /* number of u segments */
	 {BMO_OP_SLOT_INT,         "v_segments"}, /* number of v segment */
	 {BMO_OP_SLOT_FLT,         "diameter"}, /* diameter */
	 {BMO_OP_SLOT_MAT,         "mat"}, /* matrix to multiply the new geometry with */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts.out"}, /* output verts */
	 {0},
	},
	bmo_create_uvsphere_exec,
	0,
};

/*
 * Create Ico Sphere
 *
 * Creates a grid with a variable number of subdivisions
 */
static BMOpDefine bmo_create_icosphere_def = {
	"create_icosphere",
	/* slots_in */
	{{BMO_OP_SLOT_INT,         "subdivisions"}, /* how many times to recursively subdivide the sphere */
	 {BMO_OP_SLOT_FLT,         "diameter"}, /* diameter */
	 {BMO_OP_SLOT_MAT,         "mat"}, /* matrix to multiply the new geometry with */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts.out"}, /* output verts */
	 {0},
	},
	bmo_create_icosphere_exec,
	0,
};

/*
 * Create Suzanne
 *
 * Creates a monkey.  Be wary.
 */
static BMOpDefine bmo_create_monkey_def = {
	"create_monkey",
	/* slots_in */
	{{BMO_OP_SLOT_MAT, "mat"}, /* matrix to multiply the new geometry with */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts.out"}, /* output verts */
	 {0},
	},
	bmo_create_monkey_exec,
	0,
};

/*
 * Create Cone
 *
 * Creates a cone with variable depth at both ends
 */
static BMOpDefine bmo_create_cone_def = {
	"create_cone",
	/* slots_in */
	{{BMO_OP_SLOT_BOOL, "cap_ends"},        /* wheter or not to fill in the ends with faces */
	 {BMO_OP_SLOT_BOOL, "cap_tris"},        /* fill ends with triangles instead of ngons */
	 {BMO_OP_SLOT_INT, "segments"},
	 {BMO_OP_SLOT_FLT, "diameter1"},        /* diameter of one end */
	 {BMO_OP_SLOT_FLT, "diameter2"},        /* diameter of the opposite */
	 {BMO_OP_SLOT_FLT, "depth"},            /* distance between ends */
	 {BMO_OP_SLOT_MAT, "mat"},              /* matrix to multiply the new geometry with */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts.out"}, /* output verts */
	 {0},
	},
	bmo_create_cone_exec,
	0,
};

/*
 * Creates a circle
 */
static BMOpDefine bmo_create_circle_def = {
	"create_circle",
	/* slots_in */
	{{BMO_OP_SLOT_BOOL, "cap_ends"},        /* wheter or not to fill in the ends with faces */
	 {BMO_OP_SLOT_BOOL, "cap_tris"},        /* fill ends with triangles instead of ngons */
	 {BMO_OP_SLOT_INT, "segments"},
	 {BMO_OP_SLOT_FLT, "diameter"},         /* diameter of one end */
	 {BMO_OP_SLOT_MAT, "mat"},              /* matrix to multiply the new geometry with */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts.out"}, /* output verts */
	 {0},
	},
	bmo_create_circle_exec,
	0,
};

/*
 * Create Cone
 *
 * Creates a cone with variable depth at both ends
 */
static BMOpDefine bmo_create_cube_def = {
	"create_cube",
	/* slots_in */
	{{BMO_OP_SLOT_FLT, "size"},             /* size of the cube */
	 {BMO_OP_SLOT_MAT, "mat"},              /* matrix to multiply the new geometry with */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts.out"}, /* output verts */
	 {0},
	},
	bmo_create_cube_exec,
	0,
};

/*
 * Bevel
 *
 * Bevels edges and vertices
 */
static BMOpDefine bmo_bevel_def = {
	"bevel",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom"},     /* input edges and vertices */
	 {BMO_OP_SLOT_FLT, "offset"},           /* amount to offset beveled edge */
	 {BMO_OP_SLOT_INT, "segments"},         /* number of segments in bevel */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces.out"}, /* output faces */
	 {0},
	},
#if 0  /* old bevel*/
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom"}, /* input edges and vertices */
	 {BMO_OP_SLOT_ELEMENT_BUF, "face_spans"}, /* new geometry */
	 {BMO_OP_SLOT_ELEMENT_BUF, "face_holes"}, /* new geometry */
	 {BMO_OP_SLOT_BOOL, "use_lengths"}, /* grab edge lengths from a PROP_FLT customdata layer */
	 {BMO_OP_SLOT_BOOL, "use_even"}, /* corner vert placement: use shell/angle calculations  */
	 {BMO_OP_SLOT_BOOL, "use_dist"}, /* corner vert placement: evaluate percent as a distance,
	                                  * modifier uses this. We could do this as another float setting */
	 {BMO_OP_SLOT_INT, "lengthlayer"}, /* which PROP_FLT layer to us */
	 {BMO_OP_SLOT_FLT, "percent"}, /* percentage to expand beveled edge */
	 {0},
	},
#endif
	bmo_bevel_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES
};

/*
 * Beautify Fill
 *
 * Makes triangle a bit nicer
 */
static BMOpDefine bmo_beautify_fill_def = {
	"beautify_fill",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces"}, /* input faces */
	 {BMO_OP_SLOT_ELEMENT_BUF, "constrain_edges"}, /* edges that can't be flipped */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom.out"}, /* new flipped faces and edges */
	 {0},
	},
	bmo_beautify_fill_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES
};

/*
 * Triangle Fill
 *
 * Fill edges with triangles
 */
static BMOpDefine bmo_triangle_fill_def = {
	"triangle_fill",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "edges"},    /* input edges */
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom.out"}, /* new faces and edges */
	 {0},
	},
	bmo_triangle_fill_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES
};

/*
 * Solidify
 *
 * Turns a mesh into a shell with thickness
 */
static BMOpDefine bmo_solidify_def = {
	"solidify",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom"},
	 {BMO_OP_SLOT_FLT, "thickness"},
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom.out"},
	 {0},
	},
	bmo_solidify_face_region_exec,
	0
};

/*
 * Face Inset
 *
 * Extrudes faces individually.
 */
static BMOpDefine bmo_inset_def = {
	"inset",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces"},    /* input faces */
	 {BMO_OP_SLOT_BOOL, "use_boundary"},
	 {BMO_OP_SLOT_BOOL, "use_even_offset"},
	 {BMO_OP_SLOT_BOOL, "use_relative_offset"},
	 {BMO_OP_SLOT_FLT, "thickness"},
	 {BMO_OP_SLOT_FLT, "depth"},
	 {BMO_OP_SLOT_BOOL, "use_outset"},
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces.out"}, /* output faces */
	 {0},
	},
	bmo_inset_exec,
	0
};

/*
 * Wire Frame
 *
 * Makes a wire copy of faces.
 */
static BMOpDefine bmo_wireframe_def = {
	"wireframe",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces"},   /* input faces */
	 {BMO_OP_SLOT_BOOL, "use_boundary"},
	 {BMO_OP_SLOT_BOOL, "use_even_offset"},
	 {BMO_OP_SLOT_BOOL, "use_crease"},
	 {BMO_OP_SLOT_FLT, "thickness"},
	 {BMO_OP_SLOT_BOOL, "use_relative_offset"},
	 {BMO_OP_SLOT_FLT, "depth"},
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "faces.out"}, /* output faces */
	 {0},
	},
	bmo_wireframe_exec,
	0
};

/*
 * Vertex Slide
 *
 * Translates vertes along an edge
 */
static BMOpDefine bmo_slide_vert_def = {
	"slide_vert",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "vert"},
	 {BMO_OP_SLOT_ELEMENT_BUF, "edge"},
	 {BMO_OP_SLOT_FLT, "distance_t"},
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "verts.out"},
	 {0},
	},
	bmo_slide_vert_exec,
	BMO_OP_FLAG_UNTAN_MULTIRES
};

#ifdef WITH_BULLET
/*
 * Convex Hull
 *
 * Builds a convex hull from the vertices in 'input'.
 *
 * If 'use_existing_faces' is true, the hull will not output triangles
 * that are covered by a pre-existing face.
 *
 * All hull vertices, faces, and edges are added to 'geom.out'. Any
 * input elements that end up inside the hull (i.e. are not used by an
 * output face) are added to the 'interior_geom' slot. The
 * 'unused_geom' slot will contain all interior geometry that is
 * completely unused. Lastly, 'holes_geom' contains edges and faces
 * that were in the input and are part of the hull.
 */
static BMOpDefine bmo_convex_hull_def = {
	"convex_hull",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "input"},
	 {BMO_OP_SLOT_BOOL, "use_existing_faces"},
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom.out"},
	 {BMO_OP_SLOT_ELEMENT_BUF, "geom_interior.out"},
	 {BMO_OP_SLOT_ELEMENT_BUF, "geom_unused.out"},
	 {BMO_OP_SLOT_ELEMENT_BUF, "geom_holes.out"},
	 {0},
	},
	bmo_convex_hull_exec,
	0
};
#endif

/*
 * Symmetrize
 *
 * Mekes the mesh elements in the "input" slot symmetrical. Unlike
 * normal mirroring, it only copies in one direction, as specified by
 * the "direction" slot. The edges and faces that cross the plane of
 * symmetry are split as needed to enforce symmetry.
 *
 * All new vertices, edges, and faces are added to the "geom.out" slot.
 */
static BMOpDefine bmo_symmetrize_def = {
	"symmetrize",
	/* slots_in */
	{{BMO_OP_SLOT_ELEMENT_BUF, "input"},
	 {BMO_OP_SLOT_INT, "direction"},
	 {0},
	},
	/* slots_out */
	{{BMO_OP_SLOT_ELEMENT_BUF, "geom.out"},
	 {0},
	},
	bmo_symmetrize_exec,
	0
};

BMOpDefine *opdefines[] = {
	&bmo_automerge_def,
	&bmo_average_vert_facedata_def,
	&bmo_beautify_fill_def,
	&bmo_bevel_def,
	&bmo_bisect_edges_def,
	&bmo_bmesh_to_mesh_def,
	&bmo_bridge_loops_def,
	&bmo_collapse_def,
	&bmo_collapse_uvs_def,
	&bmo_connect_verts_def,
	&bmo_contextual_create_def,
#ifdef WITH_BULLET
	&bmo_convex_hull_def,
#endif
	&bmo_create_circle_def,
	&bmo_create_cone_def,
	&bmo_create_cube_def,
	&bmo_create_grid_def,
	&bmo_create_icosphere_def,
	&bmo_create_monkey_def,
	&bmo_create_uvsphere_def,
	&bmo_create_vert_def,
	&bmo_delete_def,
	&bmo_dissolve_edge_loop_def,
	&bmo_dissolve_edges_def,
	&bmo_dissolve_faces_def,
	&bmo_dissolve_limit_def,
	&bmo_dissolve_verts_def,
	&bmo_duplicate_def,
	&bmo_edgenet_fill_def,
	&bmo_edgenet_prepare_def,
	&bmo_extrude_discrete_faces_def,
	&bmo_extrude_edge_only_def,
	&bmo_extrude_face_region_def,
	&bmo_extrude_vert_indiv_def,
	&bmo_find_doubles_def,
	&bmo_inset_def,
	&bmo_join_triangles_def,
	&bmo_mesh_to_bmesh_def,
	&bmo_mirror_def,
	&bmo_object_load_bmesh_def,
	&bmo_pointmerge_def,
	&bmo_pointmerge_facedata_def,
	&bmo_recalc_face_normals_def,
	&bmo_region_extend_def,
	&bmo_remove_doubles_def,
	&bmo_reverse_colors_def,
	&bmo_reverse_faces_def,
	&bmo_reverse_uvs_def,
	&bmo_rotate_colors_def,
	&bmo_rotate_def,
	&bmo_rotate_edges_def,
	&bmo_rotate_uvs_def,
	&bmo_scale_def,
	&bmo_shortest_path_def,
	&bmo_similar_edges_def,
	&bmo_similar_faces_def,
	&bmo_similar_verts_def,
	&bmo_slide_vert_def,
	&bmo_smooth_vert_def,
	&bmo_smooth_laplacian_vert_def,
	&bmo_solidify_def,
	&bmo_spin_def,
	&bmo_split_def,
	&bmo_split_edges_def,
	&bmo_subdivide_edges_def,
	&bmo_symmetrize_def,
	&bmo_transform_def,
	&bmo_translate_def,
	&bmo_triangle_fill_def,
	&bmo_triangulate_def,
	&bmo_unsubdivide_def,
	&bmo_weld_verts_def,
	&bmo_wireframe_def,

};

int bmesh_total_ops = (sizeof(opdefines) / sizeof(void *));
