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
 * The Original Code is Copyright (C) 2012 Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Jason Wilkins.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/gpu/intern/gpu_immediate.h
 *  \ingroup gpu
 */

#ifndef __GPU_IMMEDIATE_H__
#define __GPU_IMMEDIATE_H__

#include "BLI_utildefines.h"

#include <GL/glew.h>

#include <assert.h>
#include <limits.h>
#include <stdlib.h>



/* Are restricted pointers available? (C99) */
#if (__STDC_VERSION__ < 199901L)
	/* Not a C99 compiler */
	#ifdef __GNUC__
		#define restrict __restrict__
	#else
		#define restrict /* restrict */
	#endif
#endif



#ifndef GPU_SAFETY
#define GPU_SAFETY 1
#endif

#if GPU_SAFETY

#define GPU_SAFE_RETURN(test) \
    assert(test);             \
    if (!(test)) {            \
        return;               \
    }                         \

#endif

#define GPU_CHECK_IMMEDIATE()       \
    GPU_SAFE_RETURN(GPU_IMMEDIATE);

#define GPU_CHECK_NO_BEGIN()                   \
    GPU_CHECK_IMMEDIATE();                     \
    GPU_SAFE_RETURN(!(GPU_IMMEDIATE->buffer));

#define GPU_CHECK_NO_LOCK()                         \
    GPU_CHECK_NO_BEGIN();                           \
    GPU_SAFE_RETURN(GPU_IMMEDIATE->lockCount == 0);



#ifdef __cplusplus
extern "C" {
#endif



void gpuImmediateElementSizes(
	GLint vertexSize,
	GLint normalSize,
	GLint colorSize);

void gpuImmediateMaxVertexCount(GLsizei maxVertexCount);

void gpuImmediateTextureUnitCount(size_t count);
void gpuImmediateTexCoordSizes(const GLint *restrict sizes);
void gpuImmediateTextureUnitMap(const GLenum *restrict map);

void gpuImmediateFloatAttribCount(size_t count);
void gpuImmediateFloatAttribSizes(const GLint *restrict sizes);
void gpuImmediateFloatAttribIndexMap(const GLuint *restrict map);

void gpuImmediateUbyteAttribCount(size_t count);
void gpuImmediateUbyteAttribSizes(const GLint *restrict sizes);
void gpuImmediateUbyteAttribIndexMap(const GLuint *restrict map);

void gpuImmediateLock(void);
void gpuImmediateUnlock(void);
GLint gpuImmediateLockCount(void);



#define GPU_MAX_ELEMENT_SIZE   4
#define GPU_MAX_TEXTURE_UNITS 32
#define GPU_MAX_FLOAT_ATTRIBS 32
#define GPU_MAX_UBYTE_ATTRIBS 32

typedef struct GPUimmediate {
	GLenum mode;

	/* All variables that determine the vertex array format
	   go in one structure so they can be easily cleared. */
	struct {
		GLint vertexSize;
		GLint normalSize;
		GLint texCoordSize[GPU_MAX_TEXTURE_UNITS];
		GLint colorSize;
		GLint attribSize_f[GPU_MAX_FLOAT_ATTRIBS];
		GLint attribSize_ub[GPU_MAX_UBYTE_ATTRIBS];

		GLenum textureUnitMap[GPU_MAX_TEXTURE_UNITS];
		size_t textureUnitCount;

		GLuint attribIndexMap_f[GPU_MAX_FLOAT_ATTRIBS];
		size_t attribCount_f;
		GLboolean attribNormalized_f[GPU_MAX_FLOAT_ATTRIBS];

		GLuint attribIndexMap_ub[GPU_MAX_UBYTE_ATTRIBS];
		size_t attribCount_ub;
		GLboolean attribNormalized_ub[GPU_MAX_UBYTE_ATTRIBS];
	} format;

	GLsizei maxVertexCount;

	GLenum lastTexture;

	GLfloat vertex[GPU_MAX_ELEMENT_SIZE];
	GLfloat normal[3];
	GLfloat texCoord[GPU_MAX_TEXTURE_UNITS][GPU_MAX_ELEMENT_SIZE];
	GLubyte color[4]; //-V112
	GLfloat attrib_f[GPU_MAX_FLOAT_ATTRIBS][GPU_MAX_ELEMENT_SIZE];
	GLubyte attrib_ub[GPU_MAX_UBYTE_ATTRIBS][4]; //-V112

	char *restrict buffer;
	void *restrict bufferData;
	size_t offset;
	GLsizei count;

	int lockCount;

	void (*lockBuffer)(void);
	void (*unlockBuffer)(void);
	void (*beginBuffer)(void);
	void (*endBuffer)(void);
	void (*shutdownBuffer)(struct GPUimmediate *restrict immediate);
} GPUimmediate;

extern GPUimmediate *restrict GPU_IMMEDIATE;



GPUimmediate *restrict gpuNewImmediate(void);
void gpuImmediateMakeCurrent(GPUimmediate *restrict  immediate);
void gpuDeleteImmediate(GPUimmediate *restrict  immediate);



#ifndef GPU_LEGACY_INTEROP
#define GPU_LEGACY_INTEROP 1
#endif

#if GPU_LEGACY_INTEROP
void gpu_legacy_get_state(void);
void gpu_legacy_put_state(void);
#else
#define gpu_legacy_get_state() ((void)0)
#define gpu_legacy_put_state() ((void)0)
#endif



#ifdef __cplusplus
}
#endif

#endif /* __GPU_IMMEDIATE_H_ */
