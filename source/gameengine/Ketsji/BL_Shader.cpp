/** \file gameengine/Ketsji/BL_Shader.cpp
 *  \ingroup ketsji
 */

#ifdef GLES
#include <GLES2/gl2.h>
#endif



#include "GPU_functions.h"

#include <iostream>
#include "BL_Shader.h"
#include "BL_Material.h"

#include <GL/glew.h>

#include "MT_assert.h"
#include "MT_Matrix4x4.h"
#include "MT_Matrix3x3.h"
#include "KX_PyMath.h"
#include "MEM_guardedalloc.h"

#include "RAS_GLExtensionManager.h"
#include "RAS_MeshObject.h"
#include "RAS_IRasterizer.h"

#include "GPU_extensions.h"

#define spit(x) std::cout << x << std::endl;

#define SORT_UNIFORMS 1
#define UNIFORM_MAX_LEN (int)sizeof(float)*16
#define MAX_LOG_LEN 262144 // bounds

BL_Uniform::BL_Uniform(int data_size)
:	mLoc(-1),
	mDirty(true),
	mType(UNI_NONE),
	mTranspose(0),
	mDataLen(data_size)
{
#ifdef SORT_UNIFORMS
	MT_assert((int)mDataLen <= UNIFORM_MAX_LEN);
	mData = (void*)MEM_mallocN(mDataLen, "shader-uniform-alloc");
#endif
}

BL_Uniform::~BL_Uniform()
{
#ifdef SORT_UNIFORMS
	if (mData) {
		MEM_freeN(mData);
		mData=0;
	}
#endif
}

void BL_Uniform::Apply(class BL_Shader *shader)
{
#ifdef SORT_UNIFORMS
	MT_assert(mType > UNI_NONE && mType < UNI_MAX && mData);

	if (!mDirty) 
		return;

	switch(mType)
	{
	case UNI_FLOAT: {
			float *f = (float*)mData;
			gpuUniform1f(mLoc,(GLfloat)*f);
		}break;
	case UNI_INT: {
			int *f = (int*)mData;
			gpuUniform1i(mLoc, (GLint)*f);
		}break;
	case UNI_FLOAT2: {
			float *f = (float*)mData;
			gpuUniform2fv(mLoc,1, (GLfloat*)f);
		}break;
	case UNI_FLOAT3: {
			float *f = (float*)mData;
			gpuUniform3fv(mLoc,1,(GLfloat*)f);
		}break;
	case UNI_FLOAT4: {
			float *f = (float*)mData;
			gpuUniform4fv(mLoc,1,(GLfloat*)f);
		}break;
	case UNI_INT2: {
			int *f = (int*)mData;
			gpuUniform2iv(mLoc,1,(GLint*)f);
		}break; 
	case UNI_INT3: {
			int *f = (int*)mData;
			gpuUniform3iv(mLoc,1,(GLint*)f);
		}break; 
	case UNI_INT4: {
			int *f = (int*)mData;
			gpuUniform4iv(mLoc,1,(GLint*)f);
		}break;
	case UNI_MAT4: {
			float *f = (float*)mData;
			gpuUniformMatrix4fv(mLoc, 1, mTranspose?GL_TRUE:GL_FALSE,(GLfloat*)f);
		}break;
	case UNI_MAT3: {
			float *f = (float*)mData;
			gpuUniformMatrix3fv(mLoc, 1, mTranspose?GL_TRUE:GL_FALSE,(GLfloat*)f);
		}break;
	}
	mDirty = false;
#endif
}

void BL_Uniform::SetData(int location, int type,bool transpose)
{
#ifdef SORT_UNIFORMS
	mType	= type;
	mLoc	= location;
	mDirty	= true;
#endif
}

bool BL_Shader::Ok()const
{
	return (mShader !=0 && mOk && mUse);
}

BL_Shader::BL_Shader()
:	PyObjectPlus(),
	mShader(0),
	mPass(1),
	mOk(0),
	mUse(0),
	mAttr(0),
	vertProg(""),
	fragProg(""),
	mError(0),
	mDirty(true)
{
	// if !GLEW_ARB_shader_objects this class will not be used
	//for (int i=0; i<MAXTEX; i++) {
	//	mSampler[i] = BL_Sampler();
	//}
}

BL_Shader::~BL_Shader()
{
	//for (int i=0; i<MAXTEX; i++) {
	//	if (mSampler[i].mOwn) {
	//		if (mSampler[i].mTexture)
	//			mSampler[i].mTexture->DeleteTex();
	//	}
	//}
	ClearUniforms();

	if ( mShader ) {
		gpuDeleteProgram(mShader);
		mShader = 0;
	}
	vertProg	= 0;
	fragProg	= 0;
	mOk			= 0;
	gpuUseProgram(0);
}

void BL_Shader::ClearUniforms()
{
	BL_UniformVec::iterator it = mUniforms.begin();
	while(it != mUniforms.end()) {
		delete (*it);
		it++;
	}
	mUniforms.clear();

	
	BL_UniformVecDef::iterator itp = mPreDef.begin();
	while(itp != mPreDef.end()) {
		delete (*itp);
		itp++;
	}
	mPreDef.clear();

}


BL_Uniform  *BL_Shader::FindUniform(const int location)
{
#ifdef SORT_UNIFORMS
	BL_UniformVec::iterator it = mUniforms.begin();
	while(it != mUniforms.end()) {
		if ((*it)->GetLocation() == location)
			return (*it);
		it++;
	}
#endif
	return 0;
}

