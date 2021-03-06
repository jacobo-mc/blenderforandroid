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
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file gameengine/Rasterizer/RAS_OpenGLRasterizer/RAS_OpenGLRasterizer.cpp
 *  \ingroup bgerastogl
 */

 
#include <math.h>
#include <stdlib.h>
 
 
#include "RAS_OpenGLRasterizer.h"

#include "RAS_Rect.h"
#include "RAS_TexVert.h"
#include "RAS_MeshObject.h"
#include "MT_CmMatrix4x4.h"
#include "RAS_IRenderTools.h" // rendering text

#include "RAS_StorageIM.h"
#include "RAS_StorageVA.h"
#include "RAS_StorageVBO.h"

#include "GPU_compatibility.h"
#include "GPU_draw.h"
#include "GPU_material.h"
#include "GPU_extensions.h"

#include "DNA_image_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_material_types.h"
#include "DNA_scene_types.h"

#include "BKE_DerivedMesh.h"

#ifndef M_PI
#define M_PI		3.14159265358979323846
#endif

/**
 *  32x32 bit masks for vinterlace stereo mode
 */
static GLuint left_eye_vinterlace_mask[32];
static GLuint right_eye_vinterlace_mask[32];

/**
 *  32x32 bit masks for hinterlace stereo mode.
 *  Left eye = &hinterlace_mask[0]
 *  Right eye = &hinterlace_mask[1]
 */
static GLuint hinterlace_mask[33];

RAS_OpenGLRasterizer::RAS_OpenGLRasterizer(RAS_ICanvas* canvas, int storage)
	:RAS_IRasterizer(canvas),
	m_2DCanvas(canvas),
	m_fogenabled(false),
	m_time(0.0),
	m_campos(0.0f, 0.0f, 0.0f),
	m_camortho(false),
	m_stereomode(RAS_STEREO_NOSTEREO),
	m_curreye(RAS_STEREO_LEFTEYE),
	m_eyeseparation(0.0),
	m_focallength(0.0),
	m_setfocallength(false),
	m_noOfScanlines(32),
	m_motionblur(0),
	m_motionblurvalue(-1.0),
	m_texco_num(0),
	m_attrib_num(0),
	//m_last_alphablend(GPU_BLEND_SOLID),
	m_last_frontface(true),
	m_materialCachingInfo(0),
	m_storage_type(storage)
{
	m_viewmatrix.setIdentity();
	m_viewinvmatrix.setIdentity();
	
	for (int i = 0; i < 32; i++)
	{
		left_eye_vinterlace_mask[i] = 0x55555555;
		right_eye_vinterlace_mask[i] = 0xAAAAAAAA;
		hinterlace_mask[i] = (i&1)*0xFFFFFFFF;
	}
	hinterlace_mask[32] = 0;

	m_prevafvalue = GPU_get_anisotropic();
	
	if (m_storage_type == RAS_VBO /*|| m_storage_type == RAS_AUTO_STORAGE && GLEW_ARB_vertex_buffer_object*/)
	{
		m_storage = new RAS_StorageVBO(&m_texco_num, m_texco, &m_attrib_num, m_attrib);
		m_failsafe_storage = new RAS_StorageIM(&m_texco_num, m_texco, &m_attrib_num, m_attrib);
		m_storage_type = RAS_VBO;
	}
	else if (m_storage_type == RAS_VA || m_storage_type == RAS_AUTO_STORAGE && GLEW_VERSION_1_1)
	{
		m_storage = new RAS_StorageVA(&m_texco_num, m_texco, &m_attrib_num, m_attrib);
		m_failsafe_storage = new RAS_StorageIM(&m_texco_num, m_texco, &m_attrib_num, m_attrib);
		m_storage_type = RAS_VA;
	}
	else
	{
		m_storage = m_failsafe_storage = new RAS_StorageIM(&m_texco_num, m_texco, &m_attrib_num, m_attrib);
		m_storage_type = RAS_IMMEDIATE;
	}
}



