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

 * The Original Code is: some of this file.
 *
 * ***** END GPL LICENSE BLOCK *****
 * */

/** \file blender/blenlib/intern/math_rotation.c
 *  \ingroup bli
 */



#include <assert.h>
#include "BLI_math.h"

/******************************** Quaternions ********************************/

/* used to test is a quat is not normalized (only used for debug prints) */
#ifdef DEBUG
#  define QUAT_EPSILON 0.0001
#endif

/* convenience, avoids setting Y axis everywhere */
void unit_axis_angle(float axis[3], float *angle)
{
	axis[0] = 0.0f;
	axis[1] = 1.0f;
	axis[2] = 0.0f;
	*angle = 0.0f;
}

void unit_qt(float q[4])
{
	q[0] = 1.0f;
	q[1] = q[2] = q[3] = 0.0f;
}

void copy_qt_qt(float q1[4], const float q2[4])
{
	q1[0] = q2[0];
	q1[1] = q2[1];
	q1[2] = q2[2];
	q1[3] = q2[3];
}

int is_zero_qt(float *q)
{
	return (q[0] == 0 && q[1] == 0 && q[2] == 0 && q[3] == 0);
}

void mul_qt_qtqt(float q[4], const float q1[4], const float q2[4])
{
	float t0, t1, t2;

	t0 = q1[0] * q2[0] - q1[1] * q2[1] - q1[2] * q2[2] - q1[3] * q2[3];
	t1 = q1[0] * q2[1] + q1[1] * q2[0] + q1[2] * q2[3] - q1[3] * q2[2];
	t2 = q1[0] * q2[2] + q1[2] * q2[0] + q1[3] * q2[1] - q1[1] * q2[3];
	q[3] = q1[0] * q2[3] + q1[3] * q2[0] + q1[1] * q2[2] - q1[2] * q2[1];
	q[0] = t0;
	q[1] = t1;
	q[2] = t2;
}

/**
 * \note:
 * Assumes a unit quaternion?
 *
 * in fact not, but you may want to use a unit quat, read on...
 *
 * Shortcut for 'q v q*' when \a v is actually a quaternion.
 * This removes the need for converting a vector to a quaternion,
 * calculating q's conjugate and converting back to a vector.
 * It also happens to be faster (17+,24* vs * 24+,32*).
 * If \a q is not a unit quaternion, then \a v will be both rotated by
 * the same amount as if q was a unit quaternion, and scaled by the square of
 * the length of q.
 *
 * For people used to python mathutils, its like:
 * def mul_qt_v3(q, v): (q * Quaternion((0.0, v[0], v[1], v[2])) * q.conjugated())[1:]
 */
void mul_qt_v3(const float q[4], float v[3])
{
	float t0, t1, t2;

	t0 = -q[1] * v[0] - q[2] * v[1] - q[3] * v[2];
	t1 = q[0] * v[0] + q[2] * v[2] - q[3] * v[1];
	t2 = q[0] * v[1] + q[3] * v[0] - q[1] * v[2];
	v[2] = q[0] * v[2] + q[1] * v[1] - q[2] * v[0];
	v[0] = t1;
	v[1] = t2;

	t1 = t0 * -q[1] + v[0] * q[0] - v[1] * q[3] + v[2] * q[2];
	t2 = t0 * -q[2] + v[1] * q[0] - v[2] * q[1] + v[0] * q[3];
	v[2] = t0 * -q[3] + v[2] * q[0] - v[0] * q[2] + v[1] * q[1];
	v[0] = t1;
	v[1] = t2;
}

void conjugate_qt_qt(float q1[4], const float q2[4])
{
	q1[0] =  q2[0];
	q1[1] = -q2[1];
	q1[2] = -q2[2];
	q1[3] = -q2[3];
}

void conjugate_qt(float q[4])
{
	q[1] = -q[1];
	q[2] = -q[2];
	q[3] = -q[3];
}

float dot_qtqt(const float q1[4], const float q2[4])
{
	return q1[0] * q2[0] + q1[1] * q2[1] + q1[2] * q2[2] + q1[3] * q2[3];
}

void invert_qt(float q[4])
{
	float f = dot_qtqt(q, q);

	if (f == 0.0f)
		return;

	conjugate_qt(q);
	mul_qt_fl(q, 1.0f / f);
}

void invert_qt_qt(float q1[4], const float q2[4])
{
	copy_qt_qt(q1, q2);
	invert_qt(q1);
}

/* simple mult */
void mul_qt_fl(float q[4], const float f)
{
	q[0] *= f;
	q[1] *= f;
	q[2] *= f;
	q[3] *= f;
}

void sub_qt_qtqt(float q[4], const float q1[4], const float q2[4])
{
	float nq2[4];

	nq2[0] = -q2[0];
	nq2[1] = q2[1];
	nq2[2] = q2[2];
	nq2[3] = q2[3];

	mul_qt_qtqt(q, q1, nq2);
}

/* angular mult factor */
void mul_fac_qt_fl(float q[4], const float fac)
{
	const float angle = fac * saacos(q[0]); /* quat[0] = cos(0.5 * angle), but now the 0.5 and 2.0 rule out */
	const float co = cosf(angle);
	const float si = sinf(angle);
	q[0] = co;
	normalize_v3(q + 1);
	mul_v3_fl(q + 1, si);
}

/* skip error check, currently only needed by mat3_to_quat_is_ok */
static void quat_to_mat3_no_error(float m[][3], const float q[4])
{
	double q0, q1, q2, q3, qda, qdb, qdc, qaa, qab, qac, qbb, qbc, qcc;

	q0 = M_SQRT2 * (double)q[0];
	q1 = M_SQRT2 * (double)q[1];
	q2 = M_SQRT2 * (double)q[2];
	q3 = M_SQRT2 * (double)q[3];

	qda = q0 * q1;
	qdb = q0 * q2;
	qdc = q0 * q3;
	qaa = q1 * q1;
	qab = q1 * q2;
	qac = q1 * q3;
	qbb = q2 * q2;
	qbc = q2 * q3;
	qcc = q3 * q3;

	m[0][0] = (float)(1.0 - qbb - qcc);
	m[0][1] = (float)(qdc + qab);
	m[0][2] = (float)(-qdb + qac);

	m[1][0] = (float)(-qdc + qab);
	m[1][1] = (float)(1.0 - qaa - qcc);
	m[1][2] = (float)(qda + qbc);

	m[2][0] = (float)(qdb + qac);
	m[2][1] = (float)(-qda + qbc);
	m[2][2] = (float)(1.0 - qaa - qbb);
}

void quat_to_mat3(float m[][3], const float q[4])
{
#ifdef DEBUG
	float f;
	if (!((f = dot_qtqt(q, q)) == 0.0f || (fabsf(f - 1.0f) < (float)QUAT_EPSILON))) {
		fprintf(stderr, "Warning! quat_to_mat3() called with non-normalized: size %.8f *** report a bug ***\n", f);
	}
#endif

	quat_to_mat3_no_error(m, q);
}

