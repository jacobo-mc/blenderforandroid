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
 * The Original Code is: all of this file.
 * Contributor(s): Nathan Letwory
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/space_node/node_draw.c
 *  \ingroup spnode
 *  \brief higher level node drawing for the node editor.
 */

#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_space_types.h"
#include "DNA_screen_types.h"

#include "BLI_math.h"
#include "BLI_blenlib.h"

#include "BLF_translation.h"

#include "BKE_context.h"
#include "BKE_depsgraph.h"
#include "BKE_main.h"
#include "BKE_node.h"

#include "GPU_colors.h"
#include "GPU_primitives.h"

#include "BIF_glutil.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_node.h"
#include "ED_gpencil.h"
#include "ED_space_api.h"

#include "UI_resources.h"
#include "UI_view2d.h"

#include "RNA_access.h"

#include "node_intern.h"  /* own include */
#include "COM_compositor.h"

/* XXX interface.h */
extern void ui_dropshadow(const rctf *rct, float radius, float aspect, float alpha, int select);

/* XXX update functions for node editor are a mess, needs a clear concept */
void ED_node_tree_update(SpaceNode *snode, Scene *scene)
{
	snode_set_context(snode, scene);
	
	if (snode->nodetree && snode->nodetree->id.us == 0)
		snode->nodetree->id.us = 1;
}

void ED_node_changed_update(ID *id, bNode *node)
{
	bNodeTree *nodetree, *edittree;
	int treetype;

	node_tree_from_ID(id, &nodetree, &edittree, &treetype);

	if (treetype == NTREE_SHADER) {
		DAG_id_tag_update(id, 0);

		if (GS(id->name) == ID_MA)
			WM_main_add_notifier(NC_MATERIAL | ND_SHADING_DRAW, id);
		else if (GS(id->name) == ID_LA)
			WM_main_add_notifier(NC_LAMP | ND_LIGHTING_DRAW, id);
		else if (GS(id->name) == ID_WO)
			WM_main_add_notifier(NC_WORLD | ND_WORLD_DRAW, id);
	}
	else if (treetype == NTREE_COMPOSIT) {
		if (node)
			nodeUpdate(edittree, node);
		/* don't use NodeTagIDChanged, it gives far too many recomposites for image, scene layers, ... */
			
		node = node_tree_get_editgroup(nodetree);
		if (node)
			nodeUpdateID(nodetree, node->id);

		WM_main_add_notifier(NC_SCENE | ND_NODES, id);
	}
	else if (treetype == NTREE_TEXTURE) {
		DAG_id_tag_update(id, 0);
		WM_main_add_notifier(NC_TEXTURE | ND_NODES, id);
	}
}

static int has_nodetree(bNodeTree *ntree, bNodeTree *lookup)
{
	bNode *node;
	
	if (ntree == lookup)
		return 1;
	
	for (node = ntree->nodes.first; node; node = node->next)
		if (node->type == NODE_GROUP && node->id)
			if (has_nodetree((bNodeTree *)node->id, lookup))
				return 1;
	
	return 0;
}

typedef struct NodeUpdateCalldata {
	bNodeTree *ntree;
	bNode *node;
} NodeUpdateCalldata;
static void node_generic_update_cb(void *calldata, ID *owner_id, bNodeTree *ntree)
{
	NodeUpdateCalldata *cd = (NodeUpdateCalldata *)calldata;
	/* check if nodetree uses the group stored in calldata */
	if (has_nodetree(ntree, cd->ntree))
		ED_node_changed_update(owner_id, cd->node);
}
void ED_node_generic_update(Main *bmain, bNodeTree *ntree, bNode *node)
{
	bNodeTreeType *tti = ntreeGetType(ntree->type);
	NodeUpdateCalldata cd;
	cd.ntree = ntree;
	cd.node = node;
	/* look through all datablocks, to support groups */
	tti->foreach_nodetree(bmain, &cd, node_generic_update_cb);
	
	if (ntree->type == NTREE_TEXTURE)
		ntreeTexCheckCyclics(ntree);
}

static int compare_nodes(bNode *a, bNode *b)
{
	bNode *parent;
	/* These tell if either the node or any of the parent nodes is selected.
	 * A selected parent means an unselected node is also in foreground!
	 */
	int a_select = (a->flag & NODE_SELECT), b_select = (b->flag & NODE_SELECT);
	int a_active = (a->flag & NODE_ACTIVE), b_active = (b->flag & NODE_ACTIVE);
	
	/* if one is an ancestor of the other */
	/* XXX there might be a better sorting algorithm for stable topological sort, this is O(n^2) worst case */
	for (parent = a->parent; parent; parent = parent->parent) {
		/* if b is an ancestor, it is always behind a */
		if (parent == b)
			return 1;
		/* any selected ancestor moves the node forward */
		if (parent->flag & NODE_ACTIVE)
			a_active = 1;
		if (parent->flag & NODE_SELECT)
			a_select = 1;
	}
	for (parent = b->parent; parent; parent = parent->parent) {
		/* if a is an ancestor, it is always behind b */
		if (parent == a)
			return 0;
		/* any selected ancestor moves the node forward */
		if (parent->flag & NODE_ACTIVE)
			b_active = 1;
		if (parent->flag & NODE_SELECT)
			b_select = 1;
	}

	/* if one of the nodes is in the background and the other not */
	if ((a->flag & NODE_BACKGROUND) && !(b->flag & NODE_BACKGROUND))
		return 0;
	else if (!(a->flag & NODE_BACKGROUND) && (b->flag & NODE_BACKGROUND))
		return 1;
	
	/* if one has a higher selection state (active > selected > nothing) */
	if (!b_active && a_active)
		return 1;
	else if (!b_select && (a_active || a_select))
		return 1;
	
	return 0;
}