RAS_OpenGLRasterizer::~RAS_OpenGLRasterizer()
{
	// Restore the previous AF value
	GPU_set_anisotropic(m_prevafvalue);
	if (m_failsafe_storage && m_failsafe_storage != m_storage)
		delete m_failsafe_storage;

	if (m_storage)
		delete m_storage;
}

bool RAS_OpenGLRasterizer::Init()
{
	bool storage_init;
	GPU_state_init();

	m_ambr = 0.0f;
	m_ambg = 0.0f;
	m_ambb = 0.0f;
#include REAL_GL_MODE
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); /* set default blend function */

	//m_last_alphablend = GPU_BLEND_SOLID;
	GPU_set_material_alpha_blend(GPU_BLEND_SOLID);

	glFrontFace(GL_CCW);
	m_last_frontface = true;
#include FAKE_GL_MODE
	m_redback = 0.4375;
	m_greenback = 0.4375;
	m_blueback = 0.4375;
	m_alphaback = 0.0;

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	gpuSetClearColor(m_redback,m_greenback,m_blueback,m_alphaback);
	gpuClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


	glShadeModel(GL_SMOOTH);

	storage_init = m_storage->Init();

	return true && storage_init;
}


void RAS_OpenGLRasterizer::SetAmbientColor(float red, float green, float blue)
{
	m_ambr = red;
	m_ambg = green;
	m_ambb = blue;
}


void RAS_OpenGLRasterizer::SetAmbient(float factor)
{
	float ambient[] = { m_ambr*factor, m_ambg*factor, m_ambb*factor, 1.0f };
	gpuLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
}


void RAS_OpenGLRasterizer::SetBackColor(float red,
										float green,
										float blue,
										float alpha)
{
	m_redback = red;
	m_greenback = green;
	m_blueback = blue;
	m_alphaback = alpha;
}



void RAS_OpenGLRasterizer::SetFogColor(float r,
									   float g,
									   float b)
{
	m_fogr = r;
	m_fogg = g;
	m_fogb = b;
	m_fogenabled = true;
}



void RAS_OpenGLRasterizer::SetFogStart(float start)
{
	m_fogstart = start;
	m_fogenabled = true;
}



void RAS_OpenGLRasterizer::SetFogEnd(float fogend)
{
	m_fogdist = fogend;
	m_fogenabled = true;
}



void RAS_OpenGLRasterizer::SetFog(float start,
								  float dist,
								  float r,
								  float g,
								  float b)
{
	m_fogstart = start;
	m_fogdist = dist;
	m_fogr = r;
	m_fogg = g;
	m_fogb = b;
	m_fogenabled = true;
}



void RAS_OpenGLRasterizer::DisableFog()
{
	m_fogenabled = false;
}

bool RAS_OpenGLRasterizer::IsFogEnabled()
{
	return m_fogenabled;
}


void RAS_OpenGLRasterizer::DisplayFog()
{
	if ((m_drawingmode >= KX_SOLID) && m_fogenabled)
	{
		float params[5];
		glFogi(GL_FOG_MODE, GL_LINEAR);
		glFogf(GL_FOG_DENSITY, 0.1f);
		glFogf(GL_FOG_START, m_fogstart);
		glFogf(GL_FOG_END, m_fogstart + m_fogdist);
		params[0]= m_fogr;
		params[1]= m_fogg;
		params[2]= m_fogb;
		params[3]= 0.0;
		glFogfv(GL_FOG_COLOR, params); 
		glEnable(GL_FOG);
	} 
	else
	{
		glDisable(GL_FOG);
	}
}



bool RAS_OpenGLRasterizer::SetMaterial(const RAS_IPolyMaterial& mat)
{
	return mat.Activate(this, m_materialCachingInfo);
}