void BL_Shader::SetUniformfv(int location, int type, float *param,int size, bool transpose)
{
#ifdef SORT_UNIFORMS
	BL_Uniform *uni= FindUniform(location);
	if (uni) {
		memcpy(uni->getData(), param, size);
		uni->SetData(location, type, transpose);
	}
	else {
		uni = new BL_Uniform(size);
		memcpy(uni->getData(), param, size);

		uni->SetData(location, type, transpose);
		mUniforms.push_back(uni);
	}
	mDirty = true;
#endif
}

void BL_Shader::SetUniformiv(int location, int type, int *param,int size, bool transpose)
{
#ifdef SORT_UNIFORMS
	BL_Uniform *uni= FindUniform(location);
	if (uni) {
		memcpy(uni->getData(), param, size);
		uni->SetData(location, type, transpose);
	}
	else {
		uni = new BL_Uniform(size);
		memcpy(uni->getData(), param, size);
		uni->SetData(location, type, transpose);
		mUniforms.push_back(uni);
	}
	mDirty = true;
#endif
}


void BL_Shader::ApplyShader()
{
#ifdef SORT_UNIFORMS
	if (!mDirty) 
		return;

	for (unsigned int i=0; i<mUniforms.size(); i++)
		mUniforms[i]->Apply(this);

	mDirty = false;
#endif
}

void BL_Shader::UnloadShader()
{
	//
}


bool BL_Shader::LinkProgram()
{
	int vertlen = 0, fraglen=0, proglen=0;
	int vertstatus=0, fragstatus=0, progstatus=0;
	unsigned int tmpVert=0, tmpFrag=0, tmpProg=0;
	int char_len=0;
	char *logInf =0;
	const char *src[2];

	if (mError)
		goto programError;

	if (!vertProg || !fragProg) {
		spit("Invalid GLSL sources");
		return false;
	}
	if ( !GPU_EXT_GLSL_FRAGMENT_ENABLED) {
		spit("Fragment shaders not supported");
		return false;
	}
	if ( !GPU_EXT_GLSL_VERTEX_ENABLED) {
		spit("Vertex shaders not supported");
		return false;
	}
	
	// -- vertex shader ------------------
#ifdef GLES	
	src[0] = ""
	"#define gl_ModelViewMatrix b_ModelViewMatrix\n"
	"#define gl_ProjectionMatrix b_ProjectionMatrix\n"
	"#define gl_NormalMatrix b_NormalMatrix\n"
	
	"#define gl_Vertex b_Vertex\n"
	"#define gl_Normal b_Normal\n"
	
	"uniform mat4 b_ProjectionMatrix ;	\n"
	"uniform mat4 b_ModelViewMatrix ;	\n"
	"uniform mat3 b_NormalMatrix ;	\n"
	
	"attribute vec4 b_Vertex;	\n"
	"attribute vec3 b_Normal;	\n"
		
	"vec4 ftransform()\n"
	"{\n"
	"return gl_ProjectionMatrix * gl_ModelViewMatrix * gl_Vertex;"
	"}\n"
	;
#else
	src[0] = "";
#endif
	
	src[1] = vertProg;
	
	tmpVert = gpuCreateShader(GL_VERTEX_SHADER);
	gpuShaderSource(tmpVert, 2, (const char**)src, 0);
	gpuCompileShader(tmpVert);
	gpuGetShaderiv(tmpVert, GL_INFO_LOG_LENGTH,(GLint*) &vertlen);
	
	// print info if any
	if ( vertlen > 0 && vertlen < MAX_LOG_LEN) {
		logInf = (char*)MEM_mallocN(vertlen, "vert-log");
		gpuGetShaderInfoLog(tmpVert, vertlen, (GLsizei*)&char_len, logInf);
		if (char_len >0) {
			spit("---- Vertex Shader Error ----");
			spit(logInf);
		}
		MEM_freeN(logInf);
		logInf=0;
	}
	// check for compile errors
	gpuGetShaderiv(tmpVert, GL_COMPILE_STATUS,(GLint*)&vertstatus);
	if (!vertstatus) {
		spit("---- Vertex shader failed to compile ----");
		goto programError;
	}

	// -- fragment shader ----------------
	tmpFrag = gpuCreateShader(GL_FRAGMENT_SHADER);
	gpuShaderSource(tmpFrag, 1,(const char**)&fragProg, 0);
	gpuCompileShader(tmpFrag);
	gpuGetShaderiv(tmpFrag, GL_INFO_LOG_LENGTH, (GLint*) &fraglen);
	if (fraglen >0 && fraglen < MAX_LOG_LEN) {
		logInf = (char*)MEM_mallocN(fraglen, "frag-log");
		gpuGetShaderInfoLog(tmpFrag, fraglen,(GLsizei*) &char_len, logInf);
		if (char_len >0) {
			spit("---- Fragment Shader Error ----");
			spit(logInf);
		}
		MEM_freeN(logInf);
		logInf=0;
	}

	gpuGetShaderiv(tmpFrag, GL_COMPILE_STATUS, (GLint*) &fragstatus);
	if (!fragstatus) {
		spit("---- Fragment shader failed to compile ----");
		goto programError;
	}

	
	// -- program ------------------------
	//  set compiled vert/frag shader & link
	tmpProg = gpuCreateProgram();
	gpuAttachShader(tmpProg, tmpVert);
	gpuAttachShader(tmpProg, tmpFrag);
	gpuLinkProgram(tmpProg);
	gpuGetProgramiv(tmpProg, GL_INFO_LOG_LENGTH, (GLint*) &proglen);
	gpuGetProgramiv(tmpProg, GL_LINK_STATUS, (GLint*) &progstatus);
	

	if (proglen > 0 && proglen < MAX_LOG_LEN) {
		logInf = (char*)MEM_mallocN(proglen, "prog-log");
		gpuGetProgramInfoLog(tmpProg, proglen, (GLsizei*)&char_len, logInf);
		if (char_len >0) {
			spit("---- GLSL Program ----");
			spit(logInf);
		}
		MEM_freeN(logInf);
		logInf=0;
	}

	if (!progstatus) {
		spit("---- GLSL program failed to link ----");
		goto programError;
	}

#ifdef GLES
	gpu_assign_gles_loc(&glslesloc, tmpProg);
#endif
	// set
	mShader = tmpProg;
	gpuDeleteShader(tmpVert);
	gpuDeleteShader(tmpFrag);
	mOk		= 1;
	mError = 0;
	return true;

programError:
	if (tmpVert) {
		gpuDeleteShader(tmpVert);
		tmpVert=0;
	}
	if (tmpFrag) {
		gpuDeleteShader(tmpFrag);
		tmpFrag=0;
	}

	if (tmpProg) {
		gpuDeleteProgram(tmpProg);
		tmpProg=0;
	}

	mOk		= 0;
	mUse	= 0;
	mError	= 1;
	return false;
}