void quat_to_mat4(float m[][4], const float q[4])
{
	double q0, q1, q2, q3, qda, qdb, qdc, qaa, qab, qac, qbb, qbc, qcc;

#ifdef DEBUG
	if (!((q0 = dot_qtqt(q, q)) == 0.0 || (fabs(q0 - 1.0) < QUAT_EPSILON))) {
		fprintf(stderr, "Warning! quat_to_mat4() called with non-normalized: size %.8f *** report a bug ***\n", (float)q0);
	}
#endif

	q0 = M_SQRT2 * (double)q[0];
	q1 = M_SQRT2 * (double)q[1];
	q2 = M_SQRT2 * (double)q[2];
	q3 = M_SQRT2 * (double)q[3];

	qda = q0 * q1;
	qdb = q0 * q2;
	qdc = q0 * q3;
	qaa = q1 * q1;
	qab = q1 * q2;
	qac = q1 * q3;
	qbb = q2 * q2;
	qbc = q2 * q3;
	qcc = q3 * q3;

	m[0][0] = (float)(1.0 - qbb - qcc);
	m[0][1] = (float)(qdc + qab);
	m[0][2] = (float)(-qdb + qac);
	m[0][3] = 0.0f;

	m[1][0] = (float)(-qdc + qab);
	m[1][1] = (float)(1.0 - qaa - qcc);
	m[1][2] = (float)(qda + qbc);
	m[1][3] = 0.0f;

	m[2][0] = (float)(qdb + qac);
	m[2][1] = (float)(-qda + qbc);
	m[2][2] = (float)(1.0 - qaa - qbb);
	m[2][3] = 0.0f;

	m[3][0] = m[3][1] = m[3][2] = 0.0f;
	m[3][3] = 1.0f;
}

void mat3_to_quat(float q[4], float wmat[][3])
{
	double tr, s;
	float mat[3][3];

	/* work on a copy */
	copy_m3_m3(mat, wmat);
	normalize_m3(mat); /* this is needed AND a 'normalize_qt' in the end */

	tr = 0.25 * (double)(1.0f + mat[0][0] + mat[1][1] + mat[2][2]);

	if (tr > (double)FLT_EPSILON) {
		s = sqrt(tr);
		q[0] = (float)s;
		s = 1.0 / (4.0 * s);
		q[1] = (float)((double)(mat[1][2] - mat[2][1]) * s);
		q[2] = (float)((double)(mat[2][0] - mat[0][2]) * s);
		q[3] = (float)((double)(mat[0][1] - mat[1][0]) * s);
	}
	else {
		if (mat[0][0] > mat[1][1] && mat[0][0] > mat[2][2]) {
			s = 2.0f * sqrtf(1.0f + mat[0][0] - mat[1][1] - mat[2][2]);
			q[1] = (float)(0.25 * s);

			s = 1.0 / s;
			q[0] = (float)((double)(mat[2][1] - mat[1][2]) * s);
			q[2] = (float)((double)(mat[1][0] + mat[0][1]) * s);
			q[3] = (float)((double)(mat[2][0] + mat[0][2]) * s);
		}
		else if (mat[1][1] > mat[2][2]) {
			s = 2.0f * sqrtf(1.0f + mat[1][1] - mat[0][0] - mat[2][2]);
			q[2] = (float)(0.25 * s);

			s = 1.0 / s;
			q[0] = (float)((double)(mat[2][0] - mat[0][2]) * s);
			q[1] = (float)((double)(mat[1][0] + mat[0][1]) * s);
			q[3] = (float)((double)(mat[2][1] + mat[1][2]) * s);
		}
		else {
			s = 2.0f * sqrtf(1.0f + mat[2][2] - mat[0][0] - mat[1][1]);
			q[3] = (float)(0.25 * s);

			s = 1.0 / s;
			q[0] = (float)((double)(mat[1][0] - mat[0][1]) * s);
			q[1] = (float)((double)(mat[2][0] + mat[0][2]) * s);
			q[2] = (float)((double)(mat[2][1] + mat[1][2]) * s);
		}
	}

	normalize_qt(q);
}

void mat4_to_quat(float q[4], float m[][4])
{
	float mat[3][3];

	copy_m3_m4(mat, m);
	mat3_to_quat(q, mat);
}

void mat3_to_quat_is_ok(float q[4], float wmat[3][3])
{
	float mat[3][3], matr[3][3], matn[3][3], q1[4], q2[4], angle, si, co, nor[3];

	/* work on a copy */
	copy_m3_m3(mat, wmat);
	normalize_m3(mat);

	/* rotate z-axis of matrix to z-axis */

	nor[0] = mat[2][1]; /* cross product with (0,0,1) */
	nor[1] = -mat[2][0];
	nor[2] = 0.0;
	normalize_v3(nor);

	co = mat[2][2];
	angle = 0.5f * saacos(co);

	co = cosf(angle);
	si = sinf(angle);
	q1[0] = co;
	q1[1] = -nor[0] * si; /* negative here, but why? */
	q1[2] = -nor[1] * si;
	q1[3] = -nor[2] * si;

	/* rotate back x-axis from mat, using inverse q1 */
	quat_to_mat3_no_error(matr, q1);
	invert_m3_m3(matn, matr);
	mul_m3_v3(matn, mat[0]);

	/* and align x-axes */
	angle = (float)(0.5 * atan2(mat[0][1], mat[0][0]));

	co = cosf(angle);
	si = sinf(angle);
	q2[0] = co;
	q2[1] = 0.0f;
	q2[2] = 0.0f;
	q2[3] = si;

	mul_qt_qtqt(q, q1, q2);
}

float normalize_qt(float q[4])
{
	float len;

	len = sqrtf(dot_qtqt(q, q));
	if (len != 0.0f) {
		mul_qt_fl(q, 1.0f / len);
	}
	else {
		q[1] = 1.0f;
		q[0] = q[2] = q[3] = 0.0f;
	}

	return len;
}

float normalize_qt_qt(float r[4], const float q[4])
{
	copy_qt_qt(r, q);
	return normalize_qt(r);
}

/* note: expects vectors to be normalized */
void rotation_between_vecs_to_quat(float q[4], const float v1[3], const float v2[3])
{
	float axis[3];
	float angle;

	cross_v3_v3v3(axis, v1, v2);

	angle = angle_normalized_v3v3(v1, v2);

	axis_angle_to_quat(q, axis, angle);
}

void rotation_between_quats_to_quat(float q[4], const float q1[4], const float q2[4])
{
	float tquat[4];

	conjugate_qt_qt(tquat, q1);

	mul_qt_fl(tquat, 1.0f / dot_qtqt(tquat, tquat));

	mul_qt_qtqt(q, tquat, q2);
}

