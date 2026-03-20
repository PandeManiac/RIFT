#include "render/rft_vertex_array.h"
#include "utils/rft_assert.h"

void rft_vertex_array_init(rft_vertex_array* vao)
{
	ASSERT_FATAL(vao);

	glGenVertexArrays(1, &vao->id);
	ASSERT_FATAL(vao->id != 0);
}

void rft_vertex_array_bind(const rft_vertex_array* vao)
{
	if (!vao)
	{
		glBindVertexArray(0);
		return;
	}

	ASSERT_FATAL(vao->id != 0);
	glBindVertexArray(vao->id);
}

void rft_vertex_array_destroy(rft_vertex_array* vao)
{
	ASSERT_FATAL(vao);

	if (vao->id)
	{
		glDeleteVertexArrays(1, &vao->id);
		vao->id = 0;
	}
}

void rft_vertex_array_attrib_f32(uint32_t index, int components, GLsizei stride, uintptr_t offset)
{
	glVertexAttribPointer(index, components, GL_FLOAT, GL_FALSE, stride, (void*)offset);
	glEnableVertexAttribArray(index);
}

void rft_vertex_array_attrib_u32(uint32_t index, int components, GLsizei stride, uintptr_t offset)
{
	glVertexAttribIPointer(index, components, GL_UNSIGNED_INT, stride, (void*)offset);
	glEnableVertexAttribArray(index);
}