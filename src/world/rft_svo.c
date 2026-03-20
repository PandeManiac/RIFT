#include "svo/rft_svo_internal.h"

#include <stdlib.h>
#include <string.h>

uint8_t rft_svo_sanitize_leaf_size(uint8_t leaf_size)
{
	if (leaf_size <= 1U)
	{
		return 1U;
	}

	uint8_t sanitized = 1U;

	while (sanitized < leaf_size && sanitized < RFT_CHUNK_SIZE)
	{
		sanitized = (uint8_t)(sanitized << 1U);
	}

	return sanitized;
}

bool rft_svo_chunk_reserve(rft_svo_chunk* chunk, uint32_t additional)
{
	uint32_t needed = chunk->node_count + additional;

	if (needed <= chunk->capacity)
	{
		return true;
	}

	uint32_t new_capacity = chunk->capacity ? chunk->capacity : 64U;

	while (new_capacity < needed)
	{
		new_capacity *= 2U;
	}

	rft_svo_node* nodes = realloc(chunk->nodes, (size_t)new_capacity * sizeof(rft_svo_node));

	if (!nodes)
	{
		return false;
	}

	chunk->nodes	= nodes;
	chunk->capacity = new_capacity;

	return true;
}

void rft_svo_chunk_compact(rft_svo_chunk* chunk)
{
	if (chunk->capacity <= chunk->node_count)
	{
		return;
	}

	rft_svo_node* compact_nodes = realloc(chunk->nodes, (size_t)chunk->node_count * sizeof(rft_svo_node));

	if (!compact_nodes)
	{
		return;
	}

	chunk->nodes	= compact_nodes;
	chunk->capacity = chunk->node_count;
}

uint32_t rft_svo_child_rank(uint8_t child_mask, uint32_t child)
{
	uint32_t rank = 0;

	for (uint32_t bit = 0; bit < child; ++bit)
	{
		rank += (uint32_t)((child_mask >> bit) & 1U);
	}

	return rank;
}

void rft_svo_chunk_init(rft_svo_chunk* chunk)
{
	ASSERT_FATAL(chunk);
	memset(chunk, 0, sizeof(*chunk));
}

void rft_svo_chunk_destroy(rft_svo_chunk* chunk)
{
	ASSERT_FATAL(chunk);
	free(chunk->nodes);
	memset(chunk, 0, sizeof(*chunk));
}

size_t rft_svo_chunk_memory_usage(const rft_svo_chunk* chunk)
{
	ASSERT_FATAL(chunk);
	return (size_t)chunk->capacity * sizeof(rft_svo_node);
}
