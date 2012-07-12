/*
 * Copyright 2011, Blender Foundation.
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
 * Contributor: 
 *		Jeroen Bakker 
 *		Monique Dewanchand
 */

#include "COM_GaussianXBlurOperation.h"
#include "BLI_math.h"

extern "C" {
	#include "RE_pipeline.h"
}

GaussianXBlurOperation::GaussianXBlurOperation() : BlurBaseOperation(COM_DT_COLOR)
{
	this->m_gausstab = NULL;
	this->m_rad = 0;
}

void *GaussianXBlurOperation::initializeTileData(rcti *rect, MemoryBuffer **memoryBuffers)
{
	lockMutex();
	if (!this->m_sizeavailable) {
		updateGauss(memoryBuffers);
	}
	void *buffer = getInputOperation(0)->initializeTileData(NULL, memoryBuffers);
	unlockMutex();
	return buffer;
}

void GaussianXBlurOperation::initExecution()
{
	BlurBaseOperation::initExecution();

	initMutex();

	if (this->m_sizeavailable) {
		float rad = this->m_size * this->m_data->sizex;
		if (rad < 1)
			rad = 1;

		this->m_rad = rad;
		this->m_gausstab = BlurBaseOperation::make_gausstab(rad);
	}
}

void GaussianXBlurOperation::updateGauss(MemoryBuffer **memoryBuffers)
{
	if (this->m_gausstab == NULL) {
		updateSize(memoryBuffers);
		float rad = this->m_size * this->m_data->sizex;
		if (rad < 1)
			rad = 1;

		this->m_rad = rad;
		this->m_gausstab = BlurBaseOperation::make_gausstab(rad);
	}
}

void GaussianXBlurOperation::executePixel(float *color, int x, int y, MemoryBuffer *inputBuffers[], void *data)
{
	float color_accum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	float multiplier_accum = 0.0f;
	MemoryBuffer *inputBuffer = (MemoryBuffer *)data;
	float *buffer = inputBuffer->getBuffer();
	int bufferwidth = inputBuffer->getWidth();
	int bufferstartx = inputBuffer->getRect()->xmin;
	int bufferstarty = inputBuffer->getRect()->ymin;

	int miny = y;
	int maxy = y;
	int minx = x - this->m_rad;
	int maxx = x + this->m_rad;
	miny = max(miny, inputBuffer->getRect()->ymin);
	minx = max(minx, inputBuffer->getRect()->xmin);
	maxy = min(maxy, inputBuffer->getRect()->ymax);
	maxx = min(maxx, inputBuffer->getRect()->xmax);

	int index;
	int step = getStep();
	int offsetadd = getOffsetAdd();
	int bufferindex = ((minx - bufferstartx) * 4) + ((miny - bufferstarty) * 4 * bufferwidth);
	for (int nx = minx; nx < maxx; nx += step) {
		index = (nx - x) + this->m_rad;
		const float multiplier = this->m_gausstab[index];
		madd_v4_v4fl(color_accum, &buffer[bufferindex], multiplier);
		multiplier_accum += multiplier;
		bufferindex += offsetadd;
	}
	mul_v4_v4fl(color, color_accum, 1.0f / multiplier_accum);
}

void GaussianXBlurOperation::deinitExecution()
{
	BlurBaseOperation::deinitExecution();
	delete [] this->m_gausstab;
	this->m_gausstab = NULL;

	deinitMutex();
}

bool GaussianXBlurOperation::determineDependingAreaOfInterest(rcti *input, ReadBufferOperation *readOperation, rcti *output)
{
	rcti newInput;
	rcti sizeInput;
	sizeInput.xmin = 0;
	sizeInput.ymin = 0;
	sizeInput.xmax = 5;
	sizeInput.ymax = 5;
	
	NodeOperation *operation = this->getInputOperation(1);
	if (operation->determineDependingAreaOfInterest(&sizeInput, readOperation, output)) {
		return true;
	}
	else {
		if (this->m_sizeavailable && this->m_gausstab != NULL) {
			newInput.xmax = input->xmax + this->m_rad;
			newInput.xmin = input->xmin - this->m_rad;
			newInput.ymax = input->ymax;
			newInput.ymin = input->ymin;
		}
		else {
			newInput.xmax = this->getWidth();
			newInput.xmin = 0;
			newInput.ymax = this->getHeight();
			newInput.ymin = 0;
		}
		return NodeOperation::determineDependingAreaOfInterest(&newInput, readOperation, output);
	}
}