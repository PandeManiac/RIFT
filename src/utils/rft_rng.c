#include "utils/rft_rng.h"
#include "utils/rft_assert.h"

#include <stdint.h>

void rft_rng_seed(rft_rng* rng, uint32_t seed)
{
	ASSERT_FATAL(seed > 0);
	rng->state = seed;
}

uint32_t rft_rng_u32(rft_rng* rng)
{
	uint32_t x = rng->state;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;

	rng->state = x;
	return x;
}

float rft_rng_f32(rft_rng* rng)
{
	return (float)(rft_rng_u32(rng) >> 8) * (1.0f / 16777216.0f);
}

float rft_rng_range(rft_rng* rng, float min, float max)
{
	return min + (max - min) * rft_rng_f32(rng);
}

int rft_rng_range_i(rft_rng* rng, int min, int max)
{
	return min + (int)(rft_rng_u32(rng) % (uint32_t)(max - min + 1));
}