void vec_to_quat(float q[4], const float vec[3], short axis, const short upflag)
{
	float nor[3], tvec[3];
	float angle, si, co, len;

	assert(axis >= 0 && axis <= 5);
	assert(upflag >= 0 && upflag <= 2);

	/* first set the quat to unit */
	unit_qt(q);

	len = len_v3(vec);

	if (UNLIKELY(len == 0.0f)) {
		return;
	}

	/* rotate to axis */
	if (axis > 2) {
		copy_v3_v3(tvec, vec);
		axis -= 3;
	}
	else {
		negate_v3_v3(tvec, vec);
	}

	/* nasty! I need a good routine for this...
	 * problem is a rotation of an Y axis to the negative Y-axis for example.
	 */

	if (axis == 0) { /* x-axis */
		nor[0] =  0.0;
		nor[1] = -tvec[2];
		nor[2] =  tvec[1];

		if (fabsf(tvec[1]) + fabsf(tvec[2]) < 0.0001f)
			nor[1] = 1.0f;

		co = tvec[0];
	}
	else if (axis == 1) { /* y-axis */
		nor[0] =  tvec[2];
		nor[1] =  0.0;
		nor[2] = -tvec[0];

		if (fabsf(tvec[0]) + fabsf(tvec[2]) < 0.0001f)
			nor[2] = 1.0f;

		co = tvec[1];
	}
	else { /* z-axis */
		nor[0] = -tvec[1];
		nor[1] =  tvec[0];
		nor[2] =  0.0;

		if (fabsf(tvec[0]) + fabsf(tvec[1]) < 0.0001f)
			nor[0] = 1.0f;

		co = tvec[2];
	}
	co /= len;

	normalize_v3(nor);

	angle = 0.5f * saacos(co);
	si   = sinf(angle);
	q[0] = cosf(angle);
	q[1] = nor[0] * si;
	q[2] = nor[1] * si;
	q[3] = nor[2] * si;

	if (axis != upflag) {
		float mat[3][3];
		float q2[4];
		const float *fp = mat[2];
		quat_to_mat3(mat, q);

		if (axis == 0) {
			if (upflag == 1) angle =  0.5f * atan2f(fp[2], fp[1]);
			else             angle = -0.5f * atan2f(fp[1], fp[2]);
		}
		else if (axis == 1) {
			if (upflag == 0) angle = -0.5f * atan2f(fp[2], fp[0]);
			else             angle =  0.5f * atan2f(fp[0], fp[2]);
		}
		else {
			if (upflag == 0) angle =  0.5f * atan2f(-fp[1], -fp[0]);
			else             angle = -0.5f * atan2f(-fp[0], -fp[1]);
		}

		co = cosf(angle);
		si = sinf(angle) / len;
		q2[0] = co;
		q2[1] = tvec[0] * si;
		q2[2] = tvec[1] * si;
		q2[3] = tvec[2] * si;

		mul_qt_qtqt(q, q2, q);
	}
}

#if 0

/* A & M Watt, Advanced animation and rendering techniques, 1992 ACM press */
void QuatInterpolW(float *result, float quat1[4], float quat2[4], float t)
{
	float omega, cosom, sinom, sc1, sc2;

	cosom = quat1[0] * quat2[0] + quat1[1] * quat2[1] + quat1[2] * quat2[2] + quat1[3] * quat2[3];

	/* rotate around shortest angle */
	if ((1.0f + cosom) > 0.0001f) {

		if ((1.0f - cosom) > 0.0001f) {
			omega = (float)acos(cosom);
			sinom = (float)sin(omega);
			sc1 = (float)sin((1.0 - t) * omega) / sinom;
			sc2 = (float)sin(t * omega) / sinom;
		}
		else {
			sc1 = 1.0f - t;
			sc2 = t;
		}
		result[0] = sc1 * quat1[0] + sc2 * quat2[0];
		result[1] = sc1 * quat1[1] + sc2 * quat2[1];
		result[2] = sc1 * quat1[2] + sc2 * quat2[2];
		result[3] = sc1 * quat1[3] + sc2 * quat2[3];
	}
	else {
		result[0] = quat2[3];
		result[1] = -quat2[2];
		result[2] = quat2[1];
		result[3] = -quat2[0];

		sc1 = (float)sin((1.0 - t) * M_PI_2);
		sc2 = (float)sin(t * M_PI_2);

		result[0] = sc1 * quat1[0] + sc2 * result[0];
		result[1] = sc1 * quat1[1] + sc2 * result[1];
		result[2] = sc1 * quat1[2] + sc2 * result[2];
		result[3] = sc1 * quat1[3] + sc2 * result[3];
	}
}
#endif

void interp_qt_qtqt(float result[4], const float quat1[4], const float quat2[4], const float t)
{
	float quat[4], omega, cosom, sinom, sc1, sc2;

	cosom = quat1[0] * quat2[0] + quat1[1] * quat2[1] + quat1[2] * quat2[2] + quat1[3] * quat2[3];

	/* rotate around shortest angle */
	if (cosom < 0.0f) {
		cosom = -cosom;
		quat[0] = -quat1[0];
		quat[1] = -quat1[1];
		quat[2] = -quat1[2];
		quat[3] = -quat1[3];
	}
	else {
		quat[0] = quat1[0];
		quat[1] = quat1[1];
		quat[2] = quat1[2];
		quat[3] = quat1[3];
	}

	if ((1.0f - cosom) > 0.0001f) {
		omega = (float)acos(cosom);
		sinom = (float)sin(omega);
		sc1 = (float)sin((1 - t) * omega) / sinom;
		sc2 = (float)sin(t * omega) / sinom;
	}
	else {
		sc1 = 1.0f - t;
		sc2 = t;
	}

	result[0] = sc1 * quat[0] + sc2 * quat2[0];
	result[1] = sc1 * quat[1] + sc2 * quat2[1];
	result[2] = sc1 * quat[2] + sc2 * quat2[2];
	result[3] = sc1 * quat[3] + sc2 * quat2[3];
}

void add_qt_qtqt(float result[4], const float quat1[4], const float quat2[4], const float t)
{
	result[0] = quat1[0] + t * quat2[0];
	result[1] = quat1[1] + t * quat2[1];
	result[2] = quat1[2] + t * quat2[2];
	result[3] = quat1[3] + t * quat2[3];
}

/* same as tri_to_quat() but takes pre-computed normal from the triangle
 * used for ngons when we know their normal */
void tri_to_quat_ex(float quat[4], const float v1[3], const float v2[3], const float v3[3],
                    const float no_orig[3])
{
	/* imaginary x-axis, y-axis triangle is being rotated */
	float vec[3], q1[4], q2[4], n[3], si, co, angle, mat[3][3], imat[3][3];

	/* move z-axis to face-normal */
#if 0
	normal_tri_v3(vec, v1, v2, v3);
#else
	copy_v3_v3(vec, no_orig);
	(void)v3;
#endif

	n[0] =  vec[1];
	n[1] = -vec[0];
	n[2] =  0.0f;
	normalize_v3(n);

	if (n[0] == 0.0f && n[1] == 0.0f) {
		n[0] = 1.0f;
	}

	angle = -0.5f * (float)saacos(vec[2]);
	co = cosf(angle);
	si = sinf(angle);
	q1[0] = co;
	q1[1] = n[0] * si;
	q1[2] = n[1] * si;
	q1[3] = 0.0f;

	/* rotate back line v1-v2 */
	quat_to_mat3(mat, q1);
	invert_m3_m3(imat, mat);
	sub_v3_v3v3(vec, v2, v1);
	mul_m3_v3(imat, vec);

	/* what angle has this line with x-axis? */
	vec[2] = 0.0f;
	normalize_v3(vec);

	angle = (float)(0.5 * atan2(vec[1], vec[0]));
	co = cosf(angle);
	si = sinf(angle);
	q2[0] = co;
	q2[1] = 0.0f;
	q2[2] = 0.0f;
	q2[3] = si;

	mul_qt_qtqt(quat, q1, q2);
}

void tri_to_quat(float quat[4], const float v1[3], const float v2[3], const float v3[3])
{
	float vec[3];
	normal_tri_v3(vec, v1, v2, v3);
	tri_to_quat_ex(quat, v1, v2, v3, vec);
}

void print_qt(const char *str, const float q[4])
{
	printf("%s: %.3f %.3f %.3f %.3f\n", str, q[0], q[1], q[2], q[3]);
}

/******************************** Axis Angle *********************************/