const char *BL_Shader::GetVertPtr()
{
	return vertProg?vertProg:0;
}

const char *BL_Shader::GetFragPtr()
{
	return fragProg?fragProg:0;
}

void BL_Shader::SetVertPtr( char *vert )
{
	vertProg = vert;
}

void BL_Shader::SetFragPtr( char *frag )
{
	fragProg = frag;
}

unsigned int BL_Shader::GetProg()
{ 
	return mShader;
}
//
//const BL_Sampler* BL_Shader::GetSampler(int i)
//{
//	MT_assert(i<=MAXTEX);
//	return &mSampler[i];
//}

void BL_Shader::SetSampler(int loc, int unit)
{
	if ( GPU_EXT_GLSL_FRAGMENT_ENABLED &&
		GPU_EXT_GLSL_VERTEX_ENABLED &&
		GPU_EXT_GLSL_ENABLED 
		)
	{
		glUniform1iARB(loc, unit);
	}
}
//
//void BL_Shader::InitializeSampler(int unit, BL_Texture* texture)
//{
//	MT_assert(unit<=MAXTEX);
//	mSampler[unit].mTexture = texture;
//	mSampler[unit].mLoc =-1;
//	mSampler[unit].mOwn = 0;
//}

void BL_Shader::SetProg(bool enable)
{
	if ( GPU_EXT_GLSL_FRAGMENT_ENABLED &&
		GPU_EXT_GLSL_VERTEX_ENABLED &&
		GPU_EXT_GLSL_ENABLED 
		)
	{
		if (	mShader != 0 && mOk && enable) {
			gpuUseProgram(mShader);
#ifdef GLES
			gpu_set_shader_es(&glslesloc, 1);
#endif
		}
		else {
			gpuUseProgram(0);
#ifdef GLES
			gpu_set_shader_es(0, 0);
#endif			
		}
	}
}

void BL_Shader::Update( const RAS_MeshSlot & ms, RAS_IRasterizer* rasty )
{
	if (!Ok() || !mPreDef.size()) 
		return;

	if ( GPU_EXT_GLSL_FRAGMENT_ENABLED &&
		GPU_EXT_GLSL_VERTEX_ENABLED &&
		GPU_EXT_GLSL_ENABLED 
		)
	{
		MT_Matrix4x4 model;
		model.setValue(ms.m_OpenGLMatrix);
		const MT_Matrix4x4& view = rasty->GetViewMatrix();

		if (mAttr==SHD_TANGENT)
			ms.m_mesh->SetMeshModified(true);

		BL_UniformVecDef::iterator it;
		for (it = mPreDef.begin(); it!= mPreDef.end(); it++)
		{
			BL_DefUniform *uni = (*it);
			if (uni->mLoc == -1) continue;

			switch (uni->mType)
			{
				case MODELMATRIX:
					{
						SetUniform(uni->mLoc, model);
						break;
					}
				case MODELMATRIX_TRANSPOSE:
					{
						SetUniform(uni->mLoc, model, true);
						break;
					}
				case MODELMATRIX_INVERSE:
					{
						model.invert();
						SetUniform(uni->mLoc, model);
						break;
					}
				case MODELMATRIX_INVERSETRANSPOSE:
					{
						model.invert();
						SetUniform(uni->mLoc, model, true);
						break;
					}
				case MODELVIEWMATRIX:
					{
						SetUniform(uni->mLoc, view*model);
						break;
					}

				case MODELVIEWMATRIX_TRANSPOSE:
					{
						MT_Matrix4x4 mat(view*model);
						SetUniform(uni->mLoc, mat, true);
						break;
					}
				case MODELVIEWMATRIX_INVERSE:
					{
						MT_Matrix4x4 mat(view*model);
						mat.invert();
						SetUniform(uni->mLoc, mat);
						break;
					}
				case MODELVIEWMATRIX_INVERSETRANSPOSE:
					{
						MT_Matrix4x4 mat(view*model);
						mat.invert();
						SetUniform(uni->mLoc, mat, true);
						break;
					}
				case CAM_POS:
					{
						MT_Point3 pos(rasty->GetCameraPosition());
						SetUniform(uni->mLoc, pos);
						break;
					}
				case VIEWMATRIX:
					{
						SetUniform(uni->mLoc, view);
						break;
					}
				case VIEWMATRIX_TRANSPOSE:
					{
						SetUniform(uni->mLoc, view, true);
						break;
					}
				case VIEWMATRIX_INVERSE:
					{
						MT_Matrix4x4 viewinv = view;
						viewinv.invert();
						SetUniform(uni->mLoc, view);
						break;
					}
				case VIEWMATRIX_INVERSETRANSPOSE:
					{
						MT_Matrix4x4 viewinv = view;
						viewinv.invert();
						SetUniform(uni->mLoc, view, true);
						break;
					}
				case CONSTANT_TIMER:
					{
						SetUniform(uni->mLoc, (float)rasty->GetTime());
						break;
					}
				default:
					break;
			}
		}
	}
}


