#include "utils/rft_file.h"
#include "utils/rft_assert.h"

#include <stdio.h>
#include <stdlib.h>

char* rft_read_file(const char* path, size_t* out_size)
{
	ASSERT_FATAL(path);
	ASSERT_FATAL(out_size);

	FILE* file = fopen(path, "rb");
	ASSERT_FATAL(file && "Failed to open file");

	ASSERT_FATAL(fseek(file, 0, SEEK_END) == 0);

	long file_size_long = ftell(file);
	ASSERT_FATAL(file_size_long >= 0);

	size_t file_size = (size_t)file_size_long;
	ASSERT_FATAL((long)file_size == file_size_long);

	ASSERT_FATAL(fseek(file, 0, SEEK_SET) == 0);

	char* buffer = malloc(file_size + 1);
	ASSERT_FATAL(buffer && "Failed to allocate file buffer");

	size_t read_size = fread(buffer, 1, file_size, file);
	ASSERT_FATAL(read_size == file_size);

	buffer[file_size] = '\0';
	fclose(file);

	*out_size = file_size;
	return buffer;
}