/* Axis angle to Quaternions */
void axis_angle_to_quat(float q[4], const float axis[3], const float angle)
{
	float nor[3];

	if (LIKELY(normalize_v3_v3(nor, axis) != 0.0f)) {
		const float phi = angle / 2.0f;
		float si;
		si   = sinf(phi);
		q[0] = cosf(phi);
		q[1] = nor[0] * si;
		q[2] = nor[1] * si;
		q[3] = nor[2] * si;
	}
	else {
		unit_qt(q);
	}
}

/* Quaternions to Axis Angle */
void quat_to_axis_angle(float axis[3], float *angle, const float q[4])
{
	float ha, si;

#ifdef DEBUG
	if (!((ha = dot_qtqt(q, q)) == 0.0f || (fabsf(ha - 1.0f) < (float)QUAT_EPSILON))) {
		fprintf(stderr, "Warning! quat_to_axis_angle() called with non-normalized: size %.8f *** report a bug ***\n", ha);
	}
#endif

	/* calculate angle/2, and sin(angle/2) */
	ha = acosf(q[0]);
	si = sinf(ha);

	/* from half-angle to angle */
	*angle = ha * 2;

	/* prevent division by zero for axis conversion */
	if (fabsf(si) < 0.0005f)
		si = 1.0f;

	axis[0] = q[1] / si;
	axis[1] = q[2] / si;
	axis[2] = q[3] / si;
}

/* Axis Angle to Euler Rotation */
void axis_angle_to_eulO(float eul[3], const short order, const float axis[3], const float angle)
{
	float q[4];

	/* use quaternions as intermediate representation for now... */
	axis_angle_to_quat(q, axis, angle);
	quat_to_eulO(eul, order, q);
}

/* Euler Rotation to Axis Angle */
void eulO_to_axis_angle(float axis[3], float *angle, const float eul[3], const short order)
{
	float q[4];

	/* use quaternions as intermediate representation for now... */
	eulO_to_quat(q, eul, order);
	quat_to_axis_angle(axis, angle, q);
}

/* axis angle to 3x3 matrix - safer version (normalization of axis performed)
 *
 * note: we may want a normalized and non normalized version of this function.
 */
void axis_angle_to_mat3(float mat[3][3], const float axis[3], const float angle)
{
	float nor[3], nsi[3], co, si, ico;

	/* normalize the axis first (to remove unwanted scaling) */
	if (normalize_v3_v3(nor, axis) == 0.0f) {
		unit_m3(mat);
		return;
	}

	/* now convert this to a 3x3 matrix */
	co = cosf(angle);
	si = sinf(angle);

	ico = (1.0f - co);
	nsi[0] = nor[0] * si;
	nsi[1] = nor[1] * si;
	nsi[2] = nor[2] * si;

	mat[0][0] = ((nor[0] * nor[0]) * ico) + co;
	mat[0][1] = ((nor[0] * nor[1]) * ico) + nsi[2];
	mat[0][2] = ((nor[0] * nor[2]) * ico) - nsi[1];
	mat[1][0] = ((nor[0] * nor[1]) * ico) - nsi[2];
	mat[1][1] = ((nor[1] * nor[1]) * ico) + co;
	mat[1][2] = ((nor[1] * nor[2]) * ico) + nsi[0];
	mat[2][0] = ((nor[0] * nor[2]) * ico) + nsi[1];
	mat[2][1] = ((nor[1] * nor[2]) * ico) - nsi[0];
	mat[2][2] = ((nor[2] * nor[2]) * ico) + co;
}

/* axis angle to 4x4 matrix - safer version (normalization of axis performed) */
void axis_angle_to_mat4(float mat[4][4], const float axis[3], const float angle)
{
	float tmat[3][3];

	axis_angle_to_mat3(tmat, axis, angle);
	unit_m4(mat);
	copy_m4_m3(mat, tmat);
}

/* 3x3 matrix to axis angle (see Mat4ToVecRot too) */
void mat3_to_axis_angle(float axis[3], float *angle, float mat[3][3])
{
	float q[4];

	/* use quaternions as intermediate representation */
	/* TODO: it would be nicer to go straight there... */
	mat3_to_quat(q, mat);
	quat_to_axis_angle(axis, angle, q);
}

/* 4x4 matrix to axis angle (see Mat4ToVecRot too) */
void mat4_to_axis_angle(float axis[3], float *angle, float mat[4][4])
{
	float q[4];

	/* use quaternions as intermediate representation */
	/* TODO: it would be nicer to go straight there... */
	mat4_to_quat(q, mat);
	quat_to_axis_angle(axis, angle, q);
}

void single_axis_angle_to_mat3(float mat[3][3], const char axis, const float angle)
{
	const float angle_cos = cosf(angle);
	const float angle_sin = sinf(angle);

	switch (axis) {
		case 'X': /* rotation around X */
			mat[0][0] = 1.0f;
			mat[0][1] = 0.0f;
			mat[0][2] = 0.0f;
			mat[1][0] = 0.0f;
			mat[1][1] = angle_cos;
			mat[1][2] = angle_sin;
			mat[2][0] = 0.0f;
			mat[2][1] = -angle_sin;
			mat[2][2] = angle_cos;
			break;
		case 'Y': /* rotation around Y */
			mat[0][0] = angle_cos;
			mat[0][1] = 0.0f;
			mat[0][2] = -angle_sin;
			mat[1][0] = 0.0f;
			mat[1][1] = 1.0f;
			mat[1][2] = 0.0f;
			mat[2][0] = angle_sin;
			mat[2][1] = 0.0f;
			mat[2][2] = angle_cos;
			break;
		case 'Z': /* rotation around Z */
			mat[0][0] = angle_cos;
			mat[0][1] = angle_sin;
			mat[0][2] = 0.0f;
			mat[1][0] = -angle_sin;
			mat[1][1] = angle_cos;
			mat[1][2] = 0.0f;
			mat[2][0] = 0.0f;
			mat[2][1] = 0.0f;
			mat[2][2] = 1.0f;
			break;
		default:
			assert(0);
	}
}

/****************************** Vector/Rotation ******************************/
/* TODO: the following calls should probably be deprecated sometime         */

/* TODO, replace use of this function with axis_angle_to_mat3() */
void vec_rot_to_mat3(float mat[][3], const float vec[3], const float phi)
{
	/* rotation of phi radials around vec */
	float vx, vx2, vy, vy2, vz, vz2, co, si;

	vx = vec[0];
	vy = vec[1];
	vz = vec[2];
	vx2 = vx * vx;
	vy2 = vy * vy;
	vz2 = vz * vz;
	co = cosf(phi);
	si = sinf(phi);

	mat[0][0] = vx2 + co * (1.0f - vx2);
	mat[0][1] = vx * vy * (1.0f - co) + vz * si;
	mat[0][2] = vz * vx * (1.0f - co) - vy * si;
	mat[1][0] = vx * vy * (1.0f - co) - vz * si;
	mat[1][1] = vy2 + co * (1.0f - vy2);
	mat[1][2] = vy * vz * (1.0f - co) + vx * si;
	mat[2][0] = vz * vx * (1.0f - co) + vy * si;
	mat[2][1] = vy * vz * (1.0f - co) - vx * si;
	mat[2][2] = vz2 + co * (1.0f - vz2);
}

/******************************** XYZ Eulers *********************************/

