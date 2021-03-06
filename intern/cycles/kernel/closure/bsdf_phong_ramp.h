/*
 * Adapted from Open Shading Language with this license:
 *
 * Copyright (c) 2009-2010 Sony Pictures Imageworks Inc., et al.
 * All Rights Reserved.
 *
 * Modifications Copyright 2012, Blender Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * * Neither the name of Sony Pictures Imageworks nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __BSDF_PHONG_RAMP_H__
#define __BSDF_PHONG_RAMP_H__

CCL_NAMESPACE_BEGIN

__device float3 bsdf_phong_ramp_get_color(const ShaderClosure *sc, const float3 colors[8], float pos)
{
	int MAXCOLORS = 8;
	
	float npos = pos * (float)(MAXCOLORS - 1);
	int ipos = (int)npos;
	if (ipos >= (MAXCOLORS - 1))
		return colors[MAXCOLORS - 1];
	float offset = npos - (float)ipos;
	return colors[ipos] * (1.0f - offset) + colors[ipos+1] * offset;
}

__device int bsdf_phong_ramp_setup(ShaderClosure *sc)
{
	sc->type = CLOSURE_BSDF_PHONG_RAMP_ID;
	return SD_BSDF | SD_BSDF_HAS_EVAL | SD_BSDF_GLOSSY;
}

__device void bsdf_phong_ramp_blur(ShaderClosure *sc, float roughness)
{
}

__device float3 bsdf_phong_ramp_eval_reflect(const ShaderClosure *sc, const float3 colors[8], const float3 I, const float3 omega_in, float *pdf)
{
	float m_exponent = sc->data0;
	float cosNI = dot(sc->N, omega_in);
	float cosNO = dot(sc->N, I);
	
	if (cosNI > 0 && cosNO > 0) {
		// reflect the view vector
		float3 R = (2 * cosNO) * sc->N - I;
		float cosRI = dot(R, omega_in);
		if (cosRI > 0) {
			float cosp = powf(cosRI, m_exponent);
			float common = 0.5f * (float) M_1_PI_F * cosp;
			float out = cosNI * (m_exponent + 2) * common;
			*pdf = (m_exponent + 1) * common;
			return bsdf_phong_ramp_get_color(sc, colors, cosp) * out;
		}
	}
	
	return make_float3(0.0f, 0.0f, 0.0f);
}

__device float3 bsdf_phong_ramp_eval_transmit(const ShaderClosure *sc, const float3 colors[8], const float3 I, const float3 omega_in, float *pdf)
{
	return make_float3(0.0f, 0.0f, 0.0f);
}

__device int bsdf_phong_ramp_sample(const ShaderClosure *sc, const float3 colors[8], float3 Ng, float3 I, float3 dIdx, float3 dIdy, float randu, float randv, float3 *eval, float3 *omega_in, float3 *domega_in_dx, float3 *domega_in_dy, float *pdf)
{
	float cosNO = dot(sc->N, I);
	float m_exponent = sc->data0;
	
	if (cosNO > 0) {
		// reflect the view vector
		float3 R = (2 * cosNO) * sc->N - I;

#ifdef __RAY_DIFFERENTIALS__
		*domega_in_dx = (2 * dot(sc->N, dIdx)) * sc->N - dIdx;
		*domega_in_dy = (2 * dot(sc->N, dIdy)) * sc->N - dIdy;
#endif
		
		float3 T, B;
		make_orthonormals (R, &T, &B);
		float phi = 2 * M_PI_F * randu;
		float cosTheta = powf(randv, 1 / (m_exponent + 1));
		float sinTheta2 = 1 - cosTheta * cosTheta;
		float sinTheta = sinTheta2 > 0 ? sqrtf(sinTheta2) : 0;
		*omega_in = (cosf(phi) * sinTheta) * T +
				   (sinf(phi) * sinTheta) * B +
				   (            cosTheta) * R;
		if (dot(Ng, *omega_in) > 0.0f)
		{
			// common terms for pdf and eval
			float cosNI = dot(sc->N, *omega_in);
			// make sure the direction we chose is still in the right hemisphere
			if (cosNI > 0)
			{
				float cosp = powf(cosTheta, m_exponent);
				float common = 0.5f * M_1_PI_F * cosp;
				*pdf = (m_exponent + 1) * common;
				float out = cosNI * (m_exponent + 2) * common;
				*eval = bsdf_phong_ramp_get_color(sc, colors, cosp) * out;
				
#ifdef __RAY_DIFFERENTIALS__
				// Since there is some blur to this reflection, make the
				// derivatives a bit bigger. In theory this varies with the
				// exponent but the exact relationship is complex and
				// requires more ops than are practical.
				*domega_in_dx *= 10;
				*domega_in_dy *= 10;
#endif
			}
		}
	}
	return LABEL_REFLECT;
}


CCL_NAMESPACE_END

#endif /* __BSDF_PHONG_RAMP_H__ */
