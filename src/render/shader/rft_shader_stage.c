#include "render/shader/rft_shader_stage.h"
#include "utils/rft_assert.h"
#include "utils/rft_gen_utils.h"

#include <stdio.h>
#include <string.h>

typedef struct rft_shader_stage_info
{
	const char* name;
	GLenum		gl_enum;
} rft_shader_stage_info;

static const rft_shader_stage_info rft_shader_stage_table[] = {
	{ "RFT_SHADER_STAGE_VERTEX", GL_VERTEX_SHADER },
	{ "RFT_SHADER_STAGE_FRAGMENT", GL_FRAGMENT_SHADER },
	{ "RFT_SHADER_STAGE_GEOMETRY", GL_GEOMETRY_SHADER },
	{ "RFT_SHADER_STAGE_TESS_CTRL", GL_TESS_CONTROL_SHADER },
	{ "RFT_SHADER_STAGE_TESS_EVAL", GL_TESS_EVALUATION_SHADER },
	{ "RFT_SHADER_STAGE_COMPUTE", GL_COMPUTE_SHADER },
};

static void assert_single_stage(rft_shader_stage_mask stage)
{
	ASSERT_FATAL(stage);
	ASSERT_FATAL((stage & (stage - 1)) == 0);
}

static unsigned stage_index(rft_shader_stage_mask stage)
{
	return (unsigned)__builtin_ctz(stage);
}

const char* rft_shader_stage_name(rft_shader_stage_mask stage)
{
	assert_single_stage(stage);

	unsigned idx = stage_index(stage);
	ASSERT_FATAL(idx < COUNT_OF(rft_shader_stage_table));

	return rft_shader_stage_table[idx].name;
}

GLenum rft_shader_stage_gl(rft_shader_stage_mask stage)
{
	assert_single_stage(stage);

	unsigned idx = stage_index(stage);
	ASSERT_FATAL(idx < COUNT_OF(rft_shader_stage_table));

	return rft_shader_stage_table[idx].gl_enum;
}

void rft_shader_stage_mask_string(rft_shader_stage_mask mask, char* out, size_t out_size)
{
	ASSERT_FATAL(out);
	ASSERT_FATAL(out_size > 0);

	size_t used = 0;
	out[0]		= '\0';

	for (unsigned i = 0; i < COUNT_OF(rft_shader_stage_table); ++i)
	{
		if (mask & (1u << i))
		{
			const char* name	= rft_shader_stage_table[i].name;
			int			written = snprintf(out + used, out_size - used, "%s%s", used ? " | " : "", name);

			if (written < 0 || (size_t)written >= out_size - used)
			{
				break;
			}

			used += (size_t)written;
		}
	}
}