/* XYZ order */
void eul_to_mat3(float mat[][3], const float eul[3])
{
	double ci, cj, ch, si, sj, sh, cc, cs, sc, ss;

	ci = cos(eul[0]);
	cj = cos(eul[1]);
	ch = cos(eul[2]);
	si = sin(eul[0]);
	sj = sin(eul[1]);
	sh = sin(eul[2]);
	cc = ci * ch;
	cs = ci * sh;
	sc = si * ch;
	ss = si * sh;

	mat[0][0] = (float)(cj * ch);
	mat[1][0] = (float)(sj * sc - cs);
	mat[2][0] = (float)(sj * cc + ss);
	mat[0][1] = (float)(cj * sh);
	mat[1][1] = (float)(sj * ss + cc);
	mat[2][1] = (float)(sj * cs - sc);
	mat[0][2] = (float)-sj;
	mat[1][2] = (float)(cj * si);
	mat[2][2] = (float)(cj * ci);

}

/* XYZ order */
void eul_to_mat4(float mat[][4], const float eul[3])
{
	double ci, cj, ch, si, sj, sh, cc, cs, sc, ss;

	ci = cos(eul[0]);
	cj = cos(eul[1]);
	ch = cos(eul[2]);
	si = sin(eul[0]);
	sj = sin(eul[1]);
	sh = sin(eul[2]);
	cc = ci * ch;
	cs = ci * sh;
	sc = si * ch;
	ss = si * sh;

	mat[0][0] = (float)(cj * ch);
	mat[1][0] = (float)(sj * sc - cs);
	mat[2][0] = (float)(sj * cc + ss);
	mat[0][1] = (float)(cj * sh);
	mat[1][1] = (float)(sj * ss + cc);
	mat[2][1] = (float)(sj * cs - sc);
	mat[0][2] = (float)-sj;
	mat[1][2] = (float)(cj * si);
	mat[2][2] = (float)(cj * ci);


	mat[3][0] = mat[3][1] = mat[3][2] = mat[0][3] = mat[1][3] = mat[2][3] = 0.0f;
	mat[3][3] = 1.0f;
}

/* returns two euler calculation methods, so we can pick the best */

/* XYZ order */
static void mat3_to_eul2(float tmat[][3], float eul1[3], float eul2[3])
{
	float cy, quat[4], mat[3][3];

	mat3_to_quat(quat, tmat);
	quat_to_mat3(mat, quat);
	copy_m3_m3(mat, tmat);
	normalize_m3(mat);

	cy = (float)sqrt(mat[0][0] * mat[0][0] + mat[0][1] * mat[0][1]);

	if (cy > 16.0f * FLT_EPSILON) {

		eul1[0] = (float)atan2(mat[1][2], mat[2][2]);
		eul1[1] = (float)atan2(-mat[0][2], cy);
		eul1[2] = (float)atan2(mat[0][1], mat[0][0]);

		eul2[0] = (float)atan2(-mat[1][2], -mat[2][2]);
		eul2[1] = (float)atan2(-mat[0][2], -cy);
		eul2[2] = (float)atan2(-mat[0][1], -mat[0][0]);

	}
	else {
		eul1[0] = (float)atan2(-mat[2][1], mat[1][1]);
		eul1[1] = (float)atan2(-mat[0][2], cy);
		eul1[2] = 0.0f;

		copy_v3_v3(eul2, eul1);
	}
}

/* XYZ order */
void mat3_to_eul(float *eul, float tmat[][3])
{
	float eul1[3], eul2[3];

	mat3_to_eul2(tmat, eul1, eul2);

	/* return best, which is just the one with lowest values it in */
	if (fabsf(eul1[0]) + fabsf(eul1[1]) + fabsf(eul1[2]) > fabsf(eul2[0]) + fabsf(eul2[1]) + fabsf(eul2[2])) {
		copy_v3_v3(eul, eul2);
	}
	else {
		copy_v3_v3(eul, eul1);
	}
}

/* XYZ order */
void mat4_to_eul(float *eul, float tmat[][4])
{
	float tempMat[3][3];

	copy_m3_m4(tempMat, tmat);
	normalize_m3(tempMat);
	mat3_to_eul(eul, tempMat);
}

/* XYZ order */
void quat_to_eul(float *eul, const float quat[4])
{
	float mat[3][3];

	quat_to_mat3(mat, quat);
	mat3_to_eul(eul, mat);
}

/* XYZ order */
void eul_to_quat(float *quat, const float eul[3])
{
	float ti, tj, th, ci, cj, ch, si, sj, sh, cc, cs, sc, ss;

	ti = eul[0] * 0.5f;
	tj = eul[1] * 0.5f;
	th = eul[2] * 0.5f;
	ci = cosf(ti);
	cj = cosf(tj);
	ch = cosf(th);
	si = sinf(ti);
	sj = sinf(tj);
	sh = sinf(th);
	cc = ci * ch;
	cs = ci * sh;
	sc = si * ch;
	ss = si * sh;

	quat[0] = cj * cc + sj * ss;
	quat[1] = cj * sc - sj * cs;
	quat[2] = cj * ss + sj * cc;
	quat[3] = cj * cs - sj * sc;
}

/* XYZ order */
void rotate_eul(float *beul, const char axis, const float ang)
{
	float eul[3], mat1[3][3], mat2[3][3], totmat[3][3];

	assert(axis >= 'X' && axis <= 'Z');

	eul[0] = eul[1] = eul[2] = 0.0f;
	if (axis == 'X') eul[0] = ang;
	else if (axis == 'Y') eul[1] = ang;
	else eul[2] = ang;

	eul_to_mat3(mat1, eul);
	eul_to_mat3(mat2, beul);

	mul_m3_m3m3(totmat, mat2, mat1);

	mat3_to_eul(beul, totmat);

}

/* order independent! */
void compatible_eul(float eul[3], const float oldrot[3])
{
	/* we could use M_PI as pi_thresh: which is correct but 5.1 gives better results.
	 * Checked with baking actions to fcurves - campbell */
	const float pi_thresh = (5.1f);
	const float pi_x2     = (2.0f * (float)M_PI);

	float deul[3];
	unsigned int i;

	/* correct differences of about 360 degrees first */
	for (i = 0; i < 3; i++) {
		deul[i] = eul[i] - oldrot[i];
		if (deul[i] > pi_thresh) {
			eul[i] -= floorf(( deul[i] / pi_x2) + 0.5f) * pi_x2;
			deul[i] = eul[i] - oldrot[i];
		}
		else if (deul[i] < -pi_thresh) {
			eul[i] += floorf((-deul[i] / pi_x2) + 0.5f) * pi_x2;
			deul[i] = eul[i] - oldrot[i];
		}
	}

	/* is 1 of the axis rotations larger than 180 degrees and the other small? NO ELSE IF!! */
	if (fabsf(deul[0]) > 3.2f && fabsf(deul[1]) < 1.6f && fabsf(deul[2]) < 1.6f) {
		if (deul[0] > 0.0f) eul[0] -= pi_x2;
		else                eul[0] += pi_x2;
	}
	if (fabsf(deul[1]) > 3.2f && fabsf(deul[2]) < 1.6f && fabsf(deul[0]) < 1.6f) {
		if (deul[1] > 0.0f) eul[1] -= pi_x2;
		else                eul[1] += pi_x2;
	}
	if (fabsf(deul[2]) > 3.2f && fabsf(deul[0]) < 1.6f && fabsf(deul[1]) < 1.6f) {
		if (deul[2] > 0.0f) eul[2] -= pi_x2;
		else                eul[2] += pi_x2;
	}

#undef PI_THRESH
#undef PI_2F
}

