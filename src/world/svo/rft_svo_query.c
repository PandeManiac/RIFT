#include "rft_svo_internal.h"

#include <string.h>

static bool rft_svo_sample_node(const rft_svo_chunk* chunk, uint32_t node_idx, uint32_t x, uint32_t y, uint32_t z)
{
	uint32_t origin_x = 0;
	uint32_t origin_y = 0;
	uint32_t origin_z = 0;
	uint32_t size	  = RFT_CHUNK_SIZE;

	for (;;)
	{
		const rft_svo_node* node = &chunk->nodes[node_idx];

		if (node->is_leaf)
		{
			return true;
		}

		uint32_t child_size = size / 2U;
		uint32_t child		= 0;

		if (x >= origin_x + child_size)
		{
			child |= 1U;
			origin_x += child_size;
		}

		if (y >= origin_y + child_size)
		{
			child |= 2U;
			origin_y += child_size;
		}

		if (z >= origin_z + child_size)
		{
			child |= 4U;
			origin_z += child_size;
		}

		if (((node->child_mask >> child) & 1U) == 0U)
		{
			return false;
		}

		node_idx = node->first_child + rft_svo_child_rank(node->child_mask, child);
		size	 = child_size;
	}
}

static void rft_svo_fill_cube(rft_chunk* out_chunk, uint32_t x0, uint32_t y0, uint32_t z0, uint32_t size)
{
	uint64_t mask = (size >= 64U) ? ~0ULL : ((((uint64_t)1U << size) - 1ULL) << y0);

	for (uint32_t x = x0; x < x0 + size; ++x)
	{
		for (uint32_t z = z0; z < z0 + size; ++z)
		{
			out_chunk->columns[x][z] |= mask;
		}
	}
}

static void rft_svo_rasterize_node(const rft_svo_chunk* chunk, uint32_t node_idx, uint32_t x0, uint32_t y0, uint32_t z0, uint32_t size, rft_chunk* out_chunk)
{
	const rft_svo_node* node = &chunk->nodes[node_idx];

	if (node->is_leaf)
	{
		rft_svo_fill_cube(out_chunk, x0, y0, z0, size);
		return;
	}

	uint32_t child_size = size / 2U;

	for (uint32_t child = 0; child < 8U; ++child)
	{
		if (((node->child_mask >> child) & 1U) == 0U)
		{
			continue;
		}

		uint32_t child_idx = node->first_child + rft_svo_child_rank(node->child_mask, child);
		uint32_t child_x   = x0 + ((child & 1U) ? child_size : 0U);
		uint32_t child_y   = y0 + ((child & 2U) ? child_size : 0U);
		uint32_t child_z   = z0 + ((child & 4U) ? child_size : 0U);

		rft_svo_rasterize_node(chunk, child_idx, child_x, child_y, child_z, child_size, out_chunk);
	}
}

bool rft_svo_chunk_sample(const rft_svo_chunk* chunk, uint32_t x, uint32_t y, uint32_t z)
{
	ASSERT_FATAL(chunk);
	ASSERT_FATAL(x < RFT_CHUNK_SIZE);
	ASSERT_FATAL(y < RFT_CHUNK_SIZE);
	ASSERT_FATAL(z < RFT_CHUNK_SIZE);

	if (chunk->node_count == 0)
	{
		return false;
	}

	return rft_svo_sample_node(chunk, 0, x, y, z);
}

void rft_svo_chunk_rasterize(const rft_svo_chunk* chunk, rft_chunk* out_chunk)
{
	ASSERT_FATAL(chunk);
	ASSERT_FATAL(out_chunk);

	memset(out_chunk, 0, sizeof(*out_chunk));

	if (chunk->node_count == 0)
	{
		return;
	}

	rft_svo_rasterize_node(chunk, 0, 0, 0, 0, RFT_CHUNK_SIZE, out_chunk);
}