int BL_Shader::GetAttribLocation(const STR_String& name)
{
	if ( GPU_EXT_GLSL_FRAGMENT_ENABLED &&
		GPU_EXT_GLSL_VERTEX_ENABLED &&
		GPU_EXT_GLSL_ENABLED 
		)
	{
		return glGetAttribLocationARB(mShader, name.ReadPtr());
	}

	return -1;
}

void BL_Shader::BindAttribute(const STR_String& attr, int loc)
{
	if ( GPU_EXT_GLSL_FRAGMENT_ENABLED &&
		GPU_EXT_GLSL_VERTEX_ENABLED &&
		GPU_EXT_GLSL_ENABLED 
		)
	{
		glBindAttribLocationARB(mShader, loc, attr.ReadPtr());
	}
}

int BL_Shader::GetUniformLocation(const STR_String& name)
{
	if ( GPU_EXT_GLSL_FRAGMENT_ENABLED &&
		GPU_EXT_GLSL_VERTEX_ENABLED &&
		GPU_EXT_GLSL_ENABLED 
		)
	{
		MT_assert(mShader!=0);
		int location = glGetUniformLocationARB(mShader, name.ReadPtr());
		if (location == -1)
			spit("Invalid uniform value: " << name.ReadPtr() << ".");
		return location;
	}

	return -1;
}

void BL_Shader::SetUniform(int uniform, const MT_Tuple2& vec)
{
	if ( GPU_EXT_GLSL_FRAGMENT_ENABLED &&
		GPU_EXT_GLSL_VERTEX_ENABLED &&
		GPU_EXT_GLSL_ENABLED 
		)
	{
		float value[2];
		vec.getValue(value);
		gpuUniform2fv(uniform, 1, value);
	}

}

void BL_Shader::SetUniform(int uniform, const MT_Tuple3& vec)
{
	if ( GPU_EXT_GLSL_FRAGMENT_ENABLED &&
		GPU_EXT_GLSL_VERTEX_ENABLED &&
		GPU_EXT_GLSL_ENABLED 
		)
	{	
		float value[3];
		vec.getValue(value);
		gpuUniform3fv(uniform, 1, value);
	}
}

void BL_Shader::SetUniform(int uniform, const MT_Tuple4& vec)
{
	if ( GPU_EXT_GLSL_FRAGMENT_ENABLED &&
		GPU_EXT_GLSL_VERTEX_ENABLED &&
		GPU_EXT_GLSL_ENABLED 
		)
	{
		float value[4];
		vec.getValue(value);
		gpuUniform4fv(uniform, 1, value);
	}
}

void BL_Shader::SetUniform(int uniform, const unsigned int& val)
{
	if ( GPU_EXT_GLSL_FRAGMENT_ENABLED &&
		GPU_EXT_GLSL_VERTEX_ENABLED &&
		GPU_EXT_GLSL_ENABLED 
		)
	{
		gpuUniform1i(uniform, val);
	}
}

void BL_Shader::SetUniform(int uniform, const int val)
{
	if ( GPU_EXT_GLSL_FRAGMENT_ENABLED &&
		GPU_EXT_GLSL_VERTEX_ENABLED &&
		GPU_EXT_GLSL_ENABLED 
		)
	{
		gpuUniform1i(uniform, val);
	}
}

void BL_Shader::SetUniform(int uniform, const float& val)
{
	if ( GPU_EXT_GLSL_FRAGMENT_ENABLED &&
		GPU_EXT_GLSL_VERTEX_ENABLED &&
		GPU_EXT_GLSL_ENABLED 
		)
	{
		gpuUniform1f(uniform, val);
	}
}

void BL_Shader::SetUniform(int uniform, const MT_Matrix4x4& vec, bool transpose)
{
	if ( GPU_EXT_GLSL_FRAGMENT_ENABLED &&
		GPU_EXT_GLSL_VERTEX_ENABLED &&
		GPU_EXT_GLSL_ENABLED 
		)
	{
		float value[16];
		// note: getValue gives back column major as needed by OpenGL
		vec.getValue(value);
		gpuUniformMatrix4fv(uniform, 1, transpose?GL_TRUE:GL_FALSE, value);
	}
}

void BL_Shader::SetUniform(int uniform, const MT_Matrix3x3& vec, bool transpose)
{
	if ( GPU_EXT_GLSL_FRAGMENT_ENABLED &&
		GPU_EXT_GLSL_VERTEX_ENABLED &&
		GPU_EXT_GLSL_ENABLED 
		)
	{
		float value[9];
		value[0] = (float)vec[0][0]; value[1] = (float)vec[1][0]; value[2] = (float)vec[2][0]; 
		value[3] = (float)vec[0][1]; value[4] = (float)vec[1][1]; value[5] = (float)vec[2][1]; 
		value[6] = (float)vec[0][2]; value[7] = (float)vec[1][2]; value[8] = (float)vec[2][2];
		gpuUniformMatrix3fv(uniform, 1, transpose?GL_TRUE:GL_FALSE, value);
	}
}

void BL_Shader::SetUniform(int uniform, const float* val, int len)
{
	if ( GPU_EXT_GLSL_FRAGMENT_ENABLED &&
		GPU_EXT_GLSL_VERTEX_ENABLED &&
		GPU_EXT_GLSL_ENABLED 
		)
	{
		if (len == 2) 
			gpuUniform2fv(uniform, 1,(GLfloat*)val);
		else if (len == 3)
			gpuUniform3fv(uniform, 1,(GLfloat*)val);
		else if (len == 4)
			gpuUniform4fv(uniform, 1,(GLfloat*)val);
		else
			MT_assert(0);
	}
}

