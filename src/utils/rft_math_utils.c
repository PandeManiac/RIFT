#include "utils/rft_math_utils.h"

bool rft_is_pow2(size_t x)
{
	return x > 0 && (x & (x - 1)) == 0;
}

float rft_clampf(float v, float lo, float hi)
{
	if (v < lo)
	{
		return lo;
	}

	if (v > hi)
	{
		return hi;
	}

	return v;
}
