/*
 * Copyright 2010-2026 Haiku, inc.
 * Distributed under the terms of the MIT Licence.
 *
 * Author: Adrien Destugues <pulkomandy@pulkomandy.tk>
 */


#include "Interpolate.h"

#include <MediaDefs.h>

#include "MixerDebug.h"


/*! Resampling class doing cubic Hermite interpolation (Catmull-Rom).
	Four neighbouring source samples shape each output sample. Linear
	interpolation joins two samples with a straight line, which rolls the
	highs off well below the Nyquist frequency (Haiku #9438).
*/


static float
hermite(float p0, float p1, float p2, float p3, float t)
{
	float t2 = t * t;
	float t3 = t2 * t;
	return 0.5f * ((2.0f * p1)
		+ (-p0 + p2) * t
		+ (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
		+ (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}


template<typename inType>
static float
sample_at(Interpolate* self, const char* src, int32 srcSampleOffset,
	int32 srcSampleCount, int32 index)
{
	if (srcSampleCount > 0 && index >= srcSampleCount)
		index = srcSampleCount - 1;
	if (index >= 0 && srcSampleCount > 0) {
		const char* sample = src + index * srcSampleOffset;
		return (float)(*(const inType*)sample);
	}

	// fHist[0] is sample -3, fHist[2] is sample -1.
	if (index >= -3)
		return self->fHist[index + 3];
	return self->fHist[0];
}


template<typename inType>
static void
remember_tail(Interpolate* self, const char* src, int32 srcSampleOffset,
	int32 srcSampleCount)
{
	float tail[3];
	for (int32 i = 0; i < 3; i++) {
		tail[i] = sample_at<inType>(self, src, srcSampleOffset,
			srcSampleCount, srcSampleCount - 3 + i);
	}
	self->fHist[0] = tail[0];
	self->fHist[1] = tail[1];
	self->fHist[2] = tail[2];
}


template<typename inType, typename outType, int gnum, int gden,
	int inMiddle, int outMiddle, int32 min, int32 max> static void
kernel(Resampler* object, const void *_src, int32 srcSampleOffset,
	int32 srcSampleCount, void *_dest, int32 destSampleOffset,
	int32 destSampleCount, float _gain)
{
	// Keep the start. The copy below walks its own pointer, and the tail
	// is indexed from this base. Indexing from the walked pointer reads
	// past the buffer (48 kHz into a 48 kHz mixer hits that path).
	const char* srcBase = (const char*)_src;
	char * dest = (char *)_dest;
	int32 count = destSampleCount;
	float gain = _gain * gnum / gden;
	Interpolate* self = static_cast<Interpolate*>(object);

	if (srcSampleCount == destSampleCount) {
		// optimized case for no resampling
		const char* src = srcBase;
		while (count--) {
			float tmp = ((*(const inType*)src) - inMiddle) * gain + outMiddle;
			if (tmp <= min)
				*(outType *)dest = min;
			else if (tmp >= max)
				*(outType *)dest = max;
			else
				*(outType *)dest = (outType)tmp;
			src += srcSampleOffset;
			dest += destSampleOffset;
		}
	} else if (srcSampleCount > 0 && destSampleCount > 0) {
		const char* src = srcBase;
		double step = (double)srcSampleCount / (double)destSampleCount;
		double pos = 0.0;

		while (count--) {
			int32 i1 = (int32)pos;
			float t = (float)(pos - (double)i1);
			float sample = hermite(
				sample_at<inType>(self, src, srcSampleOffset, srcSampleCount,
					i1 - 1),
				sample_at<inType>(self, src, srcSampleOffset, srcSampleCount,
					i1),
				sample_at<inType>(self, src, srcSampleOffset, srcSampleCount,
					i1 + 1),
				sample_at<inType>(self, src, srcSampleOffset, srcSampleCount,
					i1 + 2),
				t);
			float tmp = gain * (sample - inMiddle) + outMiddle;
			if (tmp <= min)
				*(outType *)dest = min;
			else if (tmp >= max)
				*(outType *)dest = max;
			else
				*(outType *)dest = (outType)tmp;

			dest += destSampleOffset;
			pos += step;
		}
	}

	remember_tail<inType>(self, srcBase, srcSampleOffset, srcSampleCount);
}


Interpolate::Interpolate(uint32 src_format, uint32 dst_format)
	:
	Resampler()
{
	float middle = 0.0f;
	if (src_format == media_raw_audio_format::B_AUDIO_UCHAR)
		middle = 128.0f;
	fHist[0] = fHist[1] = fHist[2] = middle;

	if (dst_format == media_raw_audio_format::B_AUDIO_FLOAT) {
		switch (src_format) {
			case media_raw_audio_format::B_AUDIO_FLOAT:
				fFunc = &kernel<float, float, 1, 1, 0, 0, -1, 1>;
				return;
			case media_raw_audio_format::B_AUDIO_INT:
				fFunc = &kernel<int32, float, 1, INT32_MAX, 0, 0, -1, 1>;
				return;
			case media_raw_audio_format::B_AUDIO_SHORT:
				fFunc = &kernel<int16, float, 1, INT16_MAX, 0, 0, -1, 1>;
				return;
			case media_raw_audio_format::B_AUDIO_CHAR:
				fFunc = &kernel<int8, float, 1, INT8_MAX, 0, 0, -1, 1>;
				return;
			case media_raw_audio_format::B_AUDIO_UCHAR:
				fFunc = &kernel<uint8, float, 2, UINT8_MAX, 128, 0, -1, 1>;
				return;
			default:
				ERROR("Resampler::Resampler: unknown source format 0x%x\n",
					src_format);
				return;
		}
	}

	if (src_format == media_raw_audio_format::B_AUDIO_FLOAT) {
		switch (dst_format) {
			// float=>float already handled above
			case media_raw_audio_format::B_AUDIO_INT:
				fFunc = &kernel<float, int32, INT32_MAX, 1, 0, 0,
					INT32_MIN, INT32_MAX>;
				return;
			case media_raw_audio_format::B_AUDIO_SHORT:
				fFunc = &kernel<float, int16, INT16_MAX, 1, 0, 0,
					INT16_MIN, INT16_MAX>;
				return;
			case media_raw_audio_format::B_AUDIO_CHAR:
				fFunc = &kernel<float, int8, INT8_MAX, 1, 0, 0,
					INT8_MIN, INT8_MAX>;
				return;
			case media_raw_audio_format::B_AUDIO_UCHAR:
				fFunc = &kernel<float, uint8, UINT8_MAX, 2, 0, 128,
					0, UINT8_MAX>;
				return;
			default:
				ERROR("Resampler::Resampler: unknown destination format 0x%x\n",
					dst_format);
				return;
		}
	}

	ERROR("Resampler::Resampler: source or destination format must be "
		"B_AUDIO_FLOAT\n");
}