void BL_Shader::SetUniform(int uniform, const int* val, int len)
{
	if ( GPU_EXT_GLSL_FRAGMENT_ENABLED &&
		GPU_EXT_GLSL_VERTEX_ENABLED &&
		GPU_EXT_GLSL_ENABLED 
		)
	{
		if (len == 2) 
			gpuUniform2iv(uniform, 1, (GLint*)val);
		else if (len == 3)
			gpuUniform3iv(uniform, 1, (GLint*)val);
		else if (len == 4)
			gpuUniform4iv(uniform, 1, (GLint*)val);
		else
			MT_assert(0);
	}
}

#ifdef WITH_PYTHON

PyMethodDef BL_Shader::Methods[] = 
{
	// creation
	KX_PYMETHODTABLE( BL_Shader, setSource ),
	KX_PYMETHODTABLE( BL_Shader, delSource ),
	KX_PYMETHODTABLE( BL_Shader, getVertexProg ),
	KX_PYMETHODTABLE( BL_Shader, getFragmentProg ),
	KX_PYMETHODTABLE( BL_Shader, setNumberOfPasses ),
	KX_PYMETHODTABLE( BL_Shader, validate),
	/// access functions
	KX_PYMETHODTABLE( BL_Shader, isValid),
	KX_PYMETHODTABLE( BL_Shader, setUniform1f ),
	KX_PYMETHODTABLE( BL_Shader, setUniform2f ),
	KX_PYMETHODTABLE( BL_Shader, setUniform3f ),
	KX_PYMETHODTABLE( BL_Shader, setUniform4f ),
	KX_PYMETHODTABLE( BL_Shader, setUniform1i ),
	KX_PYMETHODTABLE( BL_Shader, setUniform2i ),
	KX_PYMETHODTABLE( BL_Shader, setUniform3i ),
	KX_PYMETHODTABLE( BL_Shader, setUniform4i ),
	KX_PYMETHODTABLE( BL_Shader, setAttrib ),

	KX_PYMETHODTABLE( BL_Shader, setUniformfv ),
	KX_PYMETHODTABLE( BL_Shader, setUniformiv ),
	KX_PYMETHODTABLE( BL_Shader, setUniformDef ),

	KX_PYMETHODTABLE( BL_Shader, setSampler  ),
	KX_PYMETHODTABLE( BL_Shader, setUniformMatrix4 ),
	KX_PYMETHODTABLE( BL_Shader, setUniformMatrix3 ),

	{NULL,NULL} //Sentinel
};

PyAttributeDef BL_Shader::Attributes[] = {
	{ NULL }	//Sentinel
};

PyTypeObject BL_Shader::Type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"BL_Shader",
	sizeof(PyObjectPlus_Proxy),
	0,
	py_base_dealloc,
	0,
	0,
	0,
	0,
	py_base_repr,
	0,0,0,0,0,0,0,0,0,
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	0,0,0,0,0,0,0,
	Methods,
	0,
	0,
	&PyObjectPlus::Type,
	0,0,0,0,0,0,
	py_base_new
};

KX_PYMETHODDEF_DOC( BL_Shader, setSource," setSource(vertexProgram, fragmentProgram)" )
{
	if (mShader !=0 && mOk  )
	{
		// already set...
		Py_RETURN_NONE;
	}
	char *v,*f;
	int apply=0;
	if ( PyArg_ParseTuple(args, "ssi:setSource", &v, &f, &apply) )
	{
		vertProg = v;
		fragProg = f;
		if ( LinkProgram() ) {
			gpuUseProgram( mShader );
			mUse = apply!=0;
			Py_RETURN_NONE;
		}
		vertProg = 0;
		fragProg = 0;
		mUse = 0;
		Py_RETURN_NONE;
	}
	return NULL;
}


KX_PYMETHODDEF_DOC( BL_Shader, delSource, "delSource( )" )
{
	ClearUniforms();
	gpuUseProgram(0);

	gpuDeleteProgram(mShader);
	mShader		= 0;
	mOk			= 0;
	mUse		= 0;
	Py_RETURN_NONE;
}

KX_PYMETHODDEF_DOC( BL_Shader, isValid, "isValid()" )
{
	return PyLong_FromSsize_t( ( mShader !=0 &&  mOk ) );
}

KX_PYMETHODDEF_DOC( BL_Shader, getVertexProg, "getVertexProg( )" )
{
	return PyUnicode_FromString(vertProg?vertProg:"");
}

KX_PYMETHODDEF_DOC( BL_Shader, getFragmentProg, "getFragmentProg( )" )
{
	return PyUnicode_FromString(fragProg?fragProg:"");
}

KX_PYMETHODDEF_DOC( BL_Shader, validate, "validate()")
{
	if (mError) {
		Py_RETURN_NONE;
	}
	if (mShader==0) {
		PyErr_SetString(PyExc_TypeError, "shader.validate(): BL_Shader, invalid shader object");
		return NULL;
	}
	int stat = 0;
	glValidateProgramARB(mShader);
	glGetObjectParameterivARB(mShader, GL_OBJECT_VALIDATE_STATUS_ARB,(GLint*) &stat);


	if (stat > 0 && stat < MAX_LOG_LEN) {
		int char_len=0;
		char *logInf = (char*)MEM_mallocN(stat, "validate-log");

		glGetInfoLogARB(mShader, stat,(GLsizei*) &char_len, logInf);
		if (char_len >0) {
			spit("---- GLSL Validation ----");
			spit(logInf);
		}
		MEM_freeN(logInf);
		logInf=0;
	}
	Py_RETURN_NONE;
}