/* uses 2 methods to retrieve eulers, and picks the closest */

/* XYZ order */
void mat3_to_compatible_eul(float eul[3], const float oldrot[3], float mat[][3])
{
	float eul1[3], eul2[3];
	float d1, d2;

	mat3_to_eul2(mat, eul1, eul2);

	compatible_eul(eul1, oldrot);
	compatible_eul(eul2, oldrot);

	d1 = fabsf(eul1[0] - oldrot[0]) + fabsf(eul1[1] - oldrot[1]) + fabsf(eul1[2] - oldrot[2]);
	d2 = fabsf(eul2[0] - oldrot[0]) + fabsf(eul2[1] - oldrot[1]) + fabsf(eul2[2] - oldrot[2]);

	/* return best, which is just the one with lowest difference */
	if (d1 > d2) {
		copy_v3_v3(eul, eul2);
	}
	else {
		copy_v3_v3(eul, eul1);
	}

}

/************************** Arbitrary Order Eulers ***************************/

/* Euler Rotation Order Code:
 * was adapted from
 *      ANSI C code from the article
 *      "Euler Angle Conversion"
 *      by Ken Shoemake, shoemake@graphics.cis.upenn.edu
 *      in "Graphics Gems IV", Academic Press, 1994
 * for use in Blender
 */

/* Type for rotation order info - see wiki for derivation details */
typedef struct RotOrderInfo {
	short axis[3];
	short parity; /* parity of axis permutation (even=0, odd=1) - 'n' in original code */
} RotOrderInfo;

/* Array of info for Rotation Order calculations
 * WARNING: must be kept in same order as eEulerRotationOrders
 */
static const RotOrderInfo rotOrders[] = {
	/* i, j, k, n */
	{{0, 1, 2}, 0}, /* XYZ */
	{{0, 2, 1}, 1}, /* XZY */
	{{1, 0, 2}, 1}, /* YXZ */
	{{1, 2, 0}, 0}, /* YZX */
	{{2, 0, 1}, 0}, /* ZXY */
	{{2, 1, 0}, 1}  /* ZYX */
};

/* Get relevant pointer to rotation order set from the array
 * NOTE: since we start at 1 for the values, but arrays index from 0,
 *		 there is -1 factor involved in this process...
 */
#define GET_ROTATIONORDER_INFO(order) (assert(order >= 0 && order <= 6), (order < 1) ? &rotOrders[0] : &rotOrders[(order) - 1])

/* Construct quaternion from Euler angles (in radians). */
void eulO_to_quat(float q[4], const float e[3], const short order)
{
	const RotOrderInfo *R = GET_ROTATIONORDER_INFO(order);
	short i = R->axis[0], j = R->axis[1], k = R->axis[2];
	double ti, tj, th, ci, cj, ch, si, sj, sh, cc, cs, sc, ss;
	double a[3];

	ti = e[i] * 0.5f;
	tj = e[j] * (R->parity ? -0.5f : 0.5f);
	th = e[k] * 0.5f;

	ci = cos(ti);
	cj = cos(tj);
	ch = cos(th);
	si = sin(ti);
	sj = sin(tj);
	sh = sin(th);

	cc = ci * ch;
	cs = ci * sh;
	sc = si * ch;
	ss = si * sh;

	a[i] = cj * sc - sj * cs;
	a[j] = cj * ss + sj * cc;
	a[k] = cj * cs - sj * sc;

	q[0] = cj * cc + sj * ss;
	q[1] = a[0];
	q[2] = a[1];
	q[3] = a[2];

	if (R->parity) q[j + 1] = -q[j + 1];
}

/* Convert quaternion to Euler angles (in radians). */
void quat_to_eulO(float e[3], short const order, const float q[4])
{
	float M[3][3];

	quat_to_mat3(M, q);
	mat3_to_eulO(e, order, M);
}

/* Construct 3x3 matrix from Euler angles (in radians). */
void eulO_to_mat3(float M[3][3], const float e[3], const short order)
{
	const RotOrderInfo *R = GET_ROTATIONORDER_INFO(order);
	short i = R->axis[0], j = R->axis[1], k = R->axis[2];
	double ti, tj, th, ci, cj, ch, si, sj, sh, cc, cs, sc, ss;

	if (R->parity) {
		ti = -e[i];
		tj = -e[j];
		th = -e[k];
	}
	else {
		ti = e[i];
		tj = e[j];
		th = e[k];
	}

	ci = cos(ti);
	cj = cos(tj);
	ch = cos(th);
	si = sin(ti);
	sj = sin(tj);
	sh = sin(th);

	cc = ci * ch;
	cs = ci * sh;
	sc = si * ch;
	ss = si * sh;

	M[i][i] = cj * ch;
	M[j][i] = sj * sc - cs;
	M[k][i] = sj * cc + ss;
	M[i][j] = cj * sh;
	M[j][j] = sj * ss + cc;
	M[k][j] = sj * cs - sc;
	M[i][k] = -sj;
	M[j][k] = cj * si;
	M[k][k] = cj * ci;
}

/* returns two euler calculation methods, so we can pick the best */
static void mat3_to_eulo2(float M[3][3], float *e1, float *e2, const short order)
{
	const RotOrderInfo *R = GET_ROTATIONORDER_INFO(order);
	short i = R->axis[0], j = R->axis[1], k = R->axis[2];
	float m[3][3];
	double cy;

	/* process the matrix first */
	copy_m3_m3(m, M);
	normalize_m3(m);

	cy = sqrt(m[i][i] * m[i][i] + m[i][j] * m[i][j]);

	if (cy > 16.0 * (double)FLT_EPSILON) {
		e1[i] = atan2(m[j][k], m[k][k]);
		e1[j] = atan2(-m[i][k], cy);
		e1[k] = atan2(m[i][j], m[i][i]);

		e2[i] = atan2(-m[j][k], -m[k][k]);
		e2[j] = atan2(-m[i][k], -cy);
		e2[k] = atan2(-m[i][j], -m[i][i]);
	}
	else {
		e1[i] = atan2(-m[k][j], m[j][j]);
		e1[j] = atan2(-m[i][k], cy);
		e1[k] = 0;

		copy_v3_v3(e2, e1);
	}

	if (R->parity) {
		e1[0] = -e1[0];
		e1[1] = -e1[1];
		e1[2] = -e1[2];

		e2[0] = -e2[0];
		e2[1] = -e2[1];
		e2[2] = -e2[2];
	}
}

/* Construct 4x4 matrix from Euler angles (in radians). */
void eulO_to_mat4(float M[4][4], const float e[3], const short order)
{
	float m[3][3];

	/* for now, we'll just do this the slow way (i.e. copying matrices) */
	normalize_m3(m);
	eulO_to_mat3(m, e, order);
	copy_m4_m3(M, m);
}

/* Convert 3x3 matrix to Euler angles (in radians). */
void mat3_to_eulO(float eul[3], const short order, float M[3][3])
{
	float eul1[3], eul2[3];

	mat3_to_eulo2(M, eul1, eul2, order);

	/* return best, which is just the one with lowest values it in */
	if (fabsf(eul1[0]) + fabsf(eul1[1]) + fabsf(eul1[2]) > fabsf(eul2[0]) + fabsf(eul2[1]) + fabsf(eul2[2])) {
		copy_v3_v3(eul, eul2);
	}
	else {
		copy_v3_v3(eul, eul1);
	}
}

