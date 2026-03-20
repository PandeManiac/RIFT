#pragma once

#include "utils/rft_hints.h"

NORETURN void rft_assert_fail(const char* expr, const char* file, int line, const char* func);

#define ASSERT_FATAL(expr)                                                                                                                                     \
	do                                                                                                                                                         \
	{                                                                                                                                                          \
		if (UNLIKELY(!(expr)))                                                                                                                                 \
		{                                                                                                                                                      \
			rft_assert_fail(#expr, __FILE__, __LINE__, __func__);                                                                                              \
		}                                                                                                                                                      \
	} while (0)

#define STATIC_ASSERT(expr, msg) _Static_assert((expr), msg)
