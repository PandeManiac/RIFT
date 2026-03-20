#pragma once

#include "vendor/glad/gl.h"
#include <stdint.h>

typedef struct
{
	GLuint id;
	int	   width;
	int	   height;
	int	   channels;
} rft_texture;

void rft_texture_init(rft_texture* texture, const char* path, int generate_mips, int nearest_filter);
void rft_texture_bind(const rft_texture* texture, uint32_t unit);
void rft_texture_destroy(rft_texture* texture);