/* Convert 4x4 matrix to Euler angles (in radians). */
void mat4_to_eulO(float e[3], const short order, float M[4][4])
{
	float m[3][3];

	/* for now, we'll just do this the slow way (i.e. copying matrices) */
	copy_m3_m4(m, M);
	normalize_m3(m);
	mat3_to_eulO(e, order, m);
}

/* uses 2 methods to retrieve eulers, and picks the closest */
void mat3_to_compatible_eulO(float eul[3], float oldrot[3], const short order, float mat[3][3])
{
	float eul1[3], eul2[3];
	float d1, d2;

	mat3_to_eulo2(mat, eul1, eul2, order);

	compatible_eul(eul1, oldrot);
	compatible_eul(eul2, oldrot);

	d1 = fabsf(eul1[0] - oldrot[0]) + fabsf(eul1[1] - oldrot[1]) + fabsf(eul1[2] - oldrot[2]);
	d2 = fabsf(eul2[0] - oldrot[0]) + fabsf(eul2[1] - oldrot[1]) + fabsf(eul2[2] - oldrot[2]);

	/* return best, which is just the one with lowest difference */
	if (d1 > d2)
		copy_v3_v3(eul, eul2);
	else
		copy_v3_v3(eul, eul1);
}

void mat4_to_compatible_eulO(float eul[3], float oldrot[3], const short order, float M[4][4])
{
	float m[3][3];

	/* for now, we'll just do this the slow way (i.e. copying matrices) */
	copy_m3_m4(m, M);
	normalize_m3(m);
	mat3_to_compatible_eulO(eul, oldrot, order, m);
}
/* rotate the given euler by the given angle on the specified axis */
/* NOTE: is this safe to do with different axis orders? */

void rotate_eulO(float beul[3], const short order, char axis, float ang)
{
	float eul[3], mat1[3][3], mat2[3][3], totmat[3][3];

	assert(axis >= 'X' && axis <= 'Z');

	eul[0] = eul[1] = eul[2] = 0.0f;
	if (axis == 'X')
		eul[0] = ang;
	else if (axis == 'Y')
		eul[1] = ang;
	else
		eul[2] = ang;

	eulO_to_mat3(mat1, eul, order);
	eulO_to_mat3(mat2, beul, order);

	mul_m3_m3m3(totmat, mat2, mat1);

	mat3_to_eulO(beul, order, totmat);
}

/* the matrix is written to as 3 axis vectors */
void eulO_to_gimbal_axis(float gmat[][3], const float eul[3], const short order)
{
	const RotOrderInfo *R = GET_ROTATIONORDER_INFO(order);

	float mat[3][3];
	float teul[3];

	/* first axis is local */
	eulO_to_mat3(mat, eul, order);
	copy_v3_v3(gmat[R->axis[0]], mat[R->axis[0]]);

	/* second axis is local minus first rotation */
	copy_v3_v3(teul, eul);
	teul[R->axis[0]] = 0;
	eulO_to_mat3(mat, teul, order);
	copy_v3_v3(gmat[R->axis[1]], mat[R->axis[1]]);


	/* Last axis is global */
	gmat[R->axis[2]][0] = 0;
	gmat[R->axis[2]][1] = 0;
	gmat[R->axis[2]][2] = 0;
	gmat[R->axis[2]][R->axis[2]] = 1;
}

/******************************* Dual Quaternions ****************************/

/**
 * Conversion routines between (regular quaternion, translation) and
 * dual quaternion.
 *
 * Version 1.0.0, February 7th, 2007
 *
 * Copyright (C) 2006-2007 University of Dublin, Trinity College, All Rights
 * Reserved
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the author(s) be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * \author Ladislav Kavan, kavanl@cs.tcd.ie
 *
 * Changes for Blender:
 * - renaming, style changes and optimization's
 * - added support for scaling
 */

void mat4_to_dquat(DualQuat *dq, float basemat[][4], float mat[][4])
{
	float *t, *q, dscale[3], scale[3], basequat[4];
	float baseRS[4][4], baseinv[4][4], baseR[4][4], baseRinv[4][4];
	float R[4][4], S[4][4];

	/* split scaling and rotation, there is probably a faster way to do
	 * this, it's done like this now to correctly get negative scaling */
	mult_m4_m4m4(baseRS, mat, basemat);
	mat4_to_size(scale, baseRS);

	dscale[0] = scale[0] - 1.0f;
	dscale[1] = scale[1] - 1.0f;
	dscale[2] = scale[2] - 1.0f;

	if ((determinant_m4(mat) < 0.0f) || len_v3(dscale) > 1e-4f) {
		/* extract R and S  */
		float tmp[4][4];

		/* extra orthogonalize, to avoid flipping with stretched bones */
		copy_m4_m4(tmp, baseRS);
		orthogonalize_m4(tmp, 1);
		mat4_to_quat(basequat, tmp);

		quat_to_mat4(baseR, basequat);
		copy_v3_v3(baseR[3], baseRS[3]);

		invert_m4_m4(baseinv, basemat);
		mult_m4_m4m4(R, baseR, baseinv);

		invert_m4_m4(baseRinv, baseR);
		mult_m4_m4m4(S, baseRinv, baseRS);

		/* set scaling part */
		mul_serie_m4(dq->scale, basemat, S, baseinv, NULL, NULL, NULL, NULL, NULL);
		dq->scale_weight = 1.0f;
	}
	else {
		/* matrix does not contain scaling */
		copy_m4_m4(R, mat);
		dq->scale_weight = 0.0f;
	}

	/* non-dual part */
	mat4_to_quat(dq->quat, R);

	/* dual part */
	t = R[3];
	q = dq->quat;
	dq->trans[0] = -0.5f * ( t[0] * q[1] + t[1] * q[2] + t[2] * q[3]);
	dq->trans[1] =  0.5f * ( t[0] * q[0] + t[1] * q[3] - t[2] * q[2]);
	dq->trans[2] =  0.5f * (-t[0] * q[3] + t[1] * q[0] + t[2] * q[1]);
	dq->trans[3] =  0.5f * ( t[0] * q[2] - t[1] * q[1] + t[2] * q[0]);
}

void dquat_to_mat4(float mat[][4], DualQuat *dq)
{
	float len, *t, q0[4];

	/* regular quaternion */
	copy_qt_qt(q0, dq->quat);

	/* normalize */
	len = sqrtf(dot_qtqt(q0, q0));
	if (len != 0.0f)
		mul_qt_fl(q0, 1.0f / len);

	/* rotation */
	quat_to_mat4(mat, q0);

	/* translation */
	t = dq->trans;
	mat[3][0] = 2.0f * (-t[0] * q0[1] + t[1] * q0[0] - t[2] * q0[3] + t[3] * q0[2]);
	mat[3][1] = 2.0f * (-t[0] * q0[2] + t[1] * q0[3] + t[2] * q0[0] - t[3] * q0[1]);
	mat[3][2] = 2.0f * (-t[0] * q0[3] - t[1] * q0[2] + t[2] * q0[1] + t[3] * q0[0]);

	/* note: this does not handle scaling */
}