/* Sorts nodes by selection: unselected nodes first, then selected,
 * then the active node at the very end. Relative order is kept intact!
 */
void ED_node_sort(bNodeTree *ntree)
{
	/* merge sort is the algorithm of choice here */
	bNode *first_a, *first_b, *node_a, *node_b, *tmp;
	int totnodes = BLI_countlist(&ntree->nodes);
	int k, a, b;
	
	k = 1;
	while (k < totnodes) {
		first_a = first_b = ntree->nodes.first;
		
		do {
			/* setup first_b pointer */
			for (b = 0; b < k && first_b; ++b) {
				first_b = first_b->next;
			}
			/* all batches merged? */
			if (first_b == NULL)
				break;
			
			/* merge batches */
			node_a = first_a;
			node_b = first_b;
			a = b = 0;
			while (a < k && b < k && node_b) {
				if (compare_nodes(node_a, node_b) == 0) {
					node_a = node_a->next;
					a++;
				}
				else {
					tmp = node_b;
					node_b = node_b->next;
					b++;
					BLI_remlink(&ntree->nodes, tmp);
					BLI_insertlinkbefore(&ntree->nodes, node_a, tmp);
				}
			}

			/* setup first pointers for next batch */
			first_b = node_b;
			for (; b < k; ++b) {
				/* all nodes sorted? */
				if (first_b == NULL)
					break;
				first_b = first_b->next;
			}
			first_a = first_b;
		} while (first_b);
		
		k = k << 1;
	}
}


static void do_node_internal_buttons(bContext *C, void *node_v, int event)
{
	if (event == B_NODE_EXEC) {
		SpaceNode *snode = CTX_wm_space_node(C);
		if (snode && snode->id)
			ED_node_changed_update(snode->id, node_v);
	}
}

static void node_uiblocks_init(const bContext *C, bNodeTree *ntree)
{
	bNode *node;
	char uiblockstr[32];
	
	/* add node uiBlocks in drawing order - prevents events going to overlapping nodes */
	
	for (node = ntree->nodes.first; node; node = node->next) {
		/* ui block */
		BLI_snprintf(uiblockstr, sizeof(uiblockstr), "node buttons %p", (void *)node);
		node->block = uiBeginBlock(C, CTX_wm_region(C), uiblockstr, UI_EMBOSS);
		uiBlockSetHandleFunc(node->block, do_node_internal_buttons, node);

		/* this cancels events for background nodes */
		uiBlockSetFlag(node->block, UI_BLOCK_CLIP_EVENTS);
	}
}

/* based on settings in node, sets drawing rect info. each redraw! */
static void node_update_basis(const bContext *C, bNodeTree *ntree, bNode *node)
{
	uiLayout *layout;
	PointerRNA ptr;
	bNodeSocket *nsock;
	float locx, locy;
	float dy;
	int buty;
	
	/* get "global" coords */
	nodeToView(node, 0.0f, 0.0f, &locx, &locy);
	dy = locy;
	
	/* header */
	dy -= NODE_DY;
	
	/* little bit space in top */
	if (node->outputs.first)
		dy -= NODE_DYS / 2;

	/* output sockets */
	for (nsock = node->outputs.first; nsock; nsock = nsock->next) {
		if (!nodeSocketIsHidden(nsock)) {
			nsock->locx = locx + node->width;
			nsock->locy = dy - NODE_DYS;
			dy -= NODE_DY;
		}
	}

	node->prvr.xmin = locx + NODE_DYS;
	node->prvr.xmax = locx + node->width - NODE_DYS;

	/* preview rect? */
	if (node->flag & NODE_PREVIEW) {
		if (node->preview && node->preview->rect) {
			float aspect = 1.0f;
			
			if (node->preview && node->preview->xsize && node->preview->ysize) 
				aspect = (float)node->preview->ysize / (float)node->preview->xsize;
			
			dy -= NODE_DYS / 2;
			node->prvr.ymax = dy;
			
			if (aspect <= 1.0f)
				node->prvr.ymin = dy - aspect * (node->width - NODE_DY);
			else {
				/* width correction of image */
				float dx = (node->width - NODE_DYS) - (node->width - NODE_DYS) / aspect;
				
				node->prvr.ymin = dy - (node->width - NODE_DY);
				
				node->prvr.xmin += 0.5f * dx;
				node->prvr.xmax -= 0.5f * dx;
			}

			dy = node->prvr.ymin - NODE_DYS / 2;

			/* make sure that maximums are bigger or equal to minimums */
			if (node->prvr.xmax < node->prvr.xmin) SWAP(float, node->prvr.xmax, node->prvr.xmin);
			if (node->prvr.ymax < node->prvr.ymin) SWAP(float, node->prvr.ymax, node->prvr.ymin);
		}
		else {
			float oldh = BLI_rctf_size_y(&node->prvr);
			if (oldh == 0.0f)
				oldh = 0.6f * node->width - NODE_DY;
			dy -= NODE_DYS / 2;
			node->prvr.ymax = dy;
			node->prvr.ymin = dy - oldh;
			dy = node->prvr.ymin - NODE_DYS / 2;
		}
	}

	/* buttons rect? */
	if ((node->flag & NODE_OPTIONS) && node->typeinfo->uifunc) {
		dy -= NODE_DYS / 2;

		/* set this for uifunc() that don't use layout engine yet */
		node->butr.xmin = 0;
		node->butr.xmax = node->width - 2 * NODE_DYS;
		node->butr.ymin = 0;
		node->butr.ymax = 0;
		
		RNA_pointer_create(&ntree->id, &RNA_Node, node, &ptr);
		
		layout = uiBlockLayout(node->block, UI_LAYOUT_VERTICAL, UI_LAYOUT_PANEL,
		                       locx + NODE_DYS, dy, node->butr.xmax, NODE_DY, UI_GetStyle());
		uiLayoutSetContextPointer(layout, "node", &ptr);
		
		node->typeinfo->uifunc(layout, (bContext *)C, &ptr);
		
		uiBlockEndAlign(node->block);
		uiBlockLayoutResolve(node->block, NULL, &buty);
		
		dy = buty - NODE_DYS / 2;
	}

	/* input sockets */
	for (nsock = node->inputs.first; nsock; nsock = nsock->next) {
		if (!nodeSocketIsHidden(nsock)) {
			nsock->locx = locx;
			nsock->locy = dy - NODE_DYS;
			dy -= NODE_DY;
		}
	}
	
	/* little bit space in end */
	if (node->inputs.first || (node->flag & (NODE_OPTIONS | NODE_PREVIEW)) == 0)
		dy -= NODE_DYS / 2;

	node->totr.xmin = locx;
	node->totr.xmax = locx + node->width;
	node->totr.ymax = locy;
	node->totr.ymin = min_ff(dy, locy - 2 * NODE_DY);
	
	/* Set the block bounds to clip mouse events from underlying nodes.
	 * Add a margin for sockets on each side.
	 */
	uiExplicitBoundsBlock(node->block,
	                      node->totr.xmin - NODE_SOCKSIZE,
	                      node->totr.ymin,
	                      node->totr.xmax + NODE_SOCKSIZE,
	                      node->totr.ymax);
}