void RAS_OpenGLRasterizer::Exit()
{

	m_storage->Exit();
#include REAL_GL_MODE
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	/*glClearDepth(1.0); */
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	gpuSetClearColor(m_redback, m_greenback, m_blueback, m_alphaback);
	gpuClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDepthMask (GL_TRUE);
	glDepthFunc(GL_LEQUAL);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); /* reset blender default */
#include FAKE_GL_MODE
	glAlphaFunc(GL_GREATER, 0.5f); /* reset blender default */

	glDisable(GL_POLYGON_STIPPLE);
	
	gpuDisableLighting();
	if (GLEW_EXT_separate_specular_color || GLEW_VERSION_1_2)
		gpuLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SINGLE_COLOR);
	
	EndFrame();
}

bool RAS_OpenGLRasterizer::BeginFrame(int drawingmode, double time)
{
	m_time = time;
	SetDrawingMode(drawingmode);

	// Blender camera routine destroys the settings
#include REAL_GL_MODE
	if (m_drawingmode < KX_SOLID)
	{
		glDisable (GL_CULL_FACE);
		glDisable (GL_DEPTH_TEST);
	}
	else
	{
		glEnable(GL_DEPTH_TEST);
		glEnable (GL_CULL_FACE);
	}

	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);

	//m_last_alphablend = GPU_BLEND_SOLID;
	GPU_set_material_alpha_blend(GPU_BLEND_SOLID);

	glFrontFace(GL_CCW);
	m_last_frontface = true;
#include FAKE_GL_MODE
	glShadeModel(GL_SMOOTH);

	glEnable(GL_MULTISAMPLE_ARB);

	m_2DCanvas->BeginFrame();
	
	return true;
}



void RAS_OpenGLRasterizer::SetDrawingMode(int drawingmode)
{
#include REAL_GL_MODE
	m_drawingmode = drawingmode;

	if (m_drawingmode == KX_WIREFRAME)
		glDisable(GL_CULL_FACE);

	m_storage->SetDrawingMode(drawingmode);
#include FAKE_GL_MODE
}

int RAS_OpenGLRasterizer::GetDrawingMode()
{
	return m_drawingmode;
}


void RAS_OpenGLRasterizer::SetDepthMask(DepthMask depthmask)
{
	glDepthMask(depthmask == KX_DEPTHMASK_DISABLED ? GL_FALSE : GL_TRUE);
}


void RAS_OpenGLRasterizer::ClearColorBuffer()
{
	m_2DCanvas->ClearColor(m_redback,m_greenback,m_blueback,m_alphaback);
	m_2DCanvas->ClearBuffer(RAS_ICanvas::COLOR_BUFFER);
}


void RAS_OpenGLRasterizer::ClearDepthBuffer()
{
	m_2DCanvas->ClearBuffer(RAS_ICanvas::DEPTH_BUFFER);
}


void RAS_OpenGLRasterizer::ClearCachingInfo(void)
{
	m_materialCachingInfo = 0;
}

