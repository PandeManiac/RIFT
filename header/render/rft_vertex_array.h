#pragma once

#include <glad/gl.h>
#include <stdint.h>

typedef struct rft_vertex_array
{
	GLuint id;
} rft_vertex_array;

void rft_vertex_array_init(rft_vertex_array* vao);
void rft_vertex_array_bind(const rft_vertex_array* vao);
void rft_vertex_array_destroy(rft_vertex_array* vao);
void rft_vertex_array_attrib_f32(uint32_t index, int components, GLsizei stride, uintptr_t offset);
void rft_vertex_array_attrib_u32(uint32_t index, int components, GLsizei stride, uintptr_t offset);
