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
 * Contributor(s): Blender Foundation.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenfont/intern/blf_internal_types.h
 *  \ingroup blf
 */


#ifndef __BLF_INTERNAL_TYPES_H__
#define __BLF_INTERNAL_TYPES_H__

#include "GPU_compatibility.h"

typedef struct GlyphCacheBLF {
	struct GlyphCacheBLF *next;
	struct GlyphCacheBLF *prev;

	/* font size. */
	int size;

	/* and dpi. */
	int dpi;

	/* and the glyphs. */
	ListBase bucket[257];

	/* fast ascii lookup */
	struct GlyphBLF *glyph_ascii_table[256];

	/* texture array, to draw the glyphs. */
	GLuint *textures;

	/* size of the array. */
	int ntex;

	/* and the last texture, aka. the current texture. */
	int cur_tex;

	/* like bftgl, we draw every glyph in a big texture, so this is the
	 * current position inside the texture.
	 */
	int x_offs;
	int y_offs;

	/* and the space from one to other. */
	unsigned int pad;

	/* and the bigger glyph in the font. */
	int max_glyph_width;
	int max_glyph_height;

	/* next two integer power of two, to build the texture. */
	int p2_width;
	int p2_height;

	/* number of glyphs in the font. */
	int num_glyphs;

	/* number of glyphs that we load here. */
	int rem_glyphs;

	/* ascender and descender value. */
	float ascender;
	float descender;
} GlyphCacheBLF;

typedef struct GlyphBLF {
	struct GlyphBLF *next;
	struct GlyphBLF *prev;

	/* and the character, as UTF8 */
	unsigned int c;

	/* freetype2 index, to speed-up the search. */
	FT_UInt idx;

	/* glyph box. */
	rctf box;

	/* advance size. */
	float advance;

	/* texture id where this glyph is store. */
	GLuint tex;

	/* position inside the texture where this glyph is store. */
	int xoff;
	int yoff;

	/* Bitmap data, from freetype. Take care that this
	 * can be NULL.
	 */
	unsigned char *bitmap;

	/* glyph width and height. */
	int width;
	int height;
	int pitch;

	/* uv coords. */
	float uv[2][2];

	/* X and Y bearing of the glyph.
	 * The X bearing is from the origin to the glyph left bbox edge.
	 * The Y bearing is from the baseline to the top of the glyph edge.
	 */
	float pos_x;
	float pos_y;

	/* with value of zero mean that we need build the texture. */
	short build_tex;
} GlyphBLF;

typedef struct FontBufInfoBLF {
	/* for draw to buffer, always set this to NULL after finish! */
	float *fbuf;

	/* the same but unsigned char */
	unsigned char *cbuf;

	/* buffer size, keep signed so comparisons with negative values work */
	int w;
	int h;

	/* number of channels. */
	int ch;

	/* display device used for color management */
	struct ColorManagedDisplay *display;

	/* and the color, the alphas is get from the glyph!
	 * color is srgb space */
	float col[4];
} FontBufInfoBLF;

typedef struct FontBLF {
	/* font name. */
	char *name;

	/* filename or NULL. */
	char *filename;

	/* aspect ratio or scale. */
	float aspect[3];

	/* initial position for draw the text. */
	float pos[3];

	/* angle in degrees. */
	float angle;
	
	/* blur: 3 or 5 large kernel */
	int blur;

	/* shadow level. */
	int shadow;

	/* and shadow offset. */
	int shadow_x;
	int shadow_y;

	/* shadow color. */
	float shadow_col[4];

	/* store color here when drawing shadow or blur. */
	float orig_col[4];

	/* Multiplied this matrix with the current one before
	 * draw the text! see blf_draw__start.
	 */
	double m[16];

	/* clipping rectangle. */
	rctf clip_rec;

	/* font dpi (default 72). */
	int dpi;

	/* font size. */
	int size;

	/* max texture size. */
	int max_tex_size;

	/* cache current OpenGL texture to save calls into the API */
	int tex_bind_state;

	/* font options. */
	int flags;

	/* list of glyph cache for this font. */
	ListBase cache;

	/* current glyph cache, size and dpi. */
	GlyphCacheBLF *glyph_cache;

	/* freetype2 lib handle. */
	FT_Library ft_lib;

	/* freetype2 face. */
	FT_Face face;

	/* data for buffer usage (drawing into a texture buffer) */
	FontBufInfoBLF buf_info;

	int locked;
} FontBLF;

typedef struct DirBLF {
	struct DirBLF *next;
	struct DirBLF *prev;

	/* full path where search fonts. */
	char *path;
} DirBLF;

#endif /* __BLF_INTERNAL_TYPES_H__ */
