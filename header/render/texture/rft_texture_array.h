#pragma once

#include "vendor/glad/gl.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct rft_texture_array
{
	GLuint	 id;
	uint64_t handle;
	int		 width;
	int		 height;
	int		 layers;
	bool	 bindless_supported;
	bool	 sparse_supported;
	bool	 sparse_enabled;
} rft_texture_array;

void rft_texture_array_init(rft_texture_array* tex, int width, int height, int layers, const void* pixel_data, int generate_mips, int nearest_filter);

void rft_texture_array_bind(const rft_texture_array* tex, uint32_t unit);
void rft_texture_array_upload_bindless(const rft_texture_array* tex, GLuint program, GLint location);
void rft_texture_array_destroy(rft_texture_array* tex);
