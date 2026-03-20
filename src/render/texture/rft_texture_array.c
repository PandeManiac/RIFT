#include "render/texture/rft_texture_array.h"
#include "utils/rft_assert.h"

#include <stdlib.h>
#include <string.h>

static int rft_texture_array_mip_count(int width, int height)
{
	int levels = 1;

	while (width > 1 || height > 1)
	{
		width  = width > 1 ? width / 2 : 1;
		height = height > 1 ? height / 2 : 1;
		levels++;
	}

	return levels;
}

static bool rft_texture_array_pick_sparse_page_size(int width, int height, int layers, GLint* out_index)
{
	ASSERT_FATAL(out_index);

	if (!GLAD_GL_ARB_sparse_texture)
	{
		return false;
	}

	GLint page_count = 0;
	glGetInternalformativ(GL_TEXTURE_2D_ARRAY, GL_RGBA8, GL_NUM_VIRTUAL_PAGE_SIZES_ARB, 1, &page_count);

	if (page_count <= 0)
	{
		return false;
	}

	GLint* page_x = calloc((size_t)page_count, sizeof(GLint));
	GLint* page_y = calloc((size_t)page_count, sizeof(GLint));
	GLint* page_z = calloc((size_t)page_count, sizeof(GLint));

	ASSERT_FATAL(page_x);
	ASSERT_FATAL(page_y);
	ASSERT_FATAL(page_z);

	glGetInternalformativ(GL_TEXTURE_2D_ARRAY, GL_RGBA8, GL_VIRTUAL_PAGE_SIZE_X_ARB, page_count, page_x);
	glGetInternalformativ(GL_TEXTURE_2D_ARRAY, GL_RGBA8, GL_VIRTUAL_PAGE_SIZE_Y_ARB, page_count, page_y);
	glGetInternalformativ(GL_TEXTURE_2D_ARRAY, GL_RGBA8, GL_VIRTUAL_PAGE_SIZE_Z_ARB, page_count, page_z);

	bool found = false;

	for (GLint i = 0; i < page_count; ++i)
	{
		if (page_x[i] <= width && page_y[i] <= height && page_z[i] <= layers)
		{
			*out_index = i;
			found	   = true;
			break;
		}
	}

	free(page_z);
	free(page_y);
	free(page_x);

	return found;
}

static void rft_texture_array_commit_sparse_levels(int width, int height, int layers, int level_count)
{
	for (int level = 0; level < level_count; ++level)
	{
		int level_width	 = width >> level;
		int level_height = height >> level;

		if (level_width < 1)
		{
			level_width = 1;
		}

		if (level_height < 1)
		{
			level_height = 1;
		}

		glTexPageCommitmentARB(GL_TEXTURE_2D_ARRAY, level, 0, 0, 0, level_width, level_height, layers, GL_TRUE);
	}
}

void rft_texture_array_init(rft_texture_array* texture, int width, int height, int layers, const void* pixel_data, int generate_mips, int nearest_filter)
{
	ASSERT_FATAL(texture);
	ASSERT_FATAL(width > 0);
	ASSERT_FATAL(height > 0);
	ASSERT_FATAL(layers > 0);
	ASSERT_FATAL(pixel_data);

	memset(texture, 0, sizeof(*texture));
	texture->width				= width;
	texture->height				= height;
	texture->layers				= layers;
	texture->bindless_supported = GLAD_GL_ARB_bindless_texture != 0;
	texture->sparse_supported	= GLAD_GL_ARB_sparse_texture != 0;

	const GLenum filter		 = nearest_filter ? GL_NEAREST : GL_LINEAR;
	const GLenum min_filter	 = generate_mips ? (nearest_filter ? GL_NEAREST_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_LINEAR) : filter;
	const int	 level_count = generate_mips ? rft_texture_array_mip_count(width, height) : 1;

	glGenTextures(1, &texture->id);
	glBindTexture(GL_TEXTURE_2D_ARRAY, texture->id);

	GLint sparse_page_index = 0;

	if (rft_texture_array_pick_sparse_page_size(width, height, layers, &sparse_page_index))
	{
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_VIRTUAL_PAGE_SIZE_INDEX_ARB, sparse_page_index);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_SPARSE_ARB, GL_TRUE);
		texture->sparse_enabled = true;
	}

	else
	{
		texture->sparse_supported = false;
	}

	glTexStorage3D(GL_TEXTURE_2D_ARRAY, level_count, GL_RGBA8, width, height, layers);

	if (texture->sparse_enabled)
	{
		rft_texture_array_commit_sparse_levels(width, height, layers, level_count);
	}

	glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, width, height, layers, GL_RGBA, GL_UNSIGNED_BYTE, pixel_data);

	if (generate_mips)
	{
		glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
	}

	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, (GLint)min_filter);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, (GLint)filter);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

	if (generate_mips && GLAD_GL_EXT_texture_filter_anisotropic)
	{
		GLfloat max_aniso = 0.0f;
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_aniso);

		if (max_aniso > 0.0f)
		{
			glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_aniso);
		}
	}

	if (texture->bindless_supported)
	{
		texture->handle = glGetTextureHandleARB(texture->id);
		glMakeTextureHandleResidentARB(texture->handle);
	}

	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void rft_texture_array_bind(const rft_texture_array* texture, uint32_t unit)
{
	ASSERT_FATAL(texture);
	glBindTextureUnit(unit, texture->id);
}

void rft_texture_array_upload_bindless(const rft_texture_array* texture, GLuint program, GLint location)
{
	ASSERT_FATAL(texture);
	ASSERT_FATAL(program != 0);
	ASSERT_FATAL(location >= 0);
	ASSERT_FATAL(texture->bindless_supported);

	glProgramUniformHandleui64ARB(program, location, texture->handle);
}

void rft_texture_array_destroy(rft_texture_array* texture)
{
	ASSERT_FATAL(texture);

	if (texture->bindless_supported && texture->handle != 0ULL)
	{
		glMakeTextureHandleNonResidentARB(texture->handle);
	}

	if (texture->id != 0U)
	{
		glDeleteTextures(1, &texture->id);
	}

	memset(texture, 0, sizeof(*texture));
}
