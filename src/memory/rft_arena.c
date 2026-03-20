#include "memory/rft_arena.h"

#include "utils/rft_assert.h"
#include "utils/rft_math_utils.h"

#include <stdint.h>
#include <string.h>

static inline uintptr_t align_up(uintptr_t value, size_t alignment)
{
	ASSERT_FATAL(rft_is_pow2(alignment));
	return (value + alignment - 1) & ~(uintptr_t)(alignment - 1);
}

void rft_arena_init(rft_arena* arena, void* memory, size_t size)
{
	ASSERT_FATAL(arena);
	ASSERT_FATAL(memory);
	ASSERT_FATAL(size > 0);

	memset(arena, 0, sizeof(*arena));

	arena->base		= (unsigned char*)memory;
	arena->capacity = size;
	arena->offset	= 0;
}

void* rft_arena_alloc(rft_arena* arena, size_t size, size_t alignment)
{
	ASSERT_FATAL(arena);
	ASSERT_FATAL(size > 0);
	ASSERT_FATAL(rft_is_pow2(alignment));
	ASSERT_FATAL(arena->offset <= arena->capacity);

	const uintptr_t base	= (uintptr_t)arena->base;
	const uintptr_t current = base + arena->offset;
	const uintptr_t aligned = align_up(current, alignment);
	const uintptr_t used	= aligned - base;

	ASSERT_FATAL(used + size <= arena->capacity);

	arena->offset = used + size;
	return (void*)aligned;
}

void rft_arena_reset(rft_arena* arena)
{
	ASSERT_FATAL(arena);
	arena->offset = 0;
}