/* based on settings in node, sets drawing rect info. each redraw! */
static void node_update_hidden(bNode *node)
{
	bNodeSocket *nsock;
	float locx, locy;
	float rad, drad, hiddenrad = HIDDEN_RAD;
	int totin = 0, totout = 0, tot;
	
	/* get "global" coords */
	nodeToView(node, 0.0f, 0.0f, &locx, &locy);

	/* calculate minimal radius */
	for (nsock = node->inputs.first; nsock; nsock = nsock->next)
		if (!nodeSocketIsHidden(nsock))
			totin++;
	for (nsock = node->outputs.first; nsock; nsock = nsock->next)
		if (!nodeSocketIsHidden(nsock))
			totout++;
	
	tot = MAX2(totin, totout);
	if (tot > 4) {
		hiddenrad += 5.0f * (float)(tot - 4);
	}
	
	node->totr.xmin = locx;
	node->totr.xmax = locx + 3 * hiddenrad + node->miniwidth;
	node->totr.ymax = locy + (hiddenrad - 0.5f * NODE_DY);
	node->totr.ymin = node->totr.ymax - 2 * hiddenrad;
	
	/* output sockets */
	rad = drad = (float)M_PI / (1.0f + (float)totout);
	
	for (nsock = node->outputs.first; nsock; nsock = nsock->next) {
		if (!nodeSocketIsHidden(nsock)) {
			nsock->locx = node->totr.xmax - hiddenrad + (float)sin(rad) * hiddenrad;
			nsock->locy = node->totr.ymin + hiddenrad + (float)cos(rad) * hiddenrad;
			rad += drad;
		}
	}
	
	/* input sockets */
	rad = drad = -(float)M_PI / (1.0f + (float)totin);
	
	for (nsock = node->inputs.first; nsock; nsock = nsock->next) {
		if (!nodeSocketIsHidden(nsock)) {
			nsock->locx = node->totr.xmin + hiddenrad + (float)sin(rad) * hiddenrad;
			nsock->locy = node->totr.ymin + hiddenrad + (float)cos(rad) * hiddenrad;
			rad += drad;
		}
	}

	/* Set the block bounds to clip mouse events from underlying nodes.
	 * Add a margin for sockets on each side.
	 */
	uiExplicitBoundsBlock(node->block,
	                      node->totr.xmin - NODE_SOCKSIZE,
	                      node->totr.ymin,
	                      node->totr.xmax + NODE_SOCKSIZE,
	                      node->totr.ymax);
}

void node_update_default(const bContext *C, bNodeTree *ntree, bNode *node)
{
	if (node->flag & NODE_HIDDEN)
		node_update_hidden(node);
	else
		node_update_basis(C, ntree, node);
}

int node_select_area_default(bNode *node, int x, int y)
{
	return BLI_rctf_isect_pt(&node->totr, x, y);
}

int node_tweak_area_default(bNode *node, int x, int y)
{
	return BLI_rctf_isect_pt(&node->totr, x, y);
}

int node_get_colorid(bNode *node)
{
	if (node->typeinfo->nclass == NODE_CLASS_INPUT)
		return TH_NODE_IN_OUT;
	if (node->typeinfo->nclass == NODE_CLASS_OUTPUT) {
		if (node->flag & NODE_DO_OUTPUT)
			return TH_NODE_IN_OUT;
		else
			return TH_NODE;
	}
	if (node->typeinfo->nclass == NODE_CLASS_CONVERTOR)
		return TH_NODE_CONVERTOR;
	if (ELEM3(node->typeinfo->nclass, NODE_CLASS_OP_COLOR, NODE_CLASS_OP_VECTOR, NODE_CLASS_OP_FILTER))
		return TH_NODE_OPERATOR;
	if (node->typeinfo->nclass == NODE_CLASS_GROUP)
		return TH_NODE_GROUP;
	return TH_NODE;
}

