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

#include "COM_GaussianBokehBlurOperation.h"
#include "BLI_math.h"

extern "C" {
	#include "RE_pipeline.h"
}

GaussianBokehBlurOperation::GaussianBokehBlurOperation() : BlurBaseOperation(COM_DT_COLOR)
{
	this->m_gausstab = NULL;
}

void *GaussianBokehBlurOperation::initializeTileData(rcti *rect, MemoryBuffer **memoryBuffers)
{
	lockMutex();
	if (!this->m_sizeavailable) {
		updateGauss(memoryBuffers);
	}
	void *buffer = getInputOperation(0)->initializeTileData(NULL, memoryBuffers);
	unlockMutex();
	return buffer;
}

void GaussianBokehBlurOperation::initExecution()
{
	BlurBaseOperation::initExecution();

	initMutex();

	if (this->m_sizeavailable) {
		updateGauss(NULL);
	}
}

void GaussianBokehBlurOperation::updateGauss(MemoryBuffer **memoryBuffers)
{
	if (this->m_gausstab == NULL) {
		float radxf;
		float radyf;
		int n;
		float *dgauss;
		float *ddgauss;
		float val;
		int j, i;
		const float width = this->getWidth();
		const float height = this->getHeight();
		if (!this->m_sizeavailable) {
			updateSize(memoryBuffers);
		}
		radxf = this->m_size * (float)this->m_data->sizex;
		if (radxf > width / 2.0f)
			radxf = width / 2.0f;
		else if (radxf < 1.0f)
			radxf = 1.0f;
	
		/* vertical */
		radyf = this->m_size * (float)this->m_data->sizey;
		if (radyf > height / 2.0f)
			radyf = height / 2.0f;
		else if (radyf < 1.0f)
			radyf = 1.0f;
	
		this->m_radx = ceil(radxf);
		this->m_rady = ceil(radyf);
	
		n = (2 * this->m_radx + 1) * (2 * this->m_rady + 1);
	
		/* create a full filter image */
		ddgauss = new float[n];
		dgauss = ddgauss;
		val = 0.0f;
		for (j = -this->m_rady; j <= this->m_rady; j++) {
			for (i = -this->m_radx; i <= this->m_radx; i++, dgauss++) {
				float fj = (float)j / radyf;
				float fi = (float)i / radxf;
				float dist = sqrt(fj * fj + fi * fi);
				*dgauss = RE_filter_value(this->m_data->filtertype, dist);
				
				val += *dgauss;
			}
		}
		if (val != 0.0f) {
			val = 1.0f / val;
			for (j = n - 1; j >= 0; j--)
				ddgauss[j] *= val;
		}
		else ddgauss[4] = 1.0f;
		
		this->m_gausstab = ddgauss;
	}
}

void GaussianBokehBlurOperation::executePixel(float *color, int x, int y, MemoryBuffer *inputBuffers[], void *data)
{
	float tempColor[4];
	tempColor[0] = 0;
	tempColor[1] = 0;
	tempColor[2] = 0;
	tempColor[3] = 0;
	float multiplier_accum = 0;
	MemoryBuffer *inputBuffer = (MemoryBuffer *)data;
	float *buffer = inputBuffer->getBuffer();
	int bufferwidth = inputBuffer->getWidth();
	int bufferstartx = inputBuffer->getRect()->xmin;
	int bufferstarty = inputBuffer->getRect()->ymin;

	int miny = y - this->m_rady;
	int maxy = y + this->m_rady;
	int minx = x - this->m_radx;
	int maxx = x + this->m_radx;
	miny = max(miny, inputBuffer->getRect()->ymin);
	minx = max(minx, inputBuffer->getRect()->xmin);
	maxy = min(maxy, inputBuffer->getRect()->ymax);
	maxx = min(maxx, inputBuffer->getRect()->xmax);

	int index;
	int step = QualityStepHelper::getStep();
	int offsetadd = QualityStepHelper::getOffsetAdd();
	for (int ny = miny; ny < maxy; ny += step) {
		index = ((ny - y) + this->m_rady) * (this->m_radx * 2 + 1) + (minx - x + this->m_radx);
		int bufferindex = ((minx - bufferstartx) * 4) + ((ny - bufferstarty) * 4 * bufferwidth);
		for (int nx = minx; nx < maxx; nx += step) {
			const float multiplier = this->m_gausstab[index];
			madd_v4_v4fl(tempColor, &buffer[bufferindex], multiplier);
			multiplier_accum += multiplier;
			index += step;
			bufferindex += offsetadd;
		}
	}

	mul_v4_v4fl(color, tempColor, 1.0f / multiplier_accum);
}

void GaussianBokehBlurOperation::deinitExecution()
{
	BlurBaseOperation::deinitExecution();
	delete [] this->m_gausstab;
	this->m_gausstab = NULL;

	deinitMutex();
}

bool GaussianBokehBlurOperation::determineDependingAreaOfInterest(rcti *input, ReadBufferOperation *readOperation, rcti *output)
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
			newInput.xmin = 0;
			newInput.ymin = 0;
			newInput.xmax = this->getWidth();
			newInput.ymax = this->getHeight();
		}
		else {
			int addx = this->m_radx;
			int addy = this->m_rady;
			newInput.xmax = input->xmax + addx;
			newInput.xmin = input->xmin - addx;
			newInput.ymax = input->ymax + addy;
			newInput.ymin = input->ymin - addy;

		}
		return BlurBaseOperation::determineDependingAreaOfInterest(&newInput, readOperation, output);
	}
}
