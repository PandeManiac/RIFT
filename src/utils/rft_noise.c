#include "utils/rft_noise.h"
#include <math.h>

static float hash(int x, int y)
{
	int n = x + y * 57;
	n	  = (n << 13) ^ n;
	int h = (n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff;
	return (1.0f - (float)h / 1073741824.0f);
}

static float lerp(float a, float b, float t)
{
	return a + t * (b - a);
}

static float smooth(float t)
{
	return t * t * (3.0f - 2.0f * t);
}

float rft_noise_2d(float x, float y)
{
	float ix_f = floorf(x);
	float iy_f = floorf(y);
	int	  ix   = (int)ix_f;
	int	  iy   = (int)iy_f;
	float fx   = x - ix_f;
	float fy   = y - iy_f;

	float v00 = hash(ix, iy);
	float v10 = hash(ix + 1, iy);
	float v01 = hash(ix, iy + 1);
	float v11 = hash(ix + 1, iy + 1);

	float sx = smooth(fx);
	float sy = smooth(fy);

	return lerp(lerp(v00, v10, sx), lerp(v01, v11, sx), sy);
}

float rft_noise_fbm_2d(float x, float y, int octaves, float persistence, float scale)
{
	float total			= 0.0f;
	float frequency		= scale;
	float amplitude		= 1.0f;
	float max_amplitude = 0.0f;

	for (int i = 0; i < octaves; i++)
	{
		total += rft_noise_2d(x * frequency, y * frequency) * amplitude;
		max_amplitude += amplitude;
		amplitude *= persistence;
		frequency *= 2.0f;
	}

	return total / max_amplitude;
}
