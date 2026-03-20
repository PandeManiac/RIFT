#include "render/rft_buffer.h"

#include "utils/rft_assert.h"
#include "utils/rft_hints.h"

#include <glad/gl.h>

static HOT inline ALWAYS_INLINE GLenum rft_buffer_type_to_gl(rft_buffer_type type)
{
	switch (type)
	{
		case RFT_BUFFER_ARRAY:
			return GL_ARRAY_BUFFER;

		case RFT_BUFFER_ELEMENT:
			return GL_ELEMENT_ARRAY_BUFFER;

		case RFT_BUFFER_SHADER_STORAGE:
			return GL_SHADER_STORAGE_BUFFER;

		case RFT_BUFFER_INDIRECT:
			return GL_DRAW_INDIRECT_BUFFER;

		default:
			return GL_INVALID_ENUM;
	}
}

static HOT inline ALWAYS_INLINE GLenum rft_buffer_usage_to_gl(rft_buffer_usage usage)
{
	switch (usage)
	{
		case RFT_BUFFER_STATIC_DRAW:
			return GL_STATIC_DRAW;

		case RFT_BUFFER_DYNAMIC_DRAW:
			return GL_DYNAMIC_DRAW;

		case RFT_BUFFER_STREAM_DRAW:
			return GL_STREAM_DRAW;

		default:
			return GL_INVALID_ENUM;
	}
}

void rft_buffer_init(rft_buffer* buffer, rft_buffer_type type)
{
	ASSERT_FATAL(buffer);

	buffer->target = rft_buffer_type_to_gl(type);
	buffer->id	   = 0;
	buffer->mapped = NULL;
	buffer->size   = 0;

	glGenBuffers(1, &buffer->id);
	ASSERT_FATAL(buffer->id != 0);
}

void rft_buffer_bind(const rft_buffer* buffer)
{
	ASSERT_FATAL(buffer);
	ASSERT_FATAL(buffer->id != 0);

	glBindBuffer(buffer->target, buffer->id);
}

void rft_buffer_bind_base(const rft_buffer* buffer, uint32_t index)
{
	ASSERT_FATAL(buffer);
	ASSERT_FATAL(buffer->id != 0);

	glBindBufferBase(buffer->target, index, buffer->id);
}

void rft_buffer_set_data(rft_buffer* buffer, size_t size, const void* data, rft_buffer_usage usage)
{
	ASSERT_FATAL(buffer);
	ASSERT_FATAL(buffer->id != 0);

	GLenum gl_usage = rft_buffer_usage_to_gl(usage);
	ASSERT_FATAL(gl_usage != GL_INVALID_ENUM);

	buffer->size = size;

	glBindBuffer(buffer->target, buffer->id);
	glBufferData(buffer->target, (GLsizeiptr)size, data, gl_usage);
}

void rft_buffer_set_data_persistent(rft_buffer* buffer, size_t size)
{
	ASSERT_FATAL(buffer);
	ASSERT_FATAL(buffer->id != 0);

	const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;

	buffer->size = size;

	glBindBuffer(buffer->target, buffer->id);
	glBufferStorage(buffer->target, (GLsizeiptr)size, NULL, flags);

	buffer->mapped = glMapBufferRange(buffer->target, 0, (GLsizeiptr)size, flags);
	ASSERT_FATAL(buffer->mapped);
}

void* rft_buffer_map(rft_buffer* buffer)
{
	ASSERT_FATAL(buffer);

	if (buffer->mapped)
	{
		return buffer->mapped;
	}

	glBindBuffer(buffer->target, buffer->id);
	buffer->mapped = glMapBuffer(buffer->target, GL_READ_WRITE);
	ASSERT_FATAL(buffer->mapped);

	return buffer->mapped;
}

void* rft_buffer_map_range(rft_buffer* buffer, size_t offset, size_t size, GLbitfield access)
{
	ASSERT_FATAL(buffer);
	ASSERT_FATAL(buffer->id != 0);

	glBindBuffer(buffer->target, buffer->id);
	void* ptr = glMapBufferRange(buffer->target, (GLintptr)offset, (GLsizeiptr)size, access);
	ASSERT_FATAL(ptr);

	return ptr;
}

void rft_buffer_unmap(rft_buffer* buffer)
{
	ASSERT_FATAL(buffer);

	if (buffer->mapped)
	{
		return;
	}
}

void rft_buffer_destroy(rft_buffer* buffer)
{
	ASSERT_FATAL(buffer);

	if (buffer->id != 0)
	{
		glDeleteBuffers(1, &buffer->id);
		buffer->id = 0;
	}

	buffer->mapped = NULL;
	buffer->size   = 0;
}