void add_weighted_dq_dq(DualQuat *dqsum, DualQuat *dq, float weight)
{
	int flipped = 0;

	/* make sure we interpolate quats in the right direction */
	if (dot_qtqt(dq->quat, dqsum->quat) < 0) {
		flipped = 1;
		weight = -weight;
	}

	/* interpolate rotation and translation */
	dqsum->quat[0] += weight * dq->quat[0];
	dqsum->quat[1] += weight * dq->quat[1];
	dqsum->quat[2] += weight * dq->quat[2];
	dqsum->quat[3] += weight * dq->quat[3];

	dqsum->trans[0] += weight * dq->trans[0];
	dqsum->trans[1] += weight * dq->trans[1];
	dqsum->trans[2] += weight * dq->trans[2];
	dqsum->trans[3] += weight * dq->trans[3];

	/* interpolate scale - but only if needed */
	if (dq->scale_weight) {
		float wmat[4][4];

		if (flipped) /* we don't want negative weights for scaling */
			weight = -weight;

		copy_m4_m4(wmat, dq->scale);
		mul_m4_fl(wmat, weight);
		add_m4_m4m4(dqsum->scale, dqsum->scale, wmat);
		dqsum->scale_weight += weight;
	}
}

void normalize_dq(DualQuat *dq, float totweight)
{
	float scale = 1.0f / totweight;

	mul_qt_fl(dq->quat, scale);
	mul_qt_fl(dq->trans, scale);

	if (dq->scale_weight) {
		float addweight = totweight - dq->scale_weight;

		if (addweight) {
			dq->scale[0][0] += addweight;
			dq->scale[1][1] += addweight;
			dq->scale[2][2] += addweight;
			dq->scale[3][3] += addweight;
		}

		mul_m4_fl(dq->scale, scale);
		dq->scale_weight = 1.0f;
	}
}

void mul_v3m3_dq(float co[3], float mat[][3], DualQuat *dq)
{
	float M[3][3], t[3], scalemat[3][3], len2;
	float w = dq->quat[0], x = dq->quat[1], y = dq->quat[2], z = dq->quat[3];
	float t0 = dq->trans[0], t1 = dq->trans[1], t2 = dq->trans[2], t3 = dq->trans[3];

	/* rotation matrix */
	M[0][0] = w * w + x * x - y * y - z * z;
	M[1][0] = 2 * (x * y - w * z);
	M[2][0] = 2 * (x * z + w * y);

	M[0][1] = 2 * (x * y + w * z);
	M[1][1] = w * w + y * y - x * x - z * z;
	M[2][1] = 2 * (y * z - w * x);

	M[0][2] = 2 * (x * z - w * y);
	M[1][2] = 2 * (y * z + w * x);
	M[2][2] = w * w + z * z - x * x - y * y;

	len2 = dot_qtqt(dq->quat, dq->quat);
	if (len2 > 0.0f)
		len2 = 1.0f / len2;

	/* translation */
	t[0] = 2 * (-t0 * x + w * t1 - t2 * z + y * t3);
	t[1] = 2 * (-t0 * y + t1 * z - x * t3 + w * t2);
	t[2] = 2 * (-t0 * z + x * t2 + w * t3 - t1 * y);

	/* apply scaling */
	if (dq->scale_weight)
		mul_m4_v3(dq->scale, co);

	/* apply rotation and translation */
	mul_m3_v3(M, co);
	co[0] = (co[0] + t[0]) * len2;
	co[1] = (co[1] + t[1]) * len2;
	co[2] = (co[2] + t[2]) * len2;

	/* compute crazyspace correction mat */
	if (mat) {
		if (dq->scale_weight) {
			copy_m3_m4(scalemat, dq->scale);
			mul_m3_m3m3(mat, M, scalemat);
		}
		else
			copy_m3_m3(mat, M);
		mul_m3_fl(mat, len2);
	}
}

void copy_dq_dq(DualQuat *dq1, DualQuat *dq2)
{
	memcpy(dq1, dq2, sizeof(DualQuat));
}

/* axis matches eTrackToAxis_Modes */
void quat_apply_track(float quat[4], short axis, short upflag)
{
	/* rotations are hard coded to match vec_to_quat */
	const float quat_track[][4] = {
		{M_SQRT1_2, 0.0, -M_SQRT1_2, 0.0}, /* pos-y90 */
		{0.5, 0.5, 0.5, 0.5}, /* Quaternion((1,0,0), radians(90)) * Quaternion((0,1,0), radians(90)) */
		{M_SQRT1_2, 0.0, 0.0, M_SQRT1_2}, /* pos-z90 */
		{M_SQRT1_2, 0.0, M_SQRT1_2, 0.0}, /* neg-y90 */
		{0.5, -0.5, -0.5, 0.5}, /* Quaternion((1,0,0), radians(-90)) * Quaternion((0,1,0), radians(-90)) */
		{0.0, M_SQRT1_2, M_SQRT1_2, 0.0} /* no rotation */
	};

	assert(axis >= 0 && axis <= 5);
	assert(upflag >= 0 && upflag <= 2);

	mul_qt_qtqt(quat, quat, quat_track[axis]);

	if (axis > 2)
		axis = axis - 3;

	/* there are 2 possible up-axis for each axis used, the 'quat_track' applies so the first
	 * up axis is used X->Y, Y->X, Z->X, if this first up axis isn used then rotate 90d
	 * the strange bit shift below just find the low axis {X:Y, Y:X, Z:X} */
	if (upflag != (2 - axis) >> 1) {
		float q[4] = {M_SQRT1_2, 0.0, 0.0, 0.0}; /* assign 90d rotation axis */
		q[axis + 1] = ((axis == 1)) ? M_SQRT1_2 : -M_SQRT1_2; /* flip non Y axis */
		mul_qt_qtqt(quat, quat, q);
	}
}

void vec_apply_track(float vec[3], short axis)
{
	float tvec[3];

	assert(axis >= 0 && axis <= 5);

	copy_v3_v3(tvec, vec);

	switch (axis) {
		case 0: /* pos-x */
			/* vec[0] =  0.0; */
			vec[1] = tvec[2];
			vec[2] = -tvec[1];
			break;
		case 1: /* pos-y */
			/* vec[0] = tvec[0]; */
			/* vec[1] =  0.0; */
			/* vec[2] = tvec[2]; */
			break;
		case 2: /* pos-z */
			/* vec[0] = tvec[0]; */
			/* vec[1] = tvec[1]; */
			/* vec[2] =  0.0; */
			break;
		case 3: /* neg-x */
			/* vec[0] =  0.0; */
			vec[1] = tvec[2];
			vec[2] = -tvec[1];
			break;
		case 4: /* neg-y */
			vec[0] = -tvec[2];
			/* vec[1] =  0.0; */
			vec[2] = tvec[0];
			break;
		case 5: /* neg-z */
			vec[0] = -tvec[0];
			vec[1] = -tvec[1];
			/* vec[2] =  0.0; */
			break;
	}
}

/* lens/angle conversion (radians) */
float focallength_to_fov(float focal_length, float sensor)
{
	return 2.0f * atanf((sensor / 2.0f) / focal_length);
}

float fov_to_focallength(float hfov, float sensor)
{
	return (sensor / 2.0f) / tanf(hfov * 0.5f);
}

/* 'mod_inline(-3,4)= 1', 'fmod(-3,4)= -3' */
static float mod_inline(float a, float b)
{
	return a - (b * floorf(a / b));
}

float angle_wrap_rad(float angle)
{
	return mod_inline(angle + (float)M_PI, (float)M_PI * 2.0f) - (float)M_PI;
}

float angle_wrap_deg(float angle)
{
	return mod_inline(angle + 180.0f, 360.0f) - 180.0f;
}
