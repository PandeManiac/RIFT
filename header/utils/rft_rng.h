#pragma once

#include <stdint.h>

typedef struct rft_rng
{
	uint32_t state;
} rft_rng;

void rft_rng_seed(rft_rng* rng, uint32_t seed);

uint32_t rft_rng_u32(rft_rng* rng);
float	 rft_rng_f32(rft_rng* rng);
float	 rft_rng_range(rft_rng* rng, float min, float max);
int		 rft_rng_range_i(rft_rng* rng, int min, int max);