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

/** \file blender/blenfont/intern/blf.c
 *  \ingroup blf
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <ft2build.h>

#include FT_FREETYPE_H
#include FT_GLYPH_H

#include "MEM_guardedalloc.h"

#include "DNA_listBase.h"
#include "DNA_vec_types.h"

#include "BLF_api.h"

#include "blf_internal_types.h"
#include "blf_internal.h"

#include "GPU_compatibility.h"

/* Max number of font in memory.
 * Take care that now every font have a glyph cache per size/dpi,
 * so we don't need load the same font with different size, just
 * load one and call BLF_size.
 */
#define BLF_MAX_FONT 16

/* Font array. */
static FontBLF *global_font[BLF_MAX_FONT] = {0};

/* Default size and dpi, for BLF_draw_default. */
static int global_font_default = -1;
static int global_font_points = 11;
static int global_font_dpi = 72;

/* XXX, should these be made into global_font_'s too? */
int blf_mono_font = -1;
int blf_mono_font_render = -1;

static FontBLF *blf_get(int fontid)
{
	if (fontid >= 0 && fontid < BLF_MAX_FONT)
		return global_font[fontid];
	return NULL;
}

int BLF_init(int points, int dpi)
{
	int i;

	for (i = 0; i < BLF_MAX_FONT; i++)
		global_font[i] = NULL;

	global_font_points = points;
	global_font_dpi = dpi;
	return blf_font_init();
}

void BLF_exit(void)
{
	FontBLF *font;
	int i;

	for (i = 0; i < BLF_MAX_FONT; i++) {
		font = global_font[i];
		if (font) {
			blf_font_free(font);
			global_font[i] = NULL;
		}
	}

	blf_font_exit();
}

void BLF_cache_clear(void)
{
	FontBLF *font;
	int i;

	for (i = 0; i < BLF_MAX_FONT; i++) {
		font = global_font[i];
		if (font)
			blf_glyph_cache_clear(font);
	}
}

static int blf_search(const char *name)
{
	FontBLF *font;
	int i;

	for (i = 0; i < BLF_MAX_FONT; i++) {
		font = global_font[i];
		if (font && (!strcmp(font->name, name)))
			return i;
	}

	return -1;
}

static int blf_search_available(void)
{
	int i;

	for (i = 0; i < BLF_MAX_FONT; i++)
		if (!global_font[i])
			return i;
	
	return -1;
}

static int blf_global_font_init(void)
{
	if (global_font_default == -1) {
		global_font_default = blf_search("default");
	}

	if (global_font_default == -1) {
		printf("Warning: Can't find default font!\n");
		return FALSE;
	}
	else {
		return TRUE;
	}
}

int BLF_load(const char *name)
{
	FontBLF *font;
	char *filename;
	int i;

	if (!name)
		return -1;

	/* check if we already load this font. */
	i = blf_search(name);
	if (i >= 0) {
		/*font = global_font[i];*/ /*UNUSED*/
		return i;
	}

	i = blf_search_available();
	if (i == -1) {
		printf("Too many fonts!!!\n");
		return -1;
	}

	filename = blf_dir_search(name);
	if (!filename) {
		printf("Can't find font: %s\n", name);
		return -1;
	}

	font = blf_font_new(name, filename);
	MEM_freeN(filename);

	if (!font) {
		printf("Can't load font: %s\n", name);
		return -1;
	}

	global_font[i] = font;
	return i;
}

int BLF_load_unique(const char *name)
{
	FontBLF *font;
	char *filename;
	int i;

	if (!name)
		return -1;

	/* Don't search in the cache!! make a new
	 * object font, this is for keep fonts threads safe.
	 */
	i = blf_search_available();
	if (i == -1) {
		printf("Too many fonts!!!\n");
		return -1;
	}

	filename = blf_dir_search(name);
	if (!filename) {
		printf("Can't find font: %s\n", name);
		return -1;
	}

	font = blf_font_new(name, filename);
	MEM_freeN(filename);

	if (!font) {
		printf("Can't load font: %s\n", name);
		return -1;
	}

	global_font[i] = font;
	return i;
}

void BLF_metrics_attach(int fontid, unsigned char *mem, int mem_size)
{
	FontBLF *font = blf_get(fontid);

	if (font) {
		blf_font_attach_from_mem(font, mem, mem_size);
	}
}