/* note: in cmp_util.c is similar code, for node_compo_pass_on()
 *       the same goes for shader and texture nodes. */
/* note: in node_edit.c is similar code, for untangle node */
static void node_draw_mute_line(View2D *v2d, SpaceNode *snode, bNode *node)
{
	bNodeLink *link;

	glEnable(GL_BLEND);
	glEnable(GL_LINE_SMOOTH);

	for (link = node->internal_links.first; link; link = link->next)
		node_draw_link_bezier(v2d, snode, link, TH_REDALERT, 0, TH_WIRE, 0, TH_WIRE);

	glDisable(GL_BLEND);
	glDisable(GL_LINE_SMOOTH);
}

/* this might have some more generic use */
static void node_circle_draw(float x, float y, float size, char *col, int highlight)
{
	/* 16 values of sin function */
	static float si[16] = {
		0.00000000f, 0.39435585f, 0.72479278f, 0.93775213f,
		0.99871650f, 0.89780453f, 0.65137248f, 0.29936312f,
		-0.10116832f, -0.48530196f, -0.79077573f, -0.96807711f,
		-0.98846832f, -0.84864425f, -0.57126821f, -0.20129852f
	};
	/* 16 values of cos function */
	static float co[16] = {
		1.00000000f, 0.91895781f, 0.68896691f, 0.34730525f,
		-0.05064916f, -0.44039415f, -0.75875812f, -0.95413925f,
		-0.99486932f, -0.87434661f, -0.61210598f, -0.25065253f,
		0.15142777f, 0.52896401f, 0.82076344f, 0.97952994f,
	};
	int a;
	
	gpuCurrentColor3ub(col[0], col[1], col[2]);

	gpuBegin(GL_TRIANGLE_FAN);
	for (a = 0; a < 16; a++)
		gpuVertex2f(x  +  size*si[a], y + size*co[a]);
	gpuEnd();
	
	if (highlight) {
		UI_ThemeColor(TH_TEXT_HI);
		glLineWidth(1.5f);
	}
	else {
		gpuCurrentColor4x(CPACK_BLACK, 0.588f);
	}
	glEnable(GL_BLEND);
	glEnable(GL_LINE_SMOOTH);
	gpuBegin(GL_LINE_LOOP);
	for (a = 0; a < 16; a++)
		gpuVertex2f(x + size*si[a], y + size*co[a]);
	gpuEnd();
	glDisable(GL_LINE_SMOOTH);
	glDisable(GL_BLEND);
	glLineWidth(1.0f);
}

void node_socket_circle_draw(bNodeTree *UNUSED(ntree), bNodeSocket *sock, float size, int highlight)
{
	bNodeSocketType *stype = ntreeGetSocketType(sock->type);
	node_circle_draw(sock->locx, sock->locy, size, stype->ui_color, highlight);
}

/* **************  Socket callbacks *********** */

/* not a callback */
static void node_draw_preview(bNodePreview *preview, rctf *prv)
{
	float xscale = BLI_rctf_size_x(prv) / ((float)preview->xsize);
	float yscale = BLI_rctf_size_y(prv) / ((float)preview->ysize);
	float tile   = BLI_rctf_size_x(prv) / 10.0f;
	float x, y;
	
	/* draw checkerboard backdrop to show alpha */
	gpuCurrentGray3f(0.471f);
	gpuSingleFilledRectf(prv->xmin, prv->ymin, prv->xmax, prv->ymax);

	gpuCurrentGray3f(0.627f);

	for (y = prv->ymin; y < prv->ymax; y += tile * 2) {
		for (x = prv->xmin; x < prv->xmax; x += tile * 2) {
			float tilex = tile, tiley = tile;

			if (x + tile > prv->xmax)
				tilex = prv->xmax - x;
			if (y + tile > prv->ymax)
				tiley = prv->ymax - y;

			gpuSingleFilledRectf(x, y, x + tilex, y + tiley);
		}
	}
	for (y = prv->ymin + tile; y < prv->ymax; y += tile * 2) {
		for (x = prv->xmin + tile; x < prv->xmax; x += tile * 2) {
			float tilex = tile, tiley = tile;

			if (x + tile > prv->xmax)
				tilex = prv->xmax - x;
			if (y + tile > prv->ymax)
				tiley = prv->ymax - y;

			gpuSingleFilledRectf(x, y, x + tilex, y + tiley);
		}
	}
	
	glPixelZoom(xscale, yscale);

	glEnable(GL_BLEND);  /* premul graphics */
	
	gpuCurrentColor3x(CPACK_WHITE);
	glaDrawPixelsTex(prv->xmin, prv->ymin, preview->xsize, preview->ysize, GL_UNSIGNED_BYTE, preview->rect);
	
	glDisable(GL_BLEND);
	glPixelZoom(1.0f, 1.0f);

	UI_ThemeColorShadeAlpha(TH_BACK, -15, +100);
	gpuSingleWireRectf(prv->xmin, prv->ymin, prv->xmax, prv->ymax);
	
}

/* common handle function for operator buttons that need to select the node first */
static void node_toggle_button_cb(struct bContext *C, void *node_argv, void *op_argv)
{
	bNode *node = (bNode *)node_argv;
	const char *opname = (const char *)op_argv;
	
	/* select & activate only the button's node */
	node_select_single(C, node);
	
	WM_operator_name_call(C, opname, WM_OP_INVOKE_DEFAULT, NULL);
}

