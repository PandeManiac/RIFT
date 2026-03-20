#include "stb/stb_image.h"

#include "render/texture/rft_texture.h"
#include "utils/rft_assert.h"

void rft_texture_init(rft_texture* texture, const char* path, int generate_mips, int nearest_filter)
{
	ASSERT_FATAL(texture);
	ASSERT_FATAL(path);

	stbi_set_flip_vertically_on_load(0);

	unsigned char* data = stbi_load(path, &texture->width, &texture->height, &texture->channels, 0);
	ASSERT_FATAL(data && "Failed to load texture");

	GLenum format = 0;

	if (texture->channels == 4)
	{
		format = GL_RGBA;
	}

	else if (texture->channels == 3)
	{
		format = GL_RGB;
	}

	else
	{
		ASSERT_FATAL(0 && "Unsupported texture format");
	}

	glGenTextures(1, &texture->id);
	glBindTexture(GL_TEXTURE_2D, texture->id);
	glTexImage2D(GL_TEXTURE_2D, 0, (GLint)format, texture->width, texture->height, 0, format, GL_UNSIGNED_BYTE, data);

	if (generate_mips)
	{
		glGenerateMipmap(GL_TEXTURE_2D);
	}

	GLenum filter = nearest_filter ? GL_NEAREST : GL_LINEAR;
	GLenum min_filter;

	if (generate_mips)
	{
		if (nearest_filter)
		{
			min_filter = GL_NEAREST_MIPMAP_LINEAR;
		}

		else
		{
			min_filter = GL_LINEAR_MIPMAP_LINEAR;
		}
	}

	else
	{
		min_filter = filter;
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (signed)min_filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (signed)filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	if (generate_mips)
	{
		GLfloat max_aniso = 0.0f;
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_aniso);

		if (max_aniso > 0.0f)
		{
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_aniso);
		}
	}

	stbi_image_free(data);
}

void rft_texture_bind(const rft_texture* texture, uint32_t unit)
{
	ASSERT_FATAL(texture);
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_2D, texture->id);
}

void rft_texture_destroy(rft_texture* texture)
{
	ASSERT_FATAL(texture);

	if (texture->id)
	{
		glDeleteTextures(1, &texture->id);
	}

	texture->id = 0;
}
