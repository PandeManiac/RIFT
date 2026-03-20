#pragma once

#include <stdatomic.h>
#include <stddef.h>

typedef struct rft_arena_atomic
{
	unsigned char* base;
	size_t		   capacity;
	_Atomic size_t offset;
} rft_arena_atomic;

void  rft_arena_atomic_init(rft_arena_atomic* arena, void* memory, size_t size);
void* rft_arena_atomic_alloc(rft_arena_atomic* arena, size_t size, size_t alignment);
void  rft_arena_atomic_reset(rft_arena_atomic* arena);