void RAS_OpenGLRasterizer::FlushDebugShapes()
{
	if (!m_debugShapes.size())
		return;

	// DrawDebugLines
	GLboolean light, tex;

	light= gpuIsLightingEnabled();
	tex= glIsEnabled(GL_TEXTURE_2D);

	if (light) gpuDisableLighting();
	if (tex) glDisable(GL_TEXTURE_2D);

	//draw lines
	gpuBegin(GL_LINES);
	for (unsigned int i=0;i<m_debugShapes.size();i++)
	{
		if (m_debugShapes[i].m_type != OglDebugShape::LINE)
			continue;
		gpuColor4f(m_debugShapes[i].m_color[0],m_debugShapes[i].m_color[1],m_debugShapes[i].m_color[2],1.f);
		const MT_Scalar* fromPtr = &m_debugShapes[i].m_pos.x();
		const MT_Scalar* toPtr= &m_debugShapes[i].m_param.x();
		gpuVertex3dv(fromPtr);
		gpuVertex3dv(toPtr);
	}
	gpuEnd();

	//draw circles
	for (unsigned int i=0;i<m_debugShapes.size();i++)
	{
		if (m_debugShapes[i].m_type != OglDebugShape::CIRCLE)
			continue;
		gpuBegin(GL_LINE_LOOP);
		gpuColor4f(m_debugShapes[i].m_color[0],m_debugShapes[i].m_color[1],m_debugShapes[i].m_color[2],1.f);

		static const MT_Vector3 worldUp(0.0, 0.0, 1.0);
		MT_Vector3 norm = m_debugShapes[i].m_param;
		MT_Matrix3x3 tr;
		if (norm.fuzzyZero() || norm == worldUp)
		{
			tr.setIdentity();
		}
		else
		{
			MT_Vector3 xaxis, yaxis;
			xaxis = MT_cross(norm, worldUp);
			yaxis = MT_cross(xaxis, norm);
			tr.setValue(xaxis.x(), xaxis.y(), xaxis.z(),
				yaxis.x(), yaxis.y(), yaxis.z(),
				norm.x(), norm.y(), norm.z());
		}
		MT_Scalar rad = m_debugShapes[i].m_param2.x();
		int n = (int) m_debugShapes[i].m_param2.y();
		for (int j = 0; j<n; j++)
		{
			MT_Scalar theta = j*M_PI*2/n;
			MT_Vector3 pos(cos(theta) * rad, sin(theta) * rad, 0.0);
			pos = pos*tr;
			pos += m_debugShapes[i].m_pos;
			const MT_Scalar* posPtr = &pos.x();
			gpuVertex3dv(posPtr);
		}
		gpuEnd();
	}

	if (light) gpuEnableLighting();
	if (tex) glEnable(GL_TEXTURE_2D);

	m_debugShapes.clear();
}

void RAS_OpenGLRasterizer::EndFrame()
{
	FlushDebugShapes();

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	glDisable(GL_MULTISAMPLE_ARB);

	m_2DCanvas->EndFrame();
}

void RAS_OpenGLRasterizer::SetRenderArea()
{
	RAS_Rect area;
	// only above/below stereo method needs viewport adjustment
	switch (m_stereomode)
	{
		case RAS_STEREO_ABOVEBELOW:
			switch(m_curreye)
			{
				case RAS_STEREO_LEFTEYE:
					// upper half of window
					area.SetLeft(0);
					area.SetBottom(m_2DCanvas->GetHeight() -
						int(m_2DCanvas->GetHeight() - m_noOfScanlines) / 2);
	
					area.SetRight(int(m_2DCanvas->GetWidth()));
					area.SetTop(int(m_2DCanvas->GetHeight()));
					m_2DCanvas->SetDisplayArea(&area);
					break;
				case RAS_STEREO_RIGHTEYE:
					// lower half of window
					area.SetLeft(0);
					area.SetBottom(0);
					area.SetRight(int(m_2DCanvas->GetWidth()));
					area.SetTop(int(m_2DCanvas->GetHeight() - m_noOfScanlines) / 2);
					m_2DCanvas->SetDisplayArea(&area);
					break;
			}
			break;
		case RAS_STEREO_SIDEBYSIDE:
			switch (m_curreye)
			{
				case RAS_STEREO_LEFTEYE:
					// Left half of window
					area.SetLeft(0);
					area.SetBottom(0);
					area.SetRight(m_2DCanvas->GetWidth()/2);
					area.SetTop(m_2DCanvas->GetHeight());
					m_2DCanvas->SetDisplayArea(&area);
					break;
				case RAS_STEREO_RIGHTEYE:
					// Right half of window
					area.SetLeft(m_2DCanvas->GetWidth()/2);
					area.SetBottom(0);
					area.SetRight(m_2DCanvas->GetWidth());
					area.SetTop(m_2DCanvas->GetHeight());
					m_2DCanvas->SetDisplayArea(&area);
					break;
			}
			break;
		default:
			// every available pixel
			area.SetLeft(0);
			area.SetBottom(0);
			area.SetRight(int(m_2DCanvas->GetWidth()));
			area.SetTop(int(m_2DCanvas->GetHeight()));
			m_2DCanvas->SetDisplayArea(&area);
			break;
	}
}
	
