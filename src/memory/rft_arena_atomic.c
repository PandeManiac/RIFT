#include "memory/rft_arena_atomic.h"

#include "utils/rft_assert.h"
#include "utils/rft_math_utils.h"

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

static inline uintptr_t align_up(uintptr_t value, size_t alignment)
{
	ASSERT_FATAL(rft_is_pow2(alignment));
	return (value + alignment - 1) & ~(uintptr_t)(alignment - 1);
}

void rft_arena_atomic_init(rft_arena_atomic* arena, void* memory, size_t size)
{
	ASSERT_FATAL(arena);
	ASSERT_FATAL(memory);
	ASSERT_FATAL(size > 0);

	memset(arena, 0, sizeof(*arena));

	arena->base		= (unsigned char*)memory;
	arena->capacity = size;
	atomic_store(&arena->offset, 0);
}

void* rft_arena_atomic_alloc(rft_arena_atomic* arena, size_t size, size_t alignment)
{
	ASSERT_FATAL(arena);
	ASSERT_FATAL(size > 0);
	ASSERT_FATAL(rft_is_pow2(alignment));

	const uintptr_t base	= (uintptr_t)arena->base;
	size_t			current = atomic_load(&arena->offset);

	for (;;)
	{
		const uintptr_t ptr		= base + current;
		const uintptr_t aligned = align_up(ptr, alignment);
		const uintptr_t next	= (aligned - base) + size;

		if (next > arena->capacity)
		{
			return NULL;
		}

		size_t expected = current;

		if (atomic_compare_exchange_weak(&arena->offset, &expected, (size_t)next))
		{
			return (void*)aligned;
		}

		current = expected;
	}
}

void rft_arena_atomic_reset(rft_arena_atomic* arena)
{
	ASSERT_FATAL(arena);
	atomic_store(&arena->offset, 0);
}
