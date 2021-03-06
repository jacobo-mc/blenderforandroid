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
 * The Original Code is Copyright (C) 2006 Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/nodes/composite/nodes/node_composite_sepcombRGBA.c
 *  \ingroup cmpnodes
 */


#include "node_composite_util.h"

/* **************** SEPARATE RGBA ******************** */
static bNodeSocketTemplate cmp_node_seprgba_in[] = {
	{	SOCK_RGBA, 1, N_("Image"),			1.0f, 1.0f, 1.0f, 1.0f},
	{	-1, 0, ""	}
};
static bNodeSocketTemplate cmp_node_seprgba_out[] = {
	{	SOCK_FLOAT, 0, N_("R")},
	{	SOCK_FLOAT, 0, N_("G")},
	{	SOCK_FLOAT, 0, N_("B")},
	{	SOCK_FLOAT, 0, N_("A")},
	{	-1, 0, ""	}
};

#ifdef WITH_COMPOSITOR_LEGACY

static void node_composit_exec_seprgba(void *UNUSED(data), bNode *UNUSED(node), bNodeStack **in, bNodeStack **out)
{
	/* stack order out: bw channels */
	/* stack order in: col */
	
	/* input no image? then only color operation */
	if (in[0]->data==NULL) {
		out[0]->vec[0] = in[0]->vec[0];
		out[1]->vec[0] = in[0]->vec[1];
		out[2]->vec[0] = in[0]->vec[2];
		out[3]->vec[0] = in[0]->vec[3];
	}
	else {
		/* make sure we get right rgba buffer */
		CompBuf *cbuf= typecheck_compbuf(in[0]->data, CB_RGBA);

		/* don't do any pixel processing, just copy the stack directly (faster, I presume) */
		if (out[0]->hasoutput)
			out[0]->data= valbuf_from_rgbabuf(cbuf, CHAN_R);
		if (out[1]->hasoutput)
			out[1]->data= valbuf_from_rgbabuf(cbuf, CHAN_G);
		if (out[2]->hasoutput)
			out[2]->data= valbuf_from_rgbabuf(cbuf, CHAN_B);
		if (out[3]->hasoutput)
			out[3]->data= valbuf_from_rgbabuf(cbuf, CHAN_A);
		
		if (cbuf!=in[0]->data) 
			free_compbuf(cbuf);

	}
}

#endif  /* WITH_COMPOSITOR_LEGACY */

void register_node_type_cmp_seprgba(bNodeTreeType *ttype)
{
	static bNodeType ntype;

	node_type_base(ttype, &ntype, CMP_NODE_SEPRGBA, "Separate RGBA", NODE_CLASS_CONVERTOR, 0);
	node_type_socket_templates(&ntype, cmp_node_seprgba_in, cmp_node_seprgba_out);
	node_type_size(&ntype, 80, 40, 140);
#ifdef WITH_COMPOSITOR_LEGACY
	node_type_exec(&ntype, node_composit_exec_seprgba);
#endif

	nodeRegisterType(ttype, &ntype);
}



/* **************** COMBINE RGBA ******************** */
static bNodeSocketTemplate cmp_node_combrgba_in[] = {
	{	SOCK_FLOAT, 1, N_("R"),			0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, PROP_NONE},
	{	SOCK_FLOAT, 1, N_("G"),			0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, PROP_NONE},
	{	SOCK_FLOAT, 1, N_("B"),			0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, PROP_NONE},
	{	SOCK_FLOAT, 1, N_("A"),			1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, PROP_NONE},
	{	-1, 0, ""	}
};
static bNodeSocketTemplate cmp_node_combrgba_out[] = {
	{	SOCK_RGBA, 0, N_("Image")},
	{	-1, 0, ""	}
};

#ifdef WITH_COMPOSITOR_LEGACY

static void do_combrgba(bNode *UNUSED(node), float *out, float *in1, float *in2, float *in3, float *in4)
{
	out[0] = in1[0];
	out[1] = in2[0];
	out[2] = in3[0];
	out[3] = in4[0];
}

static void node_composit_exec_combrgba(void *UNUSED(data), bNode *node, bNodeStack **in, bNodeStack **out)
{
	/* stack order out: 1 rgba channels */
	/* stack order in: 4 value channels */
	
	/* input no image? then only color operation */
	if ((in[0]->data==NULL) && (in[1]->data==NULL) && (in[2]->data==NULL) && (in[3]->data==NULL)) {
		out[0]->vec[0] = in[0]->vec[0];
		out[0]->vec[1] = in[1]->vec[0];
		out[0]->vec[2] = in[2]->vec[0];
		out[0]->vec[3] = in[3]->vec[0];
	}
	else {
		/* make output size of first available input image */
		CompBuf *cbuf;
		CompBuf *stackbuf;

		/* allocate a CompBuf the size of the first available input */
		if (in[0]->data) cbuf = in[0]->data;
		else if (in[1]->data) cbuf = in[1]->data;
		else if (in[2]->data) cbuf = in[2]->data;
		else cbuf = in[3]->data;
		
		stackbuf = alloc_compbuf(cbuf->x, cbuf->y, CB_RGBA, 1); /* allocs */
		
		composit4_pixel_processor(node, stackbuf, in[0]->data, in[0]->vec, in[1]->data, in[1]->vec, 
								  in[2]->data, in[2]->vec, in[3]->data, in[3]->vec, 
								  do_combrgba, CB_VAL, CB_VAL, CB_VAL, CB_VAL);
		
		out[0]->data= stackbuf;
	}
}

#endif  /* WITH_COMPOSITOR_LEGACY */

void register_node_type_cmp_combrgba(bNodeTreeType *ttype)
{
	static bNodeType ntype;

	node_type_base(ttype, &ntype, CMP_NODE_COMBRGBA, "Combine RGBA", NODE_CLASS_CONVERTOR, NODE_OPTIONS);
	node_type_socket_templates(&ntype, cmp_node_combrgba_in, cmp_node_combrgba_out);
	node_type_size(&ntype, 80, 40, 140);
#ifdef WITH_COMPOSITOR_LEGACY
	node_type_exec(&ntype, node_composit_exec_combrgba);
#endif

	nodeRegisterType(ttype, &ntype);
}