void node_draw_shadow(SpaceNode *snode, bNode *node, float radius, float alpha)
{
	rctf *rct = &node->totr;
	
	uiSetRoundBox(UI_CNR_ALL);
	if (node->parent == NULL)
		ui_dropshadow(rct, radius, snode->aspect, alpha, node->flag & SELECT);
	else {
		const float margin = 3.0f;

		gpuCurrentColor4x(CPACK_BLACK, 0.333f);
		glEnable(GL_BLEND);
		uiRoundBox(rct->xmin - margin, rct->ymin - margin,
		           rct->xmax + margin, rct->ymax + margin, radius + margin);
		glDisable(GL_BLEND);
	}
}

static void node_draw_basis(const bContext *C, ARegion *ar, SpaceNode *snode, bNodeTree *ntree, bNode *node)
{
	bNodeSocket *sock;
	rctf *rct = &node->totr;
	float iconofs;
	/* float socket_size = NODE_SOCKSIZE*U.dpi/72; */ /* UNUSED */
	float iconbutw = 0.8f * UI_UNIT_X;
	int color_id = node_get_colorid(node);
	char showname[128]; /* 128 used below */
	View2D *v2d = &ar->v2d;
	
	/* hurmf... another candidate for callback, have to see how this works first */
	if (node->id && node->block && snode->treetype == NTREE_SHADER)
		nodeShaderSynchronizeID(node, 0);
	
	/* skip if out of view */
	if (BLI_rctf_isect(&node->totr, &ar->v2d.cur, NULL) == FALSE) {
		uiEndBlock(C, node->block);
		node->block = NULL;
		return;
	}
	
	/* shadow */
	node_draw_shadow(snode, node, BASIS_RAD, 1.0f);
	
	/* header */
	if (color_id == TH_NODE)
		UI_ThemeColorShade(color_id, -20);
	else
		UI_ThemeColor(color_id);
	
	if (node->flag & NODE_MUTED)
		UI_ThemeColorBlend(color_id, TH_REDALERT, 0.5f);

#ifdef WITH_COMPOSITOR
	if (ntree->type == NTREE_COMPOSIT && (snode->flag & SNODE_SHOW_HIGHLIGHT)) {
		if (COM_isHighlightedbNode(node)) {
			UI_ThemeColorBlend(color_id, TH_ACTIVE, 0.5f);
		}
	}
#endif

	uiSetRoundBox(UI_CNR_TOP_LEFT | UI_CNR_TOP_RIGHT);
	uiRoundBox(rct->xmin, rct->ymax - NODE_DY, rct->xmax, rct->ymax, BASIS_RAD);
	
	/* show/hide icons */
	iconofs = rct->xmax - 7.0f;
	
	/* preview */
	if (node->typeinfo->flag & NODE_PREVIEW) {
		uiBut *but;
		iconofs -= iconbutw;
		uiBlockSetEmboss(node->block, UI_EMBOSSN);
		but = uiDefIconBut(node->block, TOGBUT, B_REDR, ICON_MATERIAL,
		                   iconofs, rct->ymax - NODE_DY, iconbutw, UI_UNIT_Y, NULL, 0, 0, 0, 0, "");
		uiButSetFunc(but, node_toggle_button_cb, node, (void *)"NODE_OT_preview_toggle");
		/* XXX this does not work when node is activated and the operator called right afterwards,
		 * since active ID is not updated yet (needs to process the notifier).
		 * This can only work as visual indicator!
		 */
//		if (!(node->flag & (NODE_ACTIVE_ID|NODE_DO_OUTPUT)))
//			uiButSetFlag(but, UI_BUT_DISABLED);
		uiBlockSetEmboss(node->block, UI_EMBOSS);
	}
	/* group edit */
	if (node->type == NODE_GROUP) {
		uiBut *but;
		iconofs -= iconbutw;
		uiBlockSetEmboss(node->block, UI_EMBOSSN);
		but = uiDefIconBut(node->block, TOGBUT, B_REDR, ICON_NODETREE,
		                   iconofs, rct->ymax - NODE_DY, iconbutw, UI_UNIT_Y, NULL, 0, 0, 0, 0, "");
		uiButSetFunc(but, node_toggle_button_cb, node, (void *)"NODE_OT_group_edit");
		uiBlockSetEmboss(node->block, UI_EMBOSS);
	}
	
	/* title */
	if (node->flag & SELECT) 
		UI_ThemeColor(TH_SELECT);
	else
		UI_ThemeColorBlendShade(TH_TEXT, color_id, 0.4f, 10);
	
	/* open/close entirely? */
	{
		uiBut *but;
		int but_size = UI_UNIT_X * 0.6f;
		/* XXX button uses a custom triangle draw below, so make it invisible without icon */
		uiBlockSetEmboss(node->block, UI_EMBOSSN);
		but = uiDefBut(node->block, TOGBUT, B_REDR, "",
		               rct->xmin + 10.0f - but_size / 2, rct->ymax - NODE_DY / 2.0f - but_size / 2,
		               but_size, but_size, NULL, 0, 0, 0, 0, "");
		uiButSetFunc(but, node_toggle_button_cb, node, (void *)"NODE_OT_hide_toggle");
		uiBlockSetEmboss(node->block, UI_EMBOSS);
		
		/* custom draw function for this button */
		UI_DrawTriIcon(rct->xmin + 10.0f, rct->ymax - NODE_DY / 2.0f, 'v');
	}
	
	/* this isn't doing anything for the label, so commenting out */
#if 0
	if (node->flag & SELECT) 
		UI_ThemeColor(TH_TEXT_HI);
	else
		UI_ThemeColor(TH_TEXT);
#endif
	
	BLI_strncpy(showname, nodeLabel(node), sizeof(showname));
	
	//if (node->flag & NODE_MUTED)
	//	BLI_snprintf(showname, sizeof(showname), "[%s]", showname); /* XXX - don't print into self! */
	
	uiDefBut(node->block, LABEL, 0, showname,
	         (int)(rct->xmin + (NODE_MARGIN_X / snode->aspect_sqrt)), (int)(rct->ymax - NODE_DY),
	         (short)(iconofs - rct->xmin - 18.0f), (short)NODE_DY,
	         NULL, 0, 0, 0, 0, "");

	/* body */
	if (node->flag & NODE_CUSTOM_COLOR)
		gpuCurrentColor3fv(node->color);
	else
		UI_ThemeColor4(TH_NODE);
	glEnable(GL_BLEND);
	uiSetRoundBox(UI_CNR_BOTTOM_LEFT | UI_CNR_BOTTOM_RIGHT);
	uiRoundBox(rct->xmin, rct->ymin, rct->xmax, rct->ymax - NODE_DY, BASIS_RAD);
	glDisable(GL_BLEND);

	/* outline active and selected emphasis */
	if (node->flag & SELECT) {
		glEnable(GL_BLEND);
		glEnable(GL_LINE_SMOOTH);
		
		if (node->flag & NODE_ACTIVE)
			UI_ThemeColorShadeAlpha(TH_ACTIVE, 0, -40);
		else
			UI_ThemeColorShadeAlpha(TH_SELECT, 0, -40);
		uiSetRoundBox(UI_CNR_ALL);
		uiDrawBox(GL_LINE_LOOP, rct->xmin, rct->ymin, rct->xmax, rct->ymax, BASIS_RAD);
		
		glDisable(GL_LINE_SMOOTH);
		glDisable(GL_BLEND);
	}
	
	/* disable lines */
	if (node->flag & NODE_MUTED)
		node_draw_mute_line(v2d, snode, node);

	gpuImmediateFormat_C4_V2();

	/* socket inputs, buttons */
	for (sock = node->inputs.first; sock; sock = sock->next) {
		if (nodeSocketIsHidden(sock))
			continue;

		node_socket_circle_draw(ntree, sock, NODE_SOCKSIZE, sock->flag & SELECT);
		
		node->typeinfo->drawinputfunc(C, node->block, ntree, node, sock, IFACE_(sock->name),
		                              sock->locx + (NODE_DYS / snode->aspect_sqrt), sock->locy - NODE_DYS,
		                              node->width - NODE_DY);
	}

	/* socket outputs */
	for (sock = node->outputs.first; sock; sock = sock->next) {
		if (nodeSocketIsHidden(sock))
			continue;

		node_socket_circle_draw(ntree, sock, NODE_SOCKSIZE, sock->flag & SELECT);
		
		node->typeinfo->drawoutputfunc(C, node->block, ntree, node, sock, IFACE_(sock->name),
		                               sock->locx - node->width + (NODE_DYS / snode->aspect_sqrt), sock->locy - NODE_DYS,
		                               node->width - NODE_DY);
	}

	gpuImmediateUnformat();

	/* preview */
	if (node->flag & NODE_PREVIEW) {
		if (node->preview && node->preview->rect && !BLI_rctf_is_empty(&node->prvr))
			node_draw_preview(node->preview, &node->prvr);
	}
	
	UI_ThemeClearColor(color_id);
		
	uiEndBlock(C, node->block);
	uiDrawBlock(C, node->block);
	node->block = NULL;
}