int BLF_load_mem(const char *name, const unsigned char *mem, int mem_size)
{
	FontBLF *font;
	int i;

	if (!name)
		return -1;

	i = blf_search(name);
	if (i >= 0) {
		/*font = global_font[i];*/ /*UNUSED*/
		return i;
	}

	i = blf_search_available();
	if (i == -1) {
		printf("Too many fonts!!!\n");
		return -1;
	}

	if (!mem || !mem_size) {
		printf("Can't load font: %s from memory!!\n", name);
		return -1;
	}

	font = blf_font_new_from_mem(name, mem, mem_size);
	if (!font) {
		printf("Can't load font: %s from memory!!\n", name);
		return -1;
	}

	global_font[i] = font;
	return i;
}

int BLF_load_mem_unique(const char *name, const unsigned char *mem, int mem_size)
{
	FontBLF *font;
	int i;

	if (!name)
		return -1;

	/*
	 * Don't search in the cache, make a new object font!
	 * this is to keep the font thread safe.
	 */
	i = blf_search_available();
	if (i == -1) {
		printf("Too many fonts!!!\n");
		return -1;
	}

	if (!mem || !mem_size) {
		printf("Can't load font: %s from memory!!\n", name);
		return -1;
	}

	font = blf_font_new_from_mem(name, mem, mem_size);
	if (!font) {
		printf("Can't load font: %s from memory!!\n", name);
		return -1;
	}

	global_font[i] = font;
	return i;
}

void BLF_unload(const char *name)
{
	FontBLF *font;
	int i;

	for (i = 0; i < BLF_MAX_FONT; i++) {
		font = global_font[i];

		if (font && (!strcmp(font->name, name))) {
			blf_font_free(font);
			global_font[i] = NULL;
		}
	}
}

void BLF_enable(int fontid, int option)
{
	FontBLF *font = blf_get(fontid);

	if (font) {
		font->flags |= option;
	}
}

void BLF_disable(int fontid, int option)
{
	FontBLF *font = blf_get(fontid);

	if (font) {
		font->flags &= ~option;
	}
}

void BLF_enable_default(int option)
{
	FontBLF *font = blf_get(global_font_default);

	if (font) {
		font->flags |= option;
	}
}

void BLF_disable_default(int option)
{
	FontBLF *font = blf_get(global_font_default);

	if (font) {
		font->flags &= ~option;
	}
}

void BLF_aspect(int fontid, float x, float y, float z)
{
	FontBLF *font = blf_get(fontid);

	if (font) {
		font->aspect[0] = x;
		font->aspect[1] = y;
		font->aspect[2] = z;
	}
}

void BLF_matrix(int fontid, const double m[16])
{
	FontBLF *font = blf_get(fontid);

	if (font) {
		memcpy(font->m, m, sizeof(font->m));
	}
}

void BLF_position(int fontid, float x, float y, float z)
{
	FontBLF *font = blf_get(fontid);

	if (font) {
		float xa, ya, za;
		float remainder;

		if (font->flags & BLF_ASPECT) {
			xa = font->aspect[0];
			ya = font->aspect[1];
			za = font->aspect[2];
		}
		else {
			xa = 1.0f;
			ya = 1.0f;
			za = 1.0f;
		}

		remainder = x - floorf(x);
		if (remainder > 0.4f && remainder < 0.6f) {
			if (remainder < 0.5f)
				x -= 0.1f * xa;
			else
				x += 0.1f * xa;
		}

		remainder = y - floorf(y);
		if (remainder > 0.4f && remainder < 0.6f) {
			if (remainder < 0.5f)
				y -= 0.1f * ya;
			else
				y += 0.1f * ya;
		}

		remainder = z - floorf(z);
		if (remainder > 0.4f && remainder < 0.6f) {
			if (remainder < 0.5f)
				z -= 0.1f * za;
			else
				z += 0.1f * za;
		}

		font->pos[0] = x;
		font->pos[1] = y;
		font->pos[2] = z;
	}
}

void BLF_size(int fontid, int size, int dpi)
{
	FontBLF *font = blf_get(fontid);

	if (font) {
		blf_font_size(font, size, dpi);
	}
}

void BLF_blur(int fontid, int size)
{
	FontBLF *font = blf_get(fontid);

	if (font) {
		font->blur = size;
	}
}

void BLF_draw_default_lock(void)
{
	if (blf_global_font_init()) {
		BLF_draw_lock(global_font_default);
	}
}

void BLF_draw_default_unlock(void)
{
	if (blf_global_font_init()) {
		BLF_draw_unlock(global_font_default);
	}
}

void BLF_draw_default(float x, float y, float z, const char *str, size_t len)
{
	if (str && blf_global_font_init()) {
		BLF_size(global_font_default, global_font_points, global_font_dpi);
		BLF_position(global_font_default, x, y, z);
		BLF_draw(global_font_default, str, len);
	}
}

