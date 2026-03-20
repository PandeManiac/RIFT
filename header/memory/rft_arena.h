#pragma once

#include <stddef.h>

typedef struct rft_arena
{
	unsigned char* base;
	size_t		   capacity;
	size_t		   offset;
} rft_arena;

void  rft_arena_init(rft_arena* arena, void* memory, size_t size);
void* rft_arena_alloc(rft_arena* arena, size_t size, size_t alignment);
void  rft_arena_reset(rft_arena* arena);