KX_PYMETHODDEF_DOC( BL_Shader, setSampler, "setSampler(name, index)" )
{
	if (mError) {
		Py_RETURN_NONE;
	}

	const char *uniform="";
	int index=-1;
	if (PyArg_ParseTuple(args, "si:setSampler", &uniform, &index)) 
	{
		int loc = GetUniformLocation(uniform);
		if (loc != -1) {
			if (index >= MAXTEX || index < 0)
				spit("Invalid texture sample index: " << index);

#ifdef SORT_UNIFORMS
			SetUniformiv(loc, BL_Uniform::UNI_INT, &index, (sizeof(int)) );
#else
			SetUniform(loc, index);
#endif
			//if (index <= MAXTEX)
			//	mSampler[index].mLoc = loc;
			//else
			//	spit("Invalid texture sample index: " << index);
		}
		Py_RETURN_NONE;
	}
	return NULL;
}

KX_PYMETHODDEF_DOC( BL_Shader, setNumberOfPasses, "setNumberOfPasses( max-pass )" )
{
	int pass = 1;
	if (!PyArg_ParseTuple(args, "i:setNumberOfPasses", &pass))
		return NULL;

	mPass = 1;
	Py_RETURN_NONE;
}

/// access functions
KX_PYMETHODDEF_DOC( BL_Shader, setUniform1f, "setUniform1f(name, fx)" )
{
	if (mError) {
		Py_RETURN_NONE;
	}

	const char *uniform="";
	float value=0;
	if (PyArg_ParseTuple(args, "sf:setUniform1f", &uniform, &value ))
	{
		int loc = GetUniformLocation(uniform);
		if (loc != -1)
		{
#ifdef SORT_UNIFORMS
			SetUniformfv(loc, BL_Uniform::UNI_FLOAT, &value, sizeof(float));
#else			
			SetUniform( loc, (float)value );
#endif
		}
		Py_RETURN_NONE;
	}
	return NULL;
}


KX_PYMETHODDEF_DOC( BL_Shader, setUniform2f, "setUniform2f(name, fx, fy)")
{
	if (mError) {
		Py_RETURN_NONE;
	}
	const char *uniform="";
	float array[2]={ 0,0 };
	if (PyArg_ParseTuple(args, "sff:setUniform2f", &uniform, &array[0],&array[1] ))
	{
		int loc = GetUniformLocation(uniform);
		if (loc != -1)
		{
#ifdef SORT_UNIFORMS
			SetUniformfv(loc, BL_Uniform::UNI_FLOAT2, array, (sizeof(float)*2) );
#else
			SetUniform(loc, array, 2);
#endif
		}
		Py_RETURN_NONE;
	}
	return NULL;
}


KX_PYMETHODDEF_DOC( BL_Shader, setUniform3f, "setUniform3f(name, fx,fy,fz) ")
{
	if (mError) {
		Py_RETURN_NONE;
	}
	const char *uniform="";
	float array[3]={0,0,0};
	if (PyArg_ParseTuple(args, "sfff:setUniform3f", &uniform, &array[0],&array[1],&array[2]))
	{
		int loc = GetUniformLocation(uniform);
		if (loc != -1)
		{
#ifdef SORT_UNIFORMS
			SetUniformfv(loc, BL_Uniform::UNI_FLOAT3, array, (sizeof(float)*3) );
#else
			SetUniform(loc, array, 3);
#endif
		}
		Py_RETURN_NONE;

	}
	return NULL;
}


KX_PYMETHODDEF_DOC( BL_Shader, setUniform4f, "setUniform4f(name, fx,fy,fz, fw) ")
{
	if (mError) {
		Py_RETURN_NONE;
	}
	const char *uniform="";
	float array[4]={0,0,0,0};
	if (PyArg_ParseTuple(args, "sffff:setUniform4f", &uniform, &array[0],&array[1],&array[2], &array[3]))
	{
		int loc = GetUniformLocation(uniform);
		if (loc != -1)
		{
#ifdef SORT_UNIFORMS
			SetUniformfv(loc, BL_Uniform::UNI_FLOAT4, array, (sizeof(float)*4) );
#else
			SetUniform(loc, array, 4);
#endif
		}
		Py_RETURN_NONE;
	}
	return NULL;
}


KX_PYMETHODDEF_DOC( BL_Shader, setUniform1i, "setUniform1i(name, ix)" )
{
	if (mError) {
		Py_RETURN_NONE;
	}
	const char *uniform="";
	int value=0;
	if (PyArg_ParseTuple(args, "si:setUniform1i", &uniform, &value ))
	{
		int loc = GetUniformLocation(uniform);
		if (loc != -1)
		{
#ifdef SORT_UNIFORMS
			SetUniformiv(loc, BL_Uniform::UNI_INT, &value, sizeof(int));
#else
			SetUniform(loc, (int)value);
#endif
		}
		Py_RETURN_NONE;
	}
	return NULL;
}


KX_PYMETHODDEF_DOC( BL_Shader, setUniform2i, "setUniform2i(name, ix, iy)")
{
	if (mError) {
		Py_RETURN_NONE;
	}
	const char *uniform="";
	int array[2]={ 0,0 };
	if (PyArg_ParseTuple(args, "sii:setUniform2i", &uniform, &array[0],&array[1] ))
	{
		int loc = GetUniformLocation(uniform);
		if (loc != -1)
		{
#ifdef SORT_UNIFORMS
			SetUniformiv(loc, BL_Uniform::UNI_INT2, array, sizeof(int)*2);
#else
			SetUniform(loc, array, 2);
#endif
		}
		Py_RETURN_NONE;
	}
	return NULL;
}