/* same as above but call 'BLF_draw_ascii' */
void BLF_draw_default_ascii(float x, float y, float z, const char *str, size_t len)
{
	if (str && blf_global_font_init()) {
		BLF_position(global_font_default, x, y, z);
		BLF_draw(global_font_default, str, len);  /* XXX, use real length */
	}
}

void BLF_rotation_default(float angle)
{
	FontBLF *font = blf_get(global_font_default);

	if (font) {
		font->angle = angle;
	}
}

static void draw_lock(FontBLF *font)
{
	if (!font) {
		return;
	}

	if (font->locked == 0) {
		if (font->shadow || font->blur) {
			gpuImmediateFormat_T2_C4_V2(); // DOODLE: blurred and/or shadowed text
		}
		else {
			gpuImmediateFormat_T2_V2();    // DOODLE: normal text
		}

		/* one-time GL setup */
		glEnable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
	}

	font->locked++;
}

static void draw_unlock(FontBLF *font)
{
	if (!font) {
		return;
	}

	GPU_ASSERT(font->locked > 0);

	font->locked--;

	if (font->locked == 0) {
		glDisable(GL_BLEND);
		glDisable(GL_TEXTURE_2D);

		gpuImmediateUnformat();
	}
}

void BLF_draw_lock(int fontid)
{
	draw_lock(blf_get(fontid));
}

void BLF_draw_unlock(int fontid)
{
	draw_unlock(blf_get(fontid));
}

static void blf_draw__start(FontBLF *font)
{
	/* The pixmap alignment hack is handled
	   in BLF_position (old ui_rasterpos_safe). */

#if GPU_SAFETY
	{
	GLenum param;
	glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &param);
	GPU_ASSERT(param == GL_MODULATE);

	GPU_ASSERT(gpuGetMatrixMode() == GL_MODELVIEW);
	}
#endif

	gpuMatrixMode(GL_TEXTURE);
	gpuPushMatrix();
	gpuLoadIdentity();

	gpuMatrixMode(GL_MODELVIEW);
	gpuPushMatrix();


	if (font->flags & BLF_MATRIX) {
		gpuMultMatrixd((GLdouble *)&font->m);
	}

	gpuTranslate(font->pos[0], font->pos[1], font->pos[2]);

	if (font->flags & BLF_ASPECT)
		gpuScale(font->aspect[0], font->aspect[1], font->aspect[2]);

	if (font->flags & BLF_ROTATION)
		gpuRotateAxis(font->angle*M_PI/180.0f, 'Z');

	if (font->shadow || font->blur) 
		gpuGetCurrentColor4fv(font->orig_col);

	/* always bind the texture for the first glyph */
	font->tex_bind_state = -1;

	gpuMatrixCommit();

	draw_lock(font);
}

static void blf_draw__end(FontBLF *font)
{
	draw_unlock(font);

	gpuMatrixMode(GL_TEXTURE);
	gpuPopMatrix();

	gpuMatrixMode(GL_MODELVIEW);
	gpuPopMatrix();

	/* XXX: current color becomes undefined due to use of vertex arrays,
	        but a lot of code relies on it remaining the same */
	if (font->shadow || font->blur) 
		gpuCurrentColor4fv(font->orig_col);
}

void BLF_draw(int fontid, const char *str, size_t len)
{
	if (len > 0 && str[0]) {
		FontBLF *font = blf_get(fontid);

		if (font && font->glyph_cache) {
			blf_draw__start(font);
			blf_font_draw(font, str, len);
			blf_draw__end(font);
		}
	}
}

void BLF_draw_ascii(int fontid, const char *str, size_t len)
{
	if (len > 0 && str[0]) {
		FontBLF *font = blf_get(fontid);

		if (font && font->glyph_cache) {
			blf_draw__start(font);
			blf_font_draw_ascii(font, str, len);
			blf_draw__end(font);
		}
	}
}

void BLF_boundbox(int fontid, const char *str, rctf *box)
{
	FontBLF *font = blf_get(fontid);

	if (font) {
		blf_font_boundbox(font, str, box);
	}
}

void BLF_width_and_height(int fontid, const char *str, float *width, float *height)
{
	FontBLF *font = blf_get(fontid);

	if (font && font->glyph_cache) {
		blf_font_width_and_height(font, str, width, height);
	}
	else {
		*width = *height = 0.0f;
	}
}

