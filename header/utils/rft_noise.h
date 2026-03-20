#pragma once

#include <stdint.h>

float rft_noise_2d(float x, float y);
float rft_noise_fbm_2d(float x, float y, int octaves, float persistence, float scale);
