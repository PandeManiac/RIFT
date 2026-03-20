#pragma once

#include "render/shader/rft_shader_stage.h"
#include <glad/gl.h>
#include <stddef.h>

typedef struct rft_shader_stage_desc
{
	rft_shader_stage_mask stage;
	const char*			  path;
} rft_shader_stage_desc;

typedef struct rft_shader
{
	GLuint				  program;
	rft_shader_stage_mask stage_mask;
} rft_shader;

void rft_shader_init(rft_shader* shader, const rft_shader_stage_desc* stages, size_t stage_count);
void rft_shader_bind(const rft_shader* shader);
void rft_shader_destroy(rft_shader* shader);

GLint rft_shader_get_uniform(const rft_shader* shader, const char* name);

void rft_shader_set_int(const rft_shader* shader, GLint location, int value);
void rft_shader_set_float(const rft_shader* shader, GLint location, float value);

void rft_shader_set_vec3i(const rft_shader* shader, GLint location, int x, int y, int z);

void rft_shader_set_vec2(const rft_shader* shader, GLint location, float x, float y);
void rft_shader_set_vec3(const rft_shader* shader, GLint location, float x, float y, float z);
void rft_shader_set_vec4(const rft_shader* shader, GLint location, float x, float y, float z, float w);

void rft_shader_set_mat4(const rft_shader* shader, GLint location, const float* value);
