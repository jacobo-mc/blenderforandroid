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

#include "COM_ReadBufferOperation.h"
#include "COM_WriteBufferOperation.h"
#include "COM_defines.h"

ReadBufferOperation::ReadBufferOperation() : NodeOperation()
{
	this->addOutputSocket(COM_DT_COLOR);
	this->m_offset = 0;
}

void *ReadBufferOperation::initializeTileData(rcti *rect, MemoryBuffer **memoryBuffers)
{
	return getInputMemoryBuffer(memoryBuffers);
}

void ReadBufferOperation::determineResolution(unsigned int resolution[], unsigned int preferredResolution[])
{
	if (this->m_memoryProxy != NULL) {
		WriteBufferOperation *operation = this->m_memoryProxy->getWriteBufferOperation();
		operation->determineResolution(resolution, preferredResolution);
		operation->setResolution(resolution);

		/// @todo: may not occur!, but does with blur node
		if (this->m_memoryProxy->getExecutor()) {
			this->m_memoryProxy->getExecutor()->setResolution(resolution);
		}
	}
}
void ReadBufferOperation::executePixel(float *color, float x, float y, PixelSampler sampler, MemoryBuffer *inputBuffers[])
{
	if (inputBuffers) {
		MemoryBuffer *inputBuffer = inputBuffers[this->m_offset];
		if (inputBuffer) {
			if (sampler == COM_PS_NEAREST) {
				inputBuffer->read(color, x, y);
			}
			else {
				inputBuffer->readCubic(color, x, y);
			}
		}
	} else {
		color[0] = 0.0f;
		color[1] = 0.0f;
		color[2] = 0.0f;
		color[3] = 0.0f;
	}
}

void ReadBufferOperation::executePixel(float *color, float x, float y, float dx, float dy, MemoryBuffer *inputBuffers[])
{
	MemoryBuffer *inputBuffer = inputBuffers[this->m_offset];
	if (inputBuffer) {
		inputBuffer->readEWA(color, x, y, dx, dy);
	}
}

bool ReadBufferOperation::determineDependingAreaOfInterest(rcti *input, ReadBufferOperation *readOperation, rcti *output)
{
	if (this == readOperation) {
		BLI_init_rcti(output, input->xmin, input->xmax, input->ymin, input->ymax);
		return true;
	}
	return false;
}

void ReadBufferOperation::readResolutionFromWriteBuffer()
{
	if (this->m_memoryProxy != NULL) {
		WriteBufferOperation *operation = this->m_memoryProxy->getWriteBufferOperation();
		this->setWidth(operation->getWidth());
		this->setHeight(operation->getHeight());
	}
}