#include "gpu_immediate.h"

#include <assert.h>

#include "MEM_guardedalloc.h"

#include "BLI_math_matrix.h"
#include "BLI_math_rotation.h"
#include "BLI_math_vector.h"

#define GPU_MAT_CAST_ANY
#include "GPU_matrix.h"


#ifdef GLES
#include <GLES2/gl2.h>
#include "gpu_object_gles.h"
#else 
#include <GL/glew.h>
#endif

#if WITH_GPU_SAFETY
#define GPU_STACK_DEBUG
#endif

GLint glslneedupdate = 1;

typedef GLfloat GPU_matrix[4][4];

typedef struct GPU_matrix_stack
{
	GLint size;
	GLint pos;
	GLint changed;
	GPU_matrix * dynstack;


} GPU_matrix_stack;

GPU_matrix_stack ms_modelview;
GPU_matrix_stack ms_projection;
GPU_matrix_stack ms_texture;

GPU_matrix_stack * ms_current;

void GPU_matrix_forced_update(void)
{

	glslneedupdate = 1;
		gpuMatrixCommit();
	glslneedupdate = 1;	
	
}

#define CURMATRIX (ms_current->dynstack[ms_current->pos])


/* Check if we have a good matrix */
#if WITH_GPU_SAFETY
static void checkmat(GLfloat *m)
{
	GLint i;
	for(i=0;i<16;i++) {
#if _MSC_VER
		BLI_assert(_finite(m[i]));
#else
		BLI_assert(!isinf(m[i]));
#endif
	}
}

#define CHECKMAT checkmat((GLfloat*)CURMATRIX);
#else
#define CHECKMAT
#endif


static void ms_init(GPU_matrix_stack * ms, GLint initsize)
{
	if(initsize == 0)
		initsize = 32;
	ms->size = initsize;
	ms->pos = 0;
	ms->changed = 1;
	ms->dynstack = MEM_mallocN(ms->size*sizeof(*(ms->dynstack)), "MatrixStack");
	//gpuLoadIdentity();
}

static void ms_free(GPU_matrix_stack * ms)
{
	ms->size = 0;
	ms->pos = 0;
	MEM_freeN(ms->dynstack);
	ms->dynstack = NULL;
}


static GLint glstackpos[3] = {0};
static GLint glstackmode;

void GPU_ms_init(void)
{
	ms_init(&ms_modelview, 32);
	ms_init(&ms_projection, 16);
	ms_init(&ms_texture, 16);

	ms_current = &ms_modelview;

	printf("Stack init\n");



}

void GPU_ms_exit(void)
{
	ms_free(&ms_modelview);
	ms_free(&ms_projection);
	ms_free(&ms_texture);

	printf("Stack exit\n");



}

void gpuMatrixLock(void)
{
#ifndef GLES
	GPU_matrix tm;
	glGetIntegerv(GL_MODELVIEW_STACK_DEPTH, glstackpos);
	glGetIntegerv(GL_PROJECTION_STACK_DEPTH, glstackpos+1);
	glGetIntegerv(GL_TEXTURE_STACK_DEPTH, glstackpos+2);
	glGetIntegerv(GL_MATRIX_MODE, &glstackmode);

	glGetFloatv(GL_MODELVIEW_MATRIX, (GLfloat*)tm);
	gpuMatrixMode(GL_MODELVIEW);
	gpuLoadMatrix((GLfloat*)tm);

	glGetFloatv(GL_PROJECTION_MATRIX, (GLfloat*)tm);
	gpuMatrixMode(GL_PROJECTION);
	gpuLoadMatrix((GLfloat*)tm);

	glGetFloatv(GL_TEXTURE_MATRIX, (GLfloat*)tm);
	gpuMatrixMode(GL_TEXTURE);
	gpuLoadMatrix((GLfloat*)tm);




	glMatrixMode(GL_TEXTURE);
	glPushMatrix();
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();

	glMatrixMode(glstackmode);
	switch(glstackmode)
	{
		case GL_MODELVIEW:
			gpuMatrixMode(GL_MODELVIEW);
			break;
		case GL_TEXTURE:
			gpuMatrixMode(GL_TEXTURE);
			break;
		case GL_PROJECTION:
			gpuMatrixMode(GL_PROJECTION);
			break;

	}


#endif

}


void gpuMatrixUnlock(void)
{

#ifndef GLES
	GLint curval;


	glMatrixMode(GL_TEXTURE);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	glGetIntegerv(GL_MODELVIEW_STACK_DEPTH, &curval);


	glGetIntegerv(GL_PROJECTION_STACK_DEPTH, &curval);
	glGetIntegerv(GL_TEXTURE_STACK_DEPTH, &curval);






	glMatrixMode(glstackmode);

#endif

}

void gpuMatrixCommit(void)
{
#ifndef GLES
	if(ms_modelview.changed)
	{
		ms_modelview.changed = 0;
		glMatrixMode(GL_MODELVIEW);
		glLoadMatrixf((GLfloat*)ms_modelview.dynstack[ms_modelview.pos]);
	}
	if(ms_projection.changed)
	{
		ms_projection.changed = 0;
		glMatrixMode(GL_PROJECTION);
		glLoadMatrixf((GLfloat*)ms_projection.dynstack[ms_projection.pos]);
	}
	if(ms_texture.changed)
	{
		ms_texture.changed = 0;
		glMatrixMode(GL_TEXTURE);
		glLoadMatrixf((GLfloat*)ms_texture.dynstack[ms_texture.pos]);
	}

#else
if(curglslesi)
{
#include REAL_GL_MODE
	if(ms_modelview.changed || glslneedupdate)
	{
	
		GLfloat t[3][3] = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
		copy_m3_m4(t, ms_modelview.dynstack[ms_modelview.pos]); 
		if(curglslesi->viewmatloc!=-1)
			glUniformMatrix4fv(curglslesi->viewmatloc, 1, 0, ms_modelview.dynstack[ms_modelview.pos]);
		if(curglslesi->normalmatloc!=-1)
			glUniformMatrix3fv(curglslesi->normalmatloc, 1, 0, t);
		
		
	}
	if(ms_projection.changed|| glslneedupdate)
	{
		if(curglslesi->projectionmatloc!=-1)
		glUniformMatrix4fv(curglslesi->projectionmatloc, 1, 0, ms_projection.dynstack[ms_projection.pos]);
	}
	
	
#include FAKE_GL_MODE
}


#endif
CHECKMAT
}




