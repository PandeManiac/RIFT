#include "render/shader/rft_shader.h"

#include "utils/rft_assert.h"
#include "utils/rft_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RFT_SHADER_STAGE_COUNT 6

typedef enum pipeline_class
{
	PIPELINE_INVALID	   = 0,
	PIPELINE_COMPUTE	   = 1,
	PIPELINE_GRAPHICS	   = 2,
	PIPELINE_GRAPHICS_TESS = 3,
} pipeline_class;

static GLuint compile_shader_stage(rft_shader_stage_mask stage, const char* path)
{
	size_t src_size = 0;
	char*  src		= rft_read_file(path, &src_size);

	ASSERT_FATAL(src);
	ASSERT_FATAL(src_size > 0);

	const GLenum gl_stage = rft_shader_stage_gl(stage);
	ASSERT_FATAL(gl_stage != GL_INVALID_ENUM);

	const GLuint shader = glCreateShader(gl_stage);
	ASSERT_FATAL(shader);

	const GLchar* gl_src = (const GLchar*)src;
	const GLint	  gl_len = (GLint)src_size;

	glShaderSource(shader, 1, &gl_src, &gl_len);
	free(src);

	glCompileShader(shader);

	GLint success = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

	if (!success)
	{
		GLint log_len = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);

		char* log = NULL;

		if (log_len > 0)
		{
			log = malloc((size_t)log_len);
			ASSERT_FATAL(log);

			GLsizei written = 0;
			glGetShaderInfoLog(shader, log_len, &written, log);
		}

		fprintf(stderr,
				"\n============================================================"
				"\nSHADER COMPILATION FAILURE"
				"\n------------------------------------------------------------"
				"\nStage       : %s"
				"\nGL Enum     : 0x%X"
				"\nSource Path : %s"
				"\n------------------------------------------------------------"
				"\nCompiler Log:\n%s"
				"\n============================================================\n",
				rft_shader_stage_name(stage),
				gl_stage,
				path,
				log ? log : "<no log>");

		free(log);
		glDeleteShader(shader);
		ASSERT_FATAL(0);
	}

	return shader;
}

void rft_shader_init(rft_shader* shader, const rft_shader_stage_desc* stages, size_t stage_count)
{
	ASSERT_FATAL(shader);
	ASSERT_FATAL(stages);
	ASSERT_FATAL(stage_count > 0);
	ASSERT_FATAL(stage_count <= RFT_SHADER_STAGE_COUNT);

	const GLuint program = glCreateProgram();
	ASSERT_FATAL(program);

	rft_shader_stage_mask stage_mask = 0;

	GLuint compiled[RFT_SHADER_STAGE_COUNT] = { 0 };
	size_t compiled_count					= 0;

	for (size_t i = 0; i < stage_count; ++i)
	{
		const rft_shader_stage_mask stage = stages[i].stage;

		ASSERT_FATAL(stage);
		ASSERT_FATAL((stage & (stage - 1)) == 0);
		ASSERT_FATAL(!(stage_mask & stage));
		ASSERT_FATAL(stages[i].path);

		stage_mask |= stage;

		GLuint sh = compile_shader_stage(stage, stages[i].path);
		glAttachShader(program, sh);

		compiled[compiled_count++] = sh;
	}

	const int has_compute = (stage_mask & RFT_SHADER_STAGE_COMPUTE) != 0;
	const int has_vertex  = (stage_mask & RFT_SHADER_STAGE_VERTEX) != 0;

	const rft_shader_stage_mask tess_mask = RFT_SHADER_STAGE_TESS_CTRL | RFT_SHADER_STAGE_TESS_EVAL;
	const int					has_tess  = (stage_mask & tess_mask) != 0;

	const pipeline_class compute_part  = has_compute ? PIPELINE_COMPUTE : PIPELINE_INVALID;
	const pipeline_class graphics_part = has_vertex ? PIPELINE_GRAPHICS : PIPELINE_INVALID;
	const pipeline_class tess_part	   = has_tess ? (PIPELINE_GRAPHICS_TESS ^ PIPELINE_GRAPHICS) : PIPELINE_INVALID;
	const pipeline_class cls		   = compute_part | graphics_part | tess_part;

	const int tess_complete = (stage_mask & tess_mask) == tess_mask;
	const int compute_only	= stage_mask == RFT_SHADER_STAGE_COMPUTE;

	switch (cls)
	{
		case PIPELINE_COMPUTE:
			ASSERT_FATAL(compute_only);
			break;

		case PIPELINE_GRAPHICS:
			ASSERT_FATAL(has_vertex);
			ASSERT_FATAL(!has_tess);
			break;

		case PIPELINE_GRAPHICS_TESS:
			ASSERT_FATAL(has_vertex);
			ASSERT_FATAL(tess_complete);
			break;

		case PIPELINE_INVALID:
		default:
			ASSERT_FATAL(0 && "Invalid shader pipeline classification!");
	}

	glLinkProgram(program);
	GLint linked = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);

	if (!linked)
	{
		GLint log_len = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
		char* log = NULL;

		if (log_len > 0)
		{
			log = malloc((size_t)log_len);
			ASSERT_FATAL(log);

			GLsizei written = 0;
			glGetProgramInfoLog(program, log_len, &written, log);
		}

		fprintf(stderr,
				"\n============================================================"
				"\nSHADER LINK FAILURE"
				"\n------------------------------------------------------------"
				"\nStage Mask : 0x%08X"
				"\nLog:\n%s"
				"\n============================================================\n",
				stage_mask,
				log ? log : "<no log>");

		free(log);
		ASSERT_FATAL(0);
	}

	for (size_t i = 0; i < compiled_count; ++i)
	{
		glDetachShader(program, compiled[i]);
		glDeleteShader(compiled[i]);
	}

	shader->program	   = program;
	shader->stage_mask = stage_mask;
}