static void node_draw_hidden(const bContext *C, ARegion *ar, SpaceNode *snode, bNodeTree *ntree, bNode *node)
{
	bNodeSocket *sock;
	rctf *rct = &node->totr;
	float dx, centy = BLI_rctf_cent_y(rct);
	float hiddenrad = BLI_rctf_size_y(rct) / 2.0f;
	float socket_size = NODE_SOCKSIZE * UI_DPI_ICON_FAC;
	int color_id = node_get_colorid(node);
	char showname[128]; /* 128 is used below */
	
	/* shadow */
	node_draw_shadow(snode, node, hiddenrad, 1.0f);

	/* body */
	UI_ThemeColor(color_id);
	if (node->flag & NODE_MUTED)
		UI_ThemeColorBlend(color_id, TH_REDALERT, 0.5f);

#ifdef WITH_COMPOSITOR
	if (ntree->type == NTREE_COMPOSIT && (snode->flag & SNODE_SHOW_HIGHLIGHT)) {
		if (COM_isHighlightedbNode(node)) {
			UI_ThemeColorBlend(color_id, TH_ACTIVE, 0.5f);
		}
	}
#else
	(void)ntree;
#endif
	
	uiRoundBox(rct->xmin, rct->ymin, rct->xmax, rct->ymax, hiddenrad);
	
	/* outline active and selected emphasis */
	if (node->flag & SELECT) {
		glEnable(GL_BLEND);
		glEnable(GL_LINE_SMOOTH);
		
		if (node->flag & NODE_ACTIVE)
			UI_ThemeColorShadeAlpha(TH_ACTIVE, 0, -40);
		else
			UI_ThemeColorShadeAlpha(TH_SELECT, 0, -40);
		uiDrawBox(GL_LINE_LOOP, rct->xmin, rct->ymin, rct->xmax, rct->ymax, hiddenrad);
		
		glDisable(GL_LINE_SMOOTH);
		glDisable(GL_BLEND);
	}
	
	/* title */
	if (node->flag & SELECT) 
		UI_ThemeColor(TH_SELECT);
	else
		UI_ThemeColorBlendShade(TH_TEXT, color_id, 0.4f, 10);
	
	/* open entirely icon */
	{
		uiBut *but;
		int but_size = UI_UNIT_X * 0.6f;
		/* XXX button uses a custom triangle draw below, so make it invisible without icon */
		uiBlockSetEmboss(node->block, UI_EMBOSSN);
		but = uiDefBut(node->block, TOGBUT, B_REDR, "",
		               rct->xmin + 10.0f - but_size / 2, centy - but_size / 2,
		               but_size, but_size, NULL, 0, 0, 0, 0, "");
		uiButSetFunc(but, node_toggle_button_cb, node, (void *)"NODE_OT_hide_toggle");
		uiBlockSetEmboss(node->block, UI_EMBOSS);
		
		/* custom draw function for this button */
		UI_DrawTriIcon(rct->xmin + 10.0f, centy, 'h');
	}
	
	/* disable lines */
	if (node->flag & NODE_MUTED)
		node_draw_mute_line(&ar->v2d, snode, node);
	
	if (node->flag & SELECT) 
		UI_ThemeColor(TH_SELECT);
	else
		UI_ThemeColor(TH_TEXT);
	
	if (node->miniwidth > 0.0f) {
		BLI_strncpy(showname, nodeLabel(node), sizeof(showname));
		
		//if (node->flag & NODE_MUTED)
		//	BLI_snprintf(showname, sizeof(showname), "[%s]", showname); /* XXX - don't print into self! */

		uiDefBut(node->block, LABEL, 0, showname,
		         (int)(rct->xmin + (NODE_MARGIN_X / snode->aspect_sqrt)), (int)(centy - 10),
		         (short)(BLI_rctf_size_x(rct) - 18.0f - 12.0f), (short)NODE_DY,
		         NULL, 0, 0, 0, 0, "");
	}

	gpuImmediateFormat_C4_V2(); // DOODLE: 4 theme colored lines
	gpuBegin(GL_LINES);

	/* scale widget thing */

	dx = 10.0f;
	UI_ThemeAppendColorShade(color_id, -10);
	gpuAppendLinef(rct->xmax - dx, centy - 4.0f, rct->xmax-dx, centy+4.0f);
	gpuAppendLinef(rct->xmax - dx - 3.0f*snode->aspect, centy - 4.0f, rct->xmax - dx - 3.0f*snode->aspect, centy + 4.0f);

	dx -= snode->aspect;
	UI_ThemeAppendColorShade(color_id, +30);
	gpuAppendLinef(rct->xmax - dx, centy - 4.0f, rct->xmax - dx, centy + 4.0f);
	gpuAppendLinef(rct->xmax - dx - 3.0f*snode->aspect, centy - 4.0f, rct->xmax - dx - 3.0f*snode->aspect, centy + 4.0f);

	gpuEnd();
	gpuImmediateUnformat();

	/* sockets */
	for (sock = node->inputs.first; sock; sock = sock->next) {
		if (!nodeSocketIsHidden(sock))
			node_socket_circle_draw(snode->nodetree, sock, socket_size, sock->flag & SELECT);
	}
	
	for (sock = node->outputs.first; sock; sock = sock->next) {
		if (!nodeSocketIsHidden(sock))
			node_socket_circle_draw(snode->nodetree, sock, socket_size, sock->flag & SELECT);
	}
	
	uiEndBlock(C, node->block);
	uiDrawBlock(C, node->block);
	node->block = NULL;
}

