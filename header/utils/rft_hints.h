#pragma once

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

#define ASSUME(cond)                                                                                                                                           \
	do                                                                                                                                                         \
	{                                                                                                                                                          \
		if (UNLIKELY(!(cond)))                                                                                                                                 \
		{                                                                                                                                                      \
			__builtin_unreachable();                                                                                                                           \
		}                                                                                                                                                      \
	} while (0)

#define HOT __attribute__((hot))
#define COLD __attribute__((cold))
#define NOINLINE __attribute__((noinline))
#define NORETURN __attribute__((noreturn))
#define ALWAYS_INLINE __attribute__((always_inline))
#define FLATTEN __attribute__((flatten))
#define CONSTRUCTOR __attribute__((constructor))
#define UNUSED_FUNC __attribute__((unused))
#define ALIGN(N) __attribute__((aligned(N)))

#define AVX2 __attribute__((target("avx2")))

#define UNREACHABLE                                                                                                                                            \
	do                                                                                                                                                         \
	{                                                                                                                                                          \
		__builtin_unreachable();                                                                                                                               \
	} while (0)

#define RESTRICT restrict