void gpuPushMatrix(void)
{
	ms_current->pos++;
	
	if(ms_current->pos >= ms_current->size)
	{
		ms_current->size += ((ms_current->size-1)>>1)+1; 
		/* increases size by 50% */
		ms_current->dynstack = MEM_reallocN(ms_current->dynstack,
											ms_current->size*sizeof(*(ms_current->dynstack)));
											
	
	}

	gpuLoadMatrix((GLfloat*)ms_current->dynstack[ms_current->pos-1]);
	CHECKMAT

}

void gpuPopMatrix(void)
{
	ms_current->pos--;




	ms_current->changed = 1;


#ifdef GPU_STACK_DEBUG
	if(ms_current->pos < 0)
		assert(0);
#endif	
	CHECKMAT
}


void gpuMatrixMode(GLenum mode)
{
	GPU_ASSERT(ELEM3(mode, GL_MODELVIEW, GL_PROJECTION, GL_TEXTURE));

	switch(mode)
	{
		case GL_MODELVIEW:
			ms_current = &ms_modelview;
			break;
		case GL_PROJECTION:
			ms_current = &ms_projection;
			break;
		case GL_TEXTURE:
			ms_current = & ms_texture;
			break;
		default:
			/* ignore */
			break;
	}

CHECKMAT
}


void gpuLoadMatrix(const GLfloat * m)
{
	copy_m4_m4((GLfloat (*)[4])CURMATRIX, (GLfloat (*)[4])m);
	ms_current->changed = 1;
	CHECKMAT
}

GLfloat * gpuGetMatrix(GLfloat * m)
{
	if(m)
		copy_m4_m4((GLfloat (*)[4])m,CURMATRIX);
	else
		return (GLfloat*)(CURMATRIX);
	ms_current->changed = 1;
	return 0;
}


void gpuLoadIdentity(void)
{
	unit_m4(CURMATRIX);
	ms_current->changed = 1;
	CHECKMAT
}




void gpuTranslate(GLfloat x, GLfloat y, GLfloat z)
{

	translate_m4(CURMATRIX, x, y, z);
	ms_current->changed = 1;
	CHECKMAT

}

void gpuScale(GLfloat x, GLfloat y, GLfloat z)
{

	scale_m4(CURMATRIX, x, y, z);
	ms_current->changed = 1;
	CHECKMAT
}


void gpuMultMatrix(const GLfloat *m)
{
	GPU_matrix cm;

	copy_m4_m4((GLfloat (*)[4])cm, (GLfloat (*)[4])CURMATRIX);

	mult_m4_m4m4_q(CURMATRIX, cm, (GLfloat (*)[4])m);
	ms_current->changed = 1;
	CHECKMAT

}


void gpuMultMatrixd(const double *m)
{
	GLfloat mf[16];
	GLint i;
	for(i=0; i<16; i++)
		mf[i] = m[i];
	gpuMultMatrix(mf);

}


void gpuRotateAxis(GLfloat angle, char axis)
{

	rotate_m4((GLfloat (*)[4])CURMATRIX, axis, angle*M_PI/180.0f);
	ms_current->changed = 1;
}

void gpuLoadOrtho(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat nearVal, GLfloat farVal)
{
	mat4_ortho_set(CURMATRIX, left, right, bottom, top, nearVal, farVal);
	ms_current->changed = 1;
	CHECKMAT
}


void gpuOrtho(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat nearVal, GLfloat farVal)
{
	GPU_matrix om;

	mat4_ortho_set(om, left, right, bottom, top, nearVal, farVal);

	gpuMultMatrix((GLfloat*)om);
	CHECKMAT
}


void gpuFrustum(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat nearVal, GLfloat farVal)
{
	GPU_matrix fm;
	mat4_frustum_set(fm, left, right, bottom, top, nearVal, farVal);
	gpuMultMatrix((GLfloat*) fm);
	CHECKMAT
}

void gpuLoadFrustum(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat nearVal, GLfloat farVal)
{
	mat4_frustum_set(CURMATRIX, left, right, bottom, top, nearVal, farVal);
	ms_current->changed = 1;
	CHECKMAT
}


void gpuLookAt(GLfloat eyeX, GLfloat eyeY, GLfloat eyeZ, GLfloat centerX, GLfloat centerY, GLfloat centerZ, GLfloat upX, GLfloat upY, GLfloat upZ)
{

	GPU_matrix cm;
	GLfloat lookdir[3];
	GLfloat camup[3] = {upX, upY, upZ};

	lookdir[0] =  centerX - eyeX;
	lookdir[1] =  centerY - eyeY;
	lookdir[2] =  centerZ - eyeZ;

	mat4_look_from_origin(cm, lookdir, camup);

	gpuMultMatrix((GLfloat*) cm);

	gpuTranslate(-eyeX, -eyeY, -eyeZ);
	CHECKMAT

}