int node_get_resize_cursor(int directions)
{
	if (directions == 0)
		return CURSOR_STD;
	else if ((directions & ~(NODE_RESIZE_TOP | NODE_RESIZE_BOTTOM)) == 0)
		return CURSOR_Y_MOVE;
	else if ((directions & ~(NODE_RESIZE_RIGHT | NODE_RESIZE_LEFT)) == 0)
		return CURSOR_X_MOVE;
	else
		return CURSOR_EDIT;
}

void node_set_cursor(wmWindow *win, SpaceNode *snode)
{
	bNodeTree *ntree = snode->edittree;
	bNode *node;
	bNodeSocket *sock;
	int cursor = CURSOR_STD;
	
	if (ntree) {
		if (node_find_indicated_socket(snode, &node, &sock, SOCK_IN | SOCK_OUT)) {
			/* pass */
		}
		else {
			/* check nodes front to back */
			for (node = ntree->nodes.last; node; node = node->prev) {
				if (BLI_rctf_isect_pt(&node->totr, snode->cursor[0], snode->cursor[1]))
					break;  /* first hit on node stops */
			}
			if (node) {
				int dir = node->typeinfo->resize_area_func(node, snode->cursor[0], snode->cursor[1]);
				cursor = node_get_resize_cursor(dir);
			}
		}
	}
	
	WM_cursor_set(win, cursor);
}

void node_draw_default(const bContext *C, ARegion *ar, SpaceNode *snode, bNodeTree *ntree, bNode *node)
{
	if (node->flag & NODE_HIDDEN)
		node_draw_hidden(C, ar, snode, ntree, node);
	else
		node_draw_basis(C, ar, snode, ntree, node);
}

static void node_update(const bContext *C, bNodeTree *ntree, bNode *node)
{
	if (node->typeinfo->drawupdatefunc)
		node->typeinfo->drawupdatefunc(C, ntree, node);
}

void node_update_nodetree(const bContext *C, bNodeTree *ntree, float offsetx, float offsety)
{
	bNode *node;
	
	/* update nodes front to back, so children sizes get updated before parents */
	for (node = ntree->nodes.last; node; node = node->prev) {
		/* XXX little hack */
		node->locx += offsetx;
		node->locy += offsety;
		
		node_update(C, ntree, node);
		
		node->locx -= offsetx;
		node->locy -= offsety;
	}
}

