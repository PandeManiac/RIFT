#pragma once

#include <glad/gl.h>
#include <stddef.h>
#include <stdint.h>

typedef enum rft_buffer_type
{
	RFT_BUFFER_ARRAY,
	RFT_BUFFER_ELEMENT,
	RFT_BUFFER_SHADER_STORAGE,
	RFT_BUFFER_INDIRECT,
} rft_buffer_type;

typedef enum rft_buffer_usage
{
	RFT_BUFFER_STATIC_DRAW,
	RFT_BUFFER_DYNAMIC_DRAW,
	RFT_BUFFER_STREAM_DRAW,
} rft_buffer_usage;

typedef struct rft_buffer
{
	GLuint id;
	GLenum target;

	void*  mapped;
	size_t size;
} rft_buffer;

void rft_buffer_init(rft_buffer* buffer, rft_buffer_type type);
void rft_buffer_bind(const rft_buffer* buffer);
void rft_buffer_bind_base(const rft_buffer* buffer, uint32_t index);

void rft_buffer_set_data(rft_buffer* buffer, size_t size, const void* data, rft_buffer_usage usage);
void rft_buffer_set_data_persistent(rft_buffer* buffer, size_t size);

void* rft_buffer_map(rft_buffer* buffer);
void* rft_buffer_map_range(rft_buffer* buffer, size_t offset, size_t size, GLbitfield access);
void  rft_buffer_unmap(rft_buffer* buffer);

void rft_buffer_destroy(rft_buffer* buffer);
