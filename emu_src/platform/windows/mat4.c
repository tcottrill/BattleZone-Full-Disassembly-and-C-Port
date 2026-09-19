#include "mat4.h"

void mat4_ortho(float *out, float left, float right, float bottom, float top,
	float znear, float zfar)
{
	float rl = right - left;
	float tb = top - bottom;
	float fn = zfar - znear;

	// Degenerate ranges would divide by zero; leave an identity behind instead.
	if (rl == 0.0f || tb == 0.0f || fn == 0.0f)
	{
		for (int i = 0; i < 16; i++) out[i] = (i % 5 == 0) ? 1.0f : 0.0f;
		return;
	}

	out[0] = 2.0f / rl;
	out[1] = 0.0f;
	out[2] = 0.0f;
	out[3] = 0.0f;

	out[4] = 0.0f;
	out[5] = 2.0f / tb;
	out[6] = 0.0f;
	out[7] = 0.0f;

	out[8] = 0.0f;
	out[9] = 0.0f;
	out[10] = -2.0f / fn;
	out[11] = 0.0f;

	out[12] = -(right + left) / rl;
	out[13] = -(top + bottom) / tb;
	out[14] = -(zfar + znear) / fn;
	out[15] = 1.0f;
}
