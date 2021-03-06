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

#ifndef __BLI_MATH_MATRIX_H__
#define __BLI_MATH_MATRIX_H__

/** \file BLI_math_matrix.h
 *  \ingroup bli
 */

#ifdef __cplusplus
extern "C" {
#endif

/********************************* Init **************************************/

#define MAT4_UNITY  {      \
	{ 1.0, 0.0, 0.0, 0.0}, \
	{ 0.0, 1.0, 0.0, 0.0}, \
	{ 0.0, 0.0, 1.0, 0.0}, \
	{ 0.0, 0.0, 0.0, 1.0}  \
}

#define MAT3_UNITY  { \
	{ 1.0, 0.0, 0.0}, \
	{ 0.0, 1.0, 0.0}, \
	{ 0.0, 0.0, 1.0}  \
}

void zero_m3(float R[3][3]);
void zero_m4(float R[4][4]);

void unit_m3(float R[3][3]);
void unit_m4(float R[4][4]);

void copy_m3_m3(float R[3][3], float A[3][3]);
void copy_m4_m4(float R[4][4], float A[4][4]);
void copy_m3_m4(float R[3][3], float A[4][4]);
void copy_m4_m3(float R[4][4], float A[3][3]);

void swap_m3m3(float A[3][3], float B[3][3]);
void swap_m4m4(float A[4][4], float B[4][4]);

/******************************** Arithmetic *********************************/

void add_m3_m3m3(float R[3][3], float A[3][3], float B[3][3]);
void add_m4_m4m4(float R[4][4], float A[4][4], float B[4][4]);

void sub_m3_m3m3(float R[3][3], float A[3][3], float B[3][3]);
void sub_m4_m4m4(float R[4][4], float A[4][4], float B[4][4]);
void mult_m4_m4m4_q(float m1[4][4], const float m3[4][4], const float m2[4][4]);
void mult_m4_m3m4_q(float m1[4][4], float m3[4][4], float m2[3][3]);

void mul_m3_m3m3(float R[3][3], float A[3][3], float B[3][3]);
void mul_m4_m3m4(float R[4][4], float A[3][3], float B[4][4]);
void mul_m4_m4m3(float R[4][4], float A[4][4], float B[3][3]);
/* note: the A,B arguments are reversed compared to previous mul_m4_m4m4
 * function, for consistency with above functions & math notation. */
void mult_m4_m4m4(float R[4][4], float A[4][4], float B[4][4]);
void mult_m3_m3m4(float R[3][3], float A[4][4], float B[3][3]);

void mul_serie_m3(float R[3][3],
                  float M1[3][3], float M2[3][3], float M3[3][3], float M4[3][3],
                  float M5[3][3], float M6[3][3], float M7[3][3], float M8[3][3]);
void mul_serie_m4(float R[4][4],
                  float M1[4][4], float M2[4][4], float M3[4][4], float M4[4][4],
                  float M5[4][4], float M6[4][4], float M7[4][4], float M8[4][4]);

void mul_m4_v3(float M[4][4], float r[3]);
void mul_v3_m4v3(float r[3], float M[4][4], const float v[3]);
void mul_v4_m4v3(float r[4], const float M[4][4], const float v[3]);
void mul_v3_m4v3_q(float r[3], float M[4][4], const float v[3]);
void mul_mat3_m4_v3(float M[4][4], float r[3]);
void mul_m4_v4(const float M[4][4], float r[4]);
void mul_v4_m4v4(float r[4], const float M[4][4], float v[4]);
void mul_project_m4_v3(float M[4][4], float vec[3]);

void mul_m3_v3(float M[3][3], float r[3]);
void mul_v3_m3v3(float r[3], float M[3][3], float a[3]);
void mul_transposed_m3_v3(float M[3][3], float r[3]);
void mul_m3_v3_double(float M[3][3], double r[3]);

void mul_m3_fl(float R[3][3], float f);
void mul_m4_fl(float R[4][4], float f);
void mul_mat3_m4_fl(float R[4][4], float f);

int invert_m3_ex(float m[3][3], const float epsilon);
int invert_m3_m3_ex(float m1[3][3], float m2[3][3], const float epsilon);