KX_PYMETHODDEF_DOC( BL_Shader, setUniform3i, "setUniform3i(name, ix,iy,iz) ")
{
	if (mError) {
		Py_RETURN_NONE;
	}

	const char *uniform="";
	int array[3]={0,0,0};
	if (PyArg_ParseTuple(args, "siii:setUniform3i", &uniform, &array[0],&array[1],&array[2]))
	{
		int loc = GetUniformLocation(uniform);
		if (loc != -1)
		{
#ifdef SORT_UNIFORMS
			SetUniformiv(loc, BL_Uniform::UNI_INT3, array, sizeof(int)*3);
#else
			SetUniform(loc, array, 3);
#endif
		}
		Py_RETURN_NONE;
	}
	return NULL;
}

KX_PYMETHODDEF_DOC( BL_Shader, setUniform4i, "setUniform4i(name, ix,iy,iz, iw) ")
{
	if (mError) {
		Py_RETURN_NONE;
	}
	const char *uniform="";
	int array[4]={0,0,0, 0};
	if (PyArg_ParseTuple(args, "siiii:setUniform4i", &uniform, &array[0],&array[1],&array[2], &array[3] ))
	{
		int loc = GetUniformLocation(uniform);
		if (loc != -1)
		{
#ifdef SORT_UNIFORMS
			SetUniformiv(loc, BL_Uniform::UNI_INT4, array, sizeof(int)*4);
#else
			SetUniform(loc, array, 4);
#endif
		}
		Py_RETURN_NONE;
	}
	return NULL;
}

KX_PYMETHODDEF_DOC( BL_Shader, setUniformfv, "setUniformfv( float (list2 or list3 or list4) )")
{
	if (mError) {
		Py_RETURN_NONE;
	}
	const char *uniform = "";
	PyObject *listPtr =0;
	float array_data[4] = {0.f,0.f,0.f,0.f};

	if (PyArg_ParseTuple(args, "sO:setUniformfv", &uniform, &listPtr))
	{
		int loc = GetUniformLocation(uniform);
		if (loc != -1)
		{
			if (PySequence_Check(listPtr))
			{
				unsigned int list_size = PySequence_Size(listPtr);
				
				for (unsigned int i=0; (i<list_size && i<4); i++)
				{
					PyObject *item = PySequence_GetItem(listPtr, i);
					array_data[i] = (float)PyFloat_AsDouble(item);
					Py_DECREF(item);
				}

				switch(list_size)
				{
				case 2:
					{
						float array2[2] = { array_data[0],array_data[1] };
#ifdef SORT_UNIFORMS
						SetUniformfv(loc, BL_Uniform::UNI_FLOAT2, array2, sizeof(float)*2);
#else
						SetUniform(loc, array2, 2);						
#endif
						Py_RETURN_NONE;
					} break;
				case 3:
					{
						float array3[3] = { array_data[0],array_data[1],array_data[2] };
#ifdef SORT_UNIFORMS
						SetUniformfv(loc, BL_Uniform::UNI_FLOAT3, array3, sizeof(float)*3);
#else
						SetUniform(loc, array3, 3);	
#endif
						Py_RETURN_NONE;
					}break;
				case 4:
					{
						float array4[4] = { array_data[0],array_data[1],array_data[2],array_data[3] };
#ifdef SORT_UNIFORMS
						SetUniformfv(loc, BL_Uniform::UNI_FLOAT4, array4, sizeof(float)*4);
#else
						SetUniform(loc, array4, 4);	
#endif
						Py_RETURN_NONE;
					}break;
				default:
					{
						PyErr_SetString(PyExc_TypeError, "shader.setUniform4i(name, ix,iy,iz, iw): BL_Shader. invalid list size");
						return NULL;
					}break;
				}
			}
		}
	}
	return NULL;
}

KX_PYMETHODDEF_DOC( BL_Shader, setUniformiv, "setUniformiv( uniform_name, (list2 or list3 or list4) )")
{
	if (mError) {
		Py_RETURN_NONE;
	}
	const char *uniform = "";
	PyObject *listPtr =0;
	int array_data[4] = {0,0,0,0};

	if (!PyArg_ParseTuple(args, "sO:setUniformiv", &uniform, &listPtr))
		return NULL;
	
	int loc = GetUniformLocation(uniform);
	
	if (loc == -1) {
		PyErr_SetString(PyExc_TypeError, "shader.setUniformiv(...): BL_Shader, first string argument is not a valid uniform value");
		return NULL;
	}
	
	if (!PySequence_Check(listPtr)) {
		PyErr_SetString(PyExc_TypeError, "shader.setUniformiv(...): BL_Shader, second argument is not a sequence");
		return NULL;
	}
	
	unsigned int list_size = PySequence_Size(listPtr);
	
	for (unsigned int i=0; (i<list_size && i<4); i++)
	{
		PyObject *item = PySequence_GetItem(listPtr, i);
		array_data[i] = PyLong_AsSsize_t(item);
		Py_DECREF(item);
	}
	
	if (PyErr_Occurred()) {
		PyErr_SetString(PyExc_TypeError, "shader.setUniformiv(...): BL_Shader, one or more values in the list is not an int");
		return NULL;
	}
	
	/* Sanity checks done! */
	
	switch(list_size)
	{
	case 2:
		{
			int array2[2] = { array_data[0],array_data[1]};
#ifdef SORT_UNIFORMS
			SetUniformiv(loc, BL_Uniform::UNI_INT2, array2, sizeof(int)*2);
#else
			SetUniform(loc, array2, 2);						
#endif
			Py_RETURN_NONE;
		} break;
	case 3:
		{
			int array3[3] = { array_data[0],array_data[1],array_data[2] };
#ifdef SORT_UNIFORMS
			SetUniformiv(loc, BL_Uniform::UNI_INT3, array3, sizeof(int)*3);
			
#else
			SetUniform(loc, array3, 3);	
#endif
			Py_RETURN_NONE;
		}break;
	case 4:
		{
			int array4[4] = { array_data[0],array_data[1],array_data[2],array_data[3] };
#ifdef SORT_UNIFORMS
			SetUniformiv(loc, BL_Uniform::UNI_INT4, array4, sizeof(int)*4);
			
#else
			SetUniform(loc, array4, 4);	
#endif
			Py_RETURN_NONE;
		}break;
	default:
		{
			PyErr_SetString(PyExc_TypeError, "shader.setUniformiv(...): BL_Shader, second argument, invalid list size, expected an int list between 2 and 4");
			return NULL;
		}break;
	}
	
	Py_RETURN_NONE;
}