void RAS_OpenGLRasterizer::SetStereoMode(const StereoMode stereomode)
{
	m_stereomode = stereomode;
}

RAS_IRasterizer::StereoMode RAS_OpenGLRasterizer::GetStereoMode()
{
	return m_stereomode;
}

bool RAS_OpenGLRasterizer::Stereo()
{
	if (m_stereomode > RAS_STEREO_NOSTEREO) // > 0
		return true;
	else
		return false;
}

bool RAS_OpenGLRasterizer::InterlacedStereo()
{
	return m_stereomode == RAS_STEREO_VINTERLACE || m_stereomode == RAS_STEREO_INTERLACED;
}

void RAS_OpenGLRasterizer::SetEye(const StereoEye eye)
{
	m_curreye = eye;
	switch (m_stereomode)
	{
		case RAS_STEREO_QUADBUFFERED:
			glDrawBuffer(m_curreye == RAS_STEREO_LEFTEYE ? GL_BACK_LEFT : GL_BACK_RIGHT);
			break;
		case RAS_STEREO_ANAGLYPH:
			if (m_curreye == RAS_STEREO_LEFTEYE) {
				glColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_FALSE);
			}
			else {
				//glAccum(GL_LOAD, 1.0);
				glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE);
				ClearDepthBuffer();
			}
			break;
		case RAS_STEREO_VINTERLACE:
		{
			glEnable(GL_POLYGON_STIPPLE);
			glPolygonStipple((const GLubyte*) ((m_curreye == RAS_STEREO_LEFTEYE) ? left_eye_vinterlace_mask : right_eye_vinterlace_mask));
			if (m_curreye == RAS_STEREO_RIGHTEYE)
				ClearDepthBuffer();
			break;
		}
		case RAS_STEREO_INTERLACED:
		{
			glEnable(GL_POLYGON_STIPPLE);
			glPolygonStipple((const GLubyte*) &hinterlace_mask[m_curreye == RAS_STEREO_LEFTEYE?0:1]);
			if (m_curreye == RAS_STEREO_RIGHTEYE)
				ClearDepthBuffer();
			break;
		}
		default:
			break;
	}
}

RAS_IRasterizer::StereoEye RAS_OpenGLRasterizer::GetEye()
{
	return m_curreye;
}


void RAS_OpenGLRasterizer::SetEyeSeparation(const float eyeseparation)
{
	m_eyeseparation = eyeseparation;
}

float RAS_OpenGLRasterizer::GetEyeSeparation()
{
	return m_eyeseparation;
}

void RAS_OpenGLRasterizer::SetFocalLength(const float focallength)
{
	m_focallength = focallength;
	m_setfocallength = true;
}

float RAS_OpenGLRasterizer::GetFocalLength()
{
	return m_focallength;
}


void RAS_OpenGLRasterizer::SwapBuffers()
{
	m_2DCanvas->SwapBuffers();
}



const MT_Matrix4x4& RAS_OpenGLRasterizer::GetViewMatrix() const
{
	return m_viewmatrix;
}

const MT_Matrix4x4& RAS_OpenGLRasterizer::GetViewInvMatrix() const
{
	return m_viewinvmatrix;
}

void RAS_OpenGLRasterizer::IndexPrimitives_3DText(RAS_MeshSlot& ms,
									class RAS_IPolyMaterial* polymat,
									class RAS_IRenderTools* rendertools)
{ 
	bool obcolor = ms.m_bObjectColor;
	MT_Vector4& rgba = ms.m_RGBAcolor;
	RAS_MeshSlot::iterator it;

