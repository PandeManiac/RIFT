#pragma once

#include <stddef.h>
#include <stdint.h>

#define B(x) ((size_t)(x))
#define KB(x) ((size_t)(x) << 10)
#define MB(x) ((size_t)(x) << 20)
#define GB(x) ((size_t)(x) << 30)
#define TB(x) ((size_t)(x) << 40)

#define COUNT_OF(arr) (sizeof(arr) / sizeof(*(arr)))

#define UNUSED(x)                                                                                                                                              \
	do                                                                                                                                                         \
	{                                                                                                                                                          \
		(void)(x);                                                                                                                                             \
	} while (0)

void rft_set_cwd_to_executable(void);