static void node_draw(const bContext *C, ARegion *ar, SpaceNode *snode, bNodeTree *ntree, bNode *node)
{
	if (node->typeinfo->drawfunc)
		node->typeinfo->drawfunc(C, ar, snode, ntree, node);
}

#define USE_DRAW_TOT_UPDATE

void node_draw_nodetree(const bContext *C, ARegion *ar, SpaceNode *snode, bNodeTree *ntree)
{
	bNode *node;
	bNodeLink *link;
	int a;
	
	if (ntree == NULL) return;      /* groups... */

#ifdef USE_DRAW_TOT_UPDATE
	if (ntree->nodes.first) {
		BLI_rctf_init_minmax(&ar->v2d.tot);
	}
#endif

	/* draw background nodes, last nodes in front */
	for (a = 0, node = ntree->nodes.first; node; node = node->next, a++) {

#ifdef USE_DRAW_TOT_UPDATE
		/* unrelated to background nodes, update the v2d->tot,
		 * can be anywhere before we draw the scroll bars */
		BLI_rctf_union(&ar->v2d.tot, &node->totr);
#endif

		if (!(node->flag & NODE_BACKGROUND))
			continue;
		node->nr = a;        /* index of node in list, used for exec event code */
		node_draw(C, ar, snode, ntree, node);
	}
	
	/* node lines */
	glEnable(GL_BLEND);
	glEnable(GL_LINE_SMOOTH);
	for (link = ntree->links.first; link; link = link->next)
		node_draw_link(&ar->v2d, snode, link);
	glDisable(GL_LINE_SMOOTH);
	glDisable(GL_BLEND);
	
	/* draw foreground nodes, last nodes in front */
	for (a = 0, node = ntree->nodes.first; node; node = node->next, a++) {
		if (node->flag & NODE_BACKGROUND)
			continue;
		node->nr = a;        /* index of node in list, used for exec event code */
		node_draw(C, ar, snode, ntree, node);
	}
}

void drawnodespace(const bContext *C, ARegion *ar, View2D *v2d)
{
	View2DScrollers *scrollers;
	SpaceNode *snode = CTX_wm_space_node(C);
	bNodeLinkDrag *nldrag;
	LinkData *linkdata;
	
	UI_ThemeClearColor(TH_BACK);
	gpuClear(GL_COLOR_BUFFER_BIT);

	UI_view2d_view_ortho(v2d);
	
	//uiFreeBlocksWin(&sa->uiblocks, sa->win);

	ED_region_draw_cb_draw(C, ar, REGION_DRAW_PRE_VIEW);

	/* only set once */
	glEnable(GL_MAP1_VERTEX_3);

	/* aspect+font, set each time */
	snode->aspect = BLI_rctf_size_x(&v2d->cur) / (float)ar->winx;
	snode->aspect_sqrt = sqrtf(snode->aspect);
	// XXX snode->curfont = uiSetCurFont_ext(snode->aspect);

	/* grid */
	UI_view2d_multi_grid_draw(v2d, 25.0f, 5, 2);

	/* backdrop */
	draw_nodespace_back_pix(C, ar, snode);
	
	/* nodes */
	snode_set_context(snode, CTX_data_scene(C));
	
	if (snode->nodetree) {
		bNode *node;
		/* void** highlights = 0; */ /* UNUSED */
		
		node_uiblocks_init(C, snode->nodetree);
		
		/* uiBlocks must be initialized in drawing order for correct event clipping.
		 * Node group internal blocks added after the main group block.
		 */
		for (node = snode->nodetree->nodes.first; node; node = node->next) {
			if (node->flag & NODE_GROUP_EDIT)
				node_uiblocks_init(C, (bNodeTree *)node->id);
		}
		
		node_update_nodetree(C, snode->nodetree, 0.0f, 0.0f);

#ifdef WITH_COMPOSITOR
		if (snode->nodetree->type == NTREE_COMPOSIT) {
			COM_startReadHighlights();
		}
#endif

		node_draw_nodetree(C, ar, snode, snode->nodetree);
		
		#if 0
		/* active group */
		for (node = snode->nodetree->nodes.first; node; node = node->next) {
			if (node->flag & NODE_GROUP_EDIT)
				node_draw_group(C, ar, snode, snode->nodetree, node);
		}
		#endif
	}
	
	/* temporary links */
	glEnable(GL_BLEND);
	glEnable(GL_LINE_SMOOTH);
	for (nldrag = snode->linkdrag.first; nldrag; nldrag = nldrag->next) {
		for (linkdata = nldrag->links.first; linkdata; linkdata = linkdata->next) {
			node_draw_link(&ar->v2d, snode, (bNodeLink *)linkdata->data);
		}
	}
	glDisable(GL_LINE_SMOOTH);
	glDisable(GL_BLEND);
	
	ED_region_draw_cb_draw(C, ar, REGION_DRAW_POST_VIEW);
	
	/* draw grease-pencil ('canvas' strokes) */
	if (snode->nodetree)
		draw_gpencil_view2d(C, 1);
	
	/* reset view matrix */
	UI_view2d_view_restore(C);
	
	/* draw grease-pencil (screen strokes, and also paintbuffer) */
	if (snode->nodetree)
		draw_gpencil_view2d(C, 0);
	
	/* scrollers */
	scrollers = UI_view2d_scrollers_calc(C, v2d, 10, V2D_GRID_CLAMP, V2D_ARG_DUMMY, V2D_ARG_DUMMY);
	UI_view2d_scrollers_draw(C, v2d, scrollers);
	UI_view2d_scrollers_free(scrollers);
}