KX_PYMETHODDEF_DOC( BL_Shader, setUniformMatrix4, 
"setUniformMatrix4(uniform_name, mat-4x4, transpose(row-major=true, col-major=false)" )
{
	if (mError) {
		Py_RETURN_NONE;
	}

	float matr[16] = {
		1,0,0,0,
		0,1,0,0,
		0,0,1,0,
		0,0,0,1
	};

	const char *uniform="";
	PyObject *matrix=0;
	int transp=0; // python use column major by default, so no transpose....
	
	if (!PyArg_ParseTuple(args, "sO|i:setUniformMatrix4",&uniform, &matrix,&transp))
		return NULL;

	int loc = GetUniformLocation(uniform);
	
	if (loc == -1) {
		PyErr_SetString(PyExc_TypeError, "shader.setUniformMatrix4(...): BL_Shader, first string argument is not a valid uniform value");
		return NULL;
	}
	
	MT_Matrix4x4 mat;
	
	if (!PyMatTo(matrix, mat)) {
		PyErr_SetString(PyExc_TypeError, "shader.setUniformMatrix4(...): BL_Shader, second argument cannot be converted into a 4x4 matrix");
		return NULL;
	}
	
	/* Sanity checks done! */

#ifdef SORT_UNIFORMS
	mat.getValue(matr);
	SetUniformfv(loc, BL_Uniform::UNI_MAT4, matr, (sizeof(float)*16), (transp!=0) );
#else
	SetUniform(loc,mat,(transp!=0));
#endif
	Py_RETURN_NONE;
}


KX_PYMETHODDEF_DOC( BL_Shader, setUniformMatrix3,
"setUniformMatrix3(uniform_name, list[3x3], transpose(row-major=true, col-major=false)" )
{
	if (mError) {
		Py_RETURN_NONE;
	}

	float matr[9] = {
		1,0,0,
		0,1,0,
		0,0,1,
	};

	const char *uniform="";
	PyObject *matrix=0;
	int transp=0; // python use column major by default, so no transpose....
	if (!PyArg_ParseTuple(args, "sO|i:setUniformMatrix3",&uniform, &matrix,&transp))
		return NULL;
	
	int loc = GetUniformLocation(uniform);
	
	if (loc == -1) {
		PyErr_SetString(PyExc_TypeError, "shader.setUniformMatrix3(...): BL_Shader, first string argument is not a valid uniform value");
		return NULL;
	}
	
	
	MT_Matrix3x3 mat;
	
	if (!PyMatTo(matrix, mat)) {
		PyErr_SetString(PyExc_TypeError, "shader.setUniformMatrix3(...): BL_Shader, second argument cannot be converted into a 3x3 matrix");
		return NULL;
	}
	

#ifdef SORT_UNIFORMS
	mat.getValue(matr);
	SetUniformfv(loc, BL_Uniform::UNI_MAT3, matr, (sizeof(float)*9), (transp!=0) );
#else
	SetUniform(loc,mat,(transp!=0));
#endif
	Py_RETURN_NONE;
}

KX_PYMETHODDEF_DOC( BL_Shader, setAttrib, "setAttrib(enum)" )
{
	if (mError) {
		Py_RETURN_NONE;
	}
	
	int attr=0;
	
	if (!PyArg_ParseTuple(args, "i:setAttrib", &attr))
		return NULL;

	attr= SHD_TANGENT; /* user input is ignored for now, there is only 1 attr */

	if (mShader==0) {
		PyErr_SetString(PyExc_ValueError, "shader.setAttrib() BL_Shader, invalid shader object");
		return NULL;
	}

	mAttr= attr;
	gpuUseProgram(mShader);
	gpuBindAttribLocation(mShader, mAttr, "Tangent");
	Py_RETURN_NONE;
}


KX_PYMETHODDEF_DOC( BL_Shader, setUniformDef, "setUniformDef(name, enum)" )
{
	if (mError) {
		Py_RETURN_NONE;
	}

	const char *uniform="";
	int nloc=0;
	if (PyArg_ParseTuple(args, "si:setUniformDef",&uniform, &nloc))
	{
		int loc = GetUniformLocation(uniform);
		if (loc != -1)
		{
			bool defined = false;
			BL_UniformVecDef::iterator it = mPreDef.begin();
			while(it != mPreDef.end()) {
				if ((*it)->mLoc == loc) {
					defined = true;
					break;
				}
				it++;
			}
			if (defined)
			{
				Py_RETURN_NONE;
			}

			BL_DefUniform *uni = new BL_DefUniform();
			uni->mLoc = loc;
			uni->mType = nloc;
			uni->mFlag = 0;
			mPreDef.push_back(uni);
			Py_RETURN_NONE;
		}
	}
	return NULL;
}

#endif // WITH_PYTHON

// eof