void rft_shader_bind(const rft_shader* shader)
{
	ASSERT_FATAL(shader);
	ASSERT_FATAL(shader->program);
	glUseProgram(shader->program);
}

void rft_shader_destroy(rft_shader* shader)
{
	ASSERT_FATAL(shader);
	ASSERT_FATAL(shader->program);
	glDeleteProgram(shader->program);

	shader->program	   = 0;
	shader->stage_mask = 0;
}

GLint rft_shader_get_uniform(const rft_shader* shader, const char* name)
{
	ASSERT_FATAL(shader);
	ASSERT_FATAL(shader->program);
	ASSERT_FATAL(name);

	GLint loc = glGetUniformLocation(shader->program, name);
	ASSERT_FATAL(loc != -1);

	return loc;
}

void rft_shader_set_int(const rft_shader* shader, GLint location, int value)
{
	ASSERT_FATAL(shader);
	ASSERT_FATAL(location >= 0);

	glUniform1i(location, value);
}

void rft_shader_set_float(const rft_shader* shader, GLint location, float value)
{
	ASSERT_FATAL(shader);
	ASSERT_FATAL(location >= 0);

	glUniform1f(location, value);
}

void rft_shader_set_vec3i(const rft_shader* shader, GLint location, int x, int y, int z)
{
	ASSERT_FATAL(shader);
	ASSERT_FATAL(location >= 0);

	glUniform3i(location, x, y, z);
}

void rft_shader_set_vec2(const rft_shader* shader, GLint location, float x, float y)
{
	ASSERT_FATAL(shader);
	ASSERT_FATAL(location >= 0);

	glUniform2f(location, x, y);
}

void rft_shader_set_vec3(const rft_shader* shader, GLint location, float x, float y, float z)
{
	ASSERT_FATAL(shader);
	ASSERT_FATAL(location >= 0);

	glUniform3f(location, x, y, z);
}

void rft_shader_set_vec4(const rft_shader* shader, GLint location, float x, float y, float z, float w)
{
	ASSERT_FATAL(shader);
	ASSERT_FATAL(location >= 0);

	glUniform4f(location, x, y, z, w);
}

void rft_shader_set_mat4(const rft_shader* shader, GLint location, const float* value)
{
	ASSERT_FATAL(shader);
	ASSERT_FATAL(location >= 0);
	ASSERT_FATAL(value);

	glUniformMatrix4fv(location, 1, GL_FALSE, value);
}