	// handle object color
	if (obcolor) {
		glDisableClientState(GL_COLOR_ARRAY);
		gpuCurrentColor4d(rgba[0], rgba[1], rgba[2], rgba[3]);
	}
	else
		glEnableClientState(GL_COLOR_ARRAY);

	for (ms.begin(it); !ms.end(it); ms.next(it)) {
		RAS_TexVert *vertex;
		size_t i, j, numvert;
		
		numvert = it.array->m_type;

		if (it.array->m_type == RAS_DisplayArray::LINE) {
			// line drawing, no text
			gpuBegin(GL_LINES);

			for (i=0; i<it.totindex; i+=2)
			{
				vertex = &it.vertex[it.index[i]];
				gpuVertex3fv(vertex->getXYZ());

				vertex = &it.vertex[it.index[i+1]];
				gpuVertex3fv(vertex->getXYZ());
			}

			gpuEnd();
		}
		else {
			// triangle and quad text drawing
			for (i=0; i<it.totindex; i+=numvert)
			{
				float v[4][3];
				int glattrib, unit;

				for (j=0; j<numvert; j++) {
					vertex = &it.vertex[it.index[i+j]];

					v[j][0] = vertex->getXYZ()[0];
					v[j][1] = vertex->getXYZ()[1];
					v[j][2] = vertex->getXYZ()[2];
				}

				// find the right opengl attribute
				glattrib = -1;
				if (GPU_EXT_GLSL_VERTEX_ENABLED)
					for (unit=0; unit<m_attrib_num; unit++)
						if (m_attrib[unit] == RAS_TEXCO_UV)
							glattrib = unit;
				
				rendertools->RenderText(polymat->GetDrawingMode(), polymat,
					v[0], v[1], v[2], (numvert == 4)? v[3]: NULL, glattrib);

				ClearCachingInfo();
			}
		}
	}

	glDisableClientState(GL_COLOR_ARRAY);
}

void RAS_OpenGLRasterizer::SetTexCoordNum(int num)
{
	m_texco_num = num;
	if (m_texco_num > RAS_MAX_TEXCO)
		m_texco_num = RAS_MAX_TEXCO;
}

void RAS_OpenGLRasterizer::SetAttribNum(int num)
{
	m_attrib_num = num;
	if (m_attrib_num > RAS_MAX_ATTRIB)
		m_attrib_num = RAS_MAX_ATTRIB;
}

void RAS_OpenGLRasterizer::SetTexCoord(TexCoGen coords, int unit)
{
	// this changes from material to material
	if (unit < RAS_MAX_TEXCO)
		m_texco[unit] = coords;
}

void RAS_OpenGLRasterizer::SetAttrib(TexCoGen coords, int unit)
{
	// this changes from material to material
	if (unit < RAS_MAX_ATTRIB)
		m_attrib[unit] = coords;
}

void RAS_OpenGLRasterizer::IndexPrimitives(RAS_MeshSlot& ms)
{
	gpuMatrixCommit();
	if (ms.m_pDerivedMesh)
		m_failsafe_storage->IndexPrimitives(ms);
	else
		m_storage->IndexPrimitives(ms);
}

void RAS_OpenGLRasterizer::IndexPrimitivesMulti(RAS_MeshSlot& ms)
{
	gpuMatrixCommit();
	if (ms.m_pDerivedMesh)
		m_failsafe_storage->IndexPrimitivesMulti(ms);
	else
		m_storage->IndexPrimitivesMulti(ms);
}

void RAS_OpenGLRasterizer::SetProjectionMatrix(MT_CmMatrix4x4 &mat)
{
	gpuMatrixMode(GL_PROJECTION);

	float matrix[16];
	double *matrixd;
	/*test me*/
	matrixd = mat.getPointer();

	for(int i=0;i<16;i++)
		matrix[i]=matrixd[i];

	gpuLoadMatrix(matrix);
	gpuMatrixCommit();

	m_camortho = (mat(3, 3) != 0.0);

	gpuMatrixMode(GL_MODELVIEW);
}

