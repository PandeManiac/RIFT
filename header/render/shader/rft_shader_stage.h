#pragma once

#include <glad/gl.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t rft_shader_stage_mask;

#define RFT_SHADER_STAGE_VERTEX (1u << 0)
#define RFT_SHADER_STAGE_FRAGMENT (1u << 1)
#define RFT_SHADER_STAGE_GEOMETRY (1u << 2)
#define RFT_SHADER_STAGE_TESS_CTRL (1u << 3)
#define RFT_SHADER_STAGE_TESS_EVAL (1u << 4)
#define RFT_SHADER_STAGE_COMPUTE (1u << 5)

const char* rft_shader_stage_name(rft_shader_stage_mask stage);
GLenum		rft_shader_stage_gl(rft_shader_stage_mask stage);
void		rft_shader_stage_mask_string(rft_shader_stage_mask mask, char* out, size_t out_size);