int invert_m3(float R[3][3]);
int invert_m3_m3(float R[3][3], float A[3][3]);
int invert_m4(float R[4][4]);
int invert_m4_m4(float R[4][4], float A[4][4]);

/* double ariphmetics */
void mul_m4_v4d(float M[4][4], double r[4]);
void mul_v4d_m4v4d(double r[4], float M[4][4], double v[4]);


/****************************** Linear Algebra *******************************/

void transpose_m3(float R[3][3]);
void transpose_m4(float R[4][4]);

void normalize_m3(float R[3][3]);
void normalize_m3_m3(float R[3][3], float A[3][3]);
void normalize_m4(float R[4][4]);
void normalize_m4_m4(float R[4][4], float A[4][4]);

void orthogonalize_m3(float R[3][3], int axis);
void orthogonalize_m4(float R[4][4], int axis);

int is_orthogonal_m3(float mat[3][3]);
int is_orthogonal_m4(float mat[4][4]);
int is_orthonormal_m3(float mat[3][3]);
int is_orthonormal_m4(float mat[4][4]);

int is_uniform_scaled_m3(float mat[3][3]);

void adjoint_m2_m2(float R[2][2], float A[2][2]);
void adjoint_m3_m3(float R[3][3], float A[3][3]);
void adjoint_m4_m4(float R[4][4], float A[4][4]);

float determinant_m2(float a, float b,
                     float c, float d);
float determinant_m3(float a, float b, float c,
                     float d, float e, float f,
                     float g, float h, float i);
float determinant_m4(float A[4][4]);

void svd_m4(float U[4][4], float s[4], float V[4][4], float A[4][4]);
void pseudoinverse_m4_m4(float Ainv[4][4], float A[4][4], float epsilon);

/****************************** Transformations ******************************/

void scale_m3_fl(float R[3][3], float scale);
void scale_m4_fl(float R[4][4], float scale);

void scale_m4(float m[][4], float x, float y, float z);

float mat3_to_scale(float M[3][3]);
float mat4_to_scale(float M[4][4]);

void size_to_mat3(float R[3][3], const float size[3]);
void size_to_mat4(float R[4][4], const float size[3]);

void mat3_to_size(float r[3], float M[3][3]);
void mat4_to_size(float r[3], float M[4][4]);

void translate_m4(float mat[4][4], float tx, float ty, float tz);
void rotate_m4(float mat[4][4], const char axis, const float angle);
void rotate_m4_right(float mat[4][4], const char axis);


void mat3_to_rot_size(float rot[3][3], float size[3], float mat3[][3]);
void mat4_to_loc_rot_size(float loc[3], float rot[3][3], float size[3], float wmat[][4]);

void loc_eul_size_to_mat4(float R[4][4],
                          const float loc[3], const float eul[3], const float size[3]);
void loc_eulO_size_to_mat4(float R[4][4],
                           const float loc[3], const float eul[3], const float size[3], const short order);
void loc_quat_size_to_mat4(float R[4][4],
                           const float loc[3], const float quat[4], const float size[3]);
void loc_axisangle_size_to_mat4(float R[4][4],
                                const float loc[3], const float axis[4], const float angle, const float size[3]);

void blend_m3_m3m3(float R[3][3], float A[3][3], float B[3][3], const float t);
void blend_m4_m4m4(float R[4][4], float A[4][4], float B[4][4], const float t);

int is_negative_m3(float mat[3][3]);
int is_negative_m4(float mat[4][4]);

/******************************** Projections ********************************/

void mat4_ortho_set(float m[4][4], float left, float right, float bottom, float top, float nearVal, float farVal);
void mat4_frustum_set(float m[4][4], float left, float right, float bottom, float top, float nearVal, float farVal);

void mat4_look_from_origin(float m[4][4], float lookdir[3], float camup[3]);

/*********************************** Other ***********************************/

void print_m3(const char *str, float M[3][3]);
void print_m4(const char *str, float M[3][4]);

#ifdef __cplusplus
}
#endif

#endif /* __BLI_MATH_MATRIX_H__ */