void RAS_OpenGLRasterizer::SetProjectionMatrix(const MT_Matrix4x4 & mat)
{
	gpuMatrixMode(GL_PROJECTION);

	float matrix[16];
	/* Get into argument. Looks a bit dodgy, but it's ok. */
	mat.getValue(matrix);
	/* Internally, MT_Matrix4x4 uses doubles (MT_Scalar). */
	gpuLoadMatrix(matrix);
	gpuMatrixCommit();

	m_camortho= (mat[3][3] != 0.0);

	gpuMatrixMode(GL_MODELVIEW);
}

MT_Matrix4x4 RAS_OpenGLRasterizer::GetFrustumMatrix(
	float left,
	float right,
	float bottom,
	float top,
	float frustnear,
	float frustfar,
	float focallength,
	bool 
) {
	MT_Matrix4x4 result;

	// correction for stereo
	if (Stereo())
	{
			float near_div_focallength;
			float offset;

			// if Rasterizer.setFocalLength is not called we use the camera focallength
			if (!m_setfocallength)
				// if focallength is null we use a value known to be reasonable
				m_focallength = (focallength == 0.f) ? m_eyeseparation * 30.0f
					: focallength;

			near_div_focallength = frustnear / m_focallength;
			offset = 0.5f * m_eyeseparation * near_div_focallength;
			switch(m_curreye)
			{
				case RAS_STEREO_LEFTEYE:
						left += offset;
						right += offset;
						break;
				case RAS_STEREO_RIGHTEYE:
						left -= offset;
						right -= offset;
						break;
			}
			// leave bottom and top untouched
	}

	float tm [16];
	mat4_frustum_set(reinterpret_cast<float (*)[4]>(tm), left, right, bottom, top, frustnear, frustfar);
	result.setValue(tm);

	return result;
}

MT_Matrix4x4 RAS_OpenGLRasterizer::GetOrthoMatrix(
	float left,
	float right,
	float bottom,
	float top,
	float frustnear,
	float frustfar
) {
	MT_Matrix4x4 result;

	float tm [16];
	mat4_ortho_set(reinterpret_cast<float (*)[4]>(tm), left, right, bottom, top, frustnear, frustfar);
	result.setValue(tm);

	return result;
}


// next arguments probably contain redundant info, for later...
void RAS_OpenGLRasterizer::SetViewMatrix(const MT_Matrix4x4 &mat, 
										 const MT_Matrix3x3 & camOrientMat3x3,
										 const MT_Point3 & pos,
										 bool perspective)
{
	m_viewmatrix = mat;

	// correction for stereo
	if (Stereo() && perspective)
	{
		MT_Vector3 unitViewDir(0.0, -1.0, 0.0);  // minus y direction, Blender convention
		MT_Vector3 unitViewupVec(0.0, 0.0, 1.0);
		MT_Vector3 viewDir, viewupVec;
		MT_Vector3 eyeline;

		// actual viewDir
		viewDir = camOrientMat3x3 * unitViewDir;  // this is the moto convention, vector on right hand side
		// actual viewup vec
		viewupVec = camOrientMat3x3 * unitViewupVec;

		// vector between eyes
		eyeline = viewDir.cross(viewupVec);

		switch(m_curreye)
		{
			case RAS_STEREO_LEFTEYE:
				{
				// translate to left by half the eye distance
				MT_Transform transform;
				transform.setIdentity();
				transform.translate(-(eyeline * m_eyeseparation / 2.0));
				m_viewmatrix *= transform;
				}
				break;
			case RAS_STEREO_RIGHTEYE:
				{
				// translate to right by half the eye distance
				MT_Transform transform;
				transform.setIdentity();
				transform.translate(eyeline * m_eyeseparation / 2.0);
				m_viewmatrix *= transform;
				}
				break;
		}
	}

	m_viewinvmatrix = m_viewmatrix;
	m_viewinvmatrix.invert();

	// note: getValue gives back column major as needed by OpenGL
	MT_Scalar glviewmat[16];
	m_viewmatrix.getValue(glviewmat);
	float tm[16];
	for(int i=0;i<16;i++)tm[i]=glviewmat[i];

	//gpuMatrixMode(GL_MODELVIEW);




	//gpuMatrixMode(GL_PROJECTION);
	//gpuGetMatrix(GL_PROJECTION_MATRIX, pm);

	gpuMatrixMode(GL_MODELVIEW);
	//gpuLoadMatrix(pm);
	gpuLoadMatrix(tm);
	gpuMatrixCommit();
	//gpuMultMatrix(tm);

	m_campos = pos;
}