void BLF_width_and_height_default(const char *str, float *width, float *height)
{
	if (!blf_global_font_init()) {
		*width = *height = 0.0f;
		return;
	}

	BLF_width_and_height(global_font_default, str, width, height);
}

float BLF_width(int fontid, const char *str)
{
	FontBLF *font = blf_get(fontid);

	if (font && font->glyph_cache) {
		return blf_font_width(font, str);
	}

	return 0.0f;
}

float BLF_fixed_width(int fontid)
{
	FontBLF *font = blf_get(fontid);

	if (font && font->glyph_cache) {
		return blf_font_fixed_width(font);
	}

	return 0.0f;
}

float BLF_width_default(const char *str)
{
	if (!blf_global_font_init())
		return 0.0f;

	BLF_size(global_font_default, global_font_points, global_font_dpi);
	return BLF_width(global_font_default, str);
}

float BLF_height(int fontid, const char *str)
{
	FontBLF *font = blf_get(fontid);

	if (font && font->glyph_cache) {
		return blf_font_height(font, str);
	}

	return 0.0f;
}

float BLF_height_max(int fontid)
{
	FontBLF *font = blf_get(fontid);

	if (font && font->glyph_cache) {
		return font->glyph_cache->max_glyph_height;
	}

	return 0.0f;
}

float BLF_width_max(int fontid)
{
	FontBLF *font = blf_get(fontid);

	if (font && font->glyph_cache) {
		return font->glyph_cache->max_glyph_width;
	}

	return 0.0f;
}

float BLF_descender(int fontid)
{
	FontBLF *font = blf_get(fontid);

	if (font && font->glyph_cache) {
		return font->glyph_cache->descender;
	}

	return 0.0f;
}

float BLF_ascender(int fontid)
{
	FontBLF *font = blf_get(fontid);

	if (font && font->glyph_cache) {
		return font->glyph_cache->ascender;
	}

	return 0.0f;
}

float BLF_height_default(const char *str)
{
	if (!blf_global_font_init())
		return 0.0f;

	BLF_size(global_font_default, global_font_points, global_font_dpi);

	return BLF_height(global_font_default, str);
}

void BLF_rotation(int fontid, float angle)
{
	FontBLF *font = blf_get(fontid);

	if (font) {
		font->angle = angle;
	}
}

void BLF_clipping(int fontid, float xmin, float ymin, float xmax, float ymax)
{
	FontBLF *font = blf_get(fontid);

	if (font) {
		font->clip_rec.xmin = xmin;
		font->clip_rec.ymin = ymin;
		font->clip_rec.xmax = xmax;
		font->clip_rec.ymax = ymax;
	}
}

void BLF_clipping_default(float xmin, float ymin, float xmax, float ymax)
{
	FontBLF *font = blf_get(global_font_default);

	if (font) {
		font->clip_rec.xmin = xmin;
		font->clip_rec.ymin = ymin;
		font->clip_rec.xmax = xmax;
		font->clip_rec.ymax = ymax;
	}
}

void BLF_shadow(int fontid, int level, float r, float g, float b, float a)
{
	FontBLF *font = blf_get(fontid);

	if (font) {
		font->shadow = level;
		font->shadow_col[0] = r;
		font->shadow_col[1] = g;
		font->shadow_col[2] = b;
		font->shadow_col[3] = a;
	}
}

void BLF_shadow_offset(int fontid, int x, int y)
{
	FontBLF *font = blf_get(fontid);

	if (font) {
		font->shadow_x = x;
		font->shadow_y = y;
	}
}

void BLF_buffer(int fontid, float *fbuf, unsigned char *cbuf, int w, int h, int nch, struct ColorManagedDisplay *display)
{
	FontBLF *font = blf_get(fontid);

	if (font) {
		font->buf_info.fbuf = fbuf;
		font->buf_info.cbuf = cbuf;
		font->buf_info.w = w;
		font->buf_info.h = h;
		font->buf_info.ch = nch;
		font->buf_info.display = display;
	}
}

void BLF_buffer_col(int fontid, float r, float g, float b, float a)
{
	FontBLF *font = blf_get(fontid);

	if (font) {
		font->buf_info.col[0] = r;
		font->buf_info.col[1] = g;
		font->buf_info.col[2] = b;
		font->buf_info.col[3] = a;
	}
}

void BLF_draw_buffer(int fontid, const char *str)
{
	FontBLF *font = blf_get(fontid);

	if (font && font->glyph_cache && (font->buf_info.fbuf || font->buf_info.cbuf)) {
		blf_font_buffer(font, str);
	}
}