const MT_Point3& RAS_OpenGLRasterizer::GetCameraPosition()
{
	return m_campos;
}

bool RAS_OpenGLRasterizer::GetCameraOrtho()
{
	return m_camortho;
}

void RAS_OpenGLRasterizer::SetCullFace(bool enable)
{
#include REAL_GL_MODE
	if (enable)
		glEnable(GL_CULL_FACE);
	else
		glDisable(GL_CULL_FACE);
#include FAKE_GL_MODE
}

void RAS_OpenGLRasterizer::SetLines(bool enable)
{
	if (enable)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	else
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void RAS_OpenGLRasterizer::SetSpecularity(float specX,
										  float specY,
										  float specZ,
										  float specval)
{
	GLfloat mat_specular[] = {specX, specY, specZ, specval};
	gpuMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mat_specular);
}



void RAS_OpenGLRasterizer::SetShinyness(float shiny)
{
	GLfloat mat_shininess[] = {	shiny };
	gpuMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, mat_shininess);
}



void RAS_OpenGLRasterizer::SetDiffuse(float difX,float difY,float difZ,float diffuse)
{
	GLfloat mat_diffuse [] = {difX, difY,difZ, diffuse};
	gpuMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mat_diffuse);
}

void RAS_OpenGLRasterizer::SetEmissive(float eX, float eY, float eZ, float e)
{
	GLfloat mat_emit [] = {eX,eY,eZ,e};
	gpuMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, mat_emit);
}


double RAS_OpenGLRasterizer::GetTime()
{
	return m_time;
}

void RAS_OpenGLRasterizer::SetPolygonOffset(float mult, float add)
{
	glPolygonOffset(mult, add);
	GLint mode = GL_POLYGON_OFFSET_FILL;
	if (m_drawingmode < KX_SHADED)
		mode = GL_POLYGON_OFFSET_LINE;
	if (mult != 0.0f || add != 0.0f)
		glEnable(mode);
	else
		glDisable(mode);
}

void RAS_OpenGLRasterizer::EnableMotionBlur(float motionblurvalue)
{
	/* don't just set m_motionblur to 1, but check if it is 0 so
	 * we don't reset a motion blur that is already enabled */
	if (m_motionblur == 0)
		m_motionblur = 1;
	m_motionblurvalue = motionblurvalue;
}

void RAS_OpenGLRasterizer::DisableMotionBlur()
{
	m_motionblur = 0;
	m_motionblurvalue = -1.0;
}

void RAS_OpenGLRasterizer::SetAlphaBlend(int alphablend)
{
	GPU_set_material_alpha_blend(alphablend);
}
#include REAL_GL_MODE
void RAS_OpenGLRasterizer::SetFrontFace(bool ccw)
{
	if (m_last_frontface == ccw)
		return;

	if (ccw)
		glFrontFace(GL_CCW);
	else
		glFrontFace(GL_CW);
	
	m_last_frontface = ccw;
}

void RAS_OpenGLRasterizer::SetAnisotropicFiltering(short level)
{
	GPU_set_anisotropic((float)level);
}

short RAS_OpenGLRasterizer::GetAnisotropicFiltering()
{
	return (short)GPU_get_anisotropic();
}
