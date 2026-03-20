#include "rft_svo_internal.h"

static rft_svo_region_state rft_svo_classify_height_region(const uint8_t heights[RFT_CHUNK_SIZE][RFT_CHUNK_SIZE],
														   uint32_t		 x0,
														   uint32_t		 y0,
														   uint32_t		 z0,
														   uint32_t		 size)
{
	uint32_t min_h = UINT32_MAX;
	uint32_t max_h = 0;

	for (uint32_t x = x0; x < x0 + size; ++x)
	{
		for (uint32_t z = z0; z < z0 + size; ++z)
		{
			uint32_t h = heights[x][z];

			if (h < min_h)
			{
				min_h = h;
			}

			if (h > max_h)
			{
				max_h = h;
			}
		}
	}

	if (max_h <= y0)
	{
		return RFT_SVO_REGION_EMPTY;
	}

	if (min_h >= y0 + size)
	{
		return RFT_SVO_REGION_SOLID;
	}

	return RFT_SVO_REGION_PARTIAL;
}

static bool rft_svo_sample_chunk_source(const rft_chunk* chunk, uint32_t x, uint32_t y, uint32_t z)
{
	return ((chunk->columns[x][z] >> y) & 1ULL) != 0ULL;
}

static rft_svo_region_state rft_svo_classify_chunk_region(const rft_chunk* chunk, uint32_t x0, uint32_t y0, uint32_t z0, uint32_t size)
{
	bool saw_empty = false;
	bool saw_solid = false;

	for (uint32_t x = x0; x < x0 + size; ++x)
	{
		for (uint32_t z = z0; z < z0 + size; ++z)
		{
			for (uint32_t y = y0; y < y0 + size; ++y)
			{
				if (rft_svo_sample_chunk_source(chunk, x, y, z))
				{
					saw_solid = true;
				}

				else
				{
					saw_empty = true;
				}

				if (saw_empty && saw_solid)
				{
					return RFT_SVO_REGION_PARTIAL;
				}
			}
		}
	}

	if (!saw_solid)
	{
		return RFT_SVO_REGION_EMPTY;
	}

	return RFT_SVO_REGION_SOLID;
}

static bool rft_svo_build_height_node(rft_svo_chunk* chunk,
									  const uint8_t	 heights[RFT_CHUNK_SIZE][RFT_CHUNK_SIZE],
									  uint32_t		 node_idx,
									  uint32_t		 x0,
									  uint32_t		 y0,
									  uint32_t		 z0,
									  uint32_t		 size)
{
	rft_svo_region_state state = rft_svo_classify_height_region(heights, x0, y0, z0, size);

	if (state == RFT_SVO_REGION_EMPTY)
	{
		return false;
	}

	chunk->nodes[node_idx] = (rft_svo_node) { 0, 0, 0, { 0, 0 } };

	if (state == RFT_SVO_REGION_SOLID || size == 1U)
	{
		chunk->nodes[node_idx].is_leaf = 1;
		return true;
	}

	uint32_t child_size	 = size / 2U;
	uint8_t	 child_mask	 = 0;
	uint32_t child_count = 0;

	for (uint32_t child = 0; child < 8U; ++child)
	{
		uint32_t child_x = x0 + ((child & 1U) ? child_size : 0U);
		uint32_t child_y = y0 + ((child & 2U) ? child_size : 0U);
		uint32_t child_z = z0 + ((child & 4U) ? child_size : 0U);

		rft_svo_region_state child_state = rft_svo_classify_height_region(heights, child_x, child_y, child_z, child_size);

		if (child_state != RFT_SVO_REGION_EMPTY)
		{
			child_mask |= (uint8_t)(1U << child);
			child_count++;
		}
	}

	ASSERT_FATAL(child_count > 0U);

	if (!rft_svo_chunk_reserve(chunk, child_count))
	{
		return false;
	}

	uint32_t first_child = chunk->node_count;
	chunk->node_count += child_count;

	chunk->nodes[node_idx].first_child = first_child;
	chunk->nodes[node_idx].child_mask  = child_mask;

	for (uint32_t child = 0; child < 8U; ++child)
	{
		if (((child_mask >> child) & 1U) == 0U)
		{
			continue;
		}

		uint32_t child_idx = first_child + rft_svo_child_rank(child_mask, child);
		uint32_t child_x   = x0 + ((child & 1U) ? child_size : 0U);
		uint32_t child_y   = y0 + ((child & 2U) ? child_size : 0U);
		uint32_t child_z   = z0 + ((child & 4U) ? child_size : 0U);

		if (!rft_svo_build_height_node(chunk, heights, child_idx, child_x, child_y, child_z, child_size))
		{
			return false;
		}
	}

	return true;
}

static bool rft_svo_build_chunk_node(rft_svo_chunk*	  chunk,
									 const rft_chunk* source_chunk,
									 uint32_t		  node_idx,
									 uint32_t		  x0,
									 uint32_t		  y0,
									 uint32_t		  z0,
									 uint32_t		  size)
{
	rft_svo_region_state state = rft_svo_classify_chunk_region(source_chunk, x0, y0, z0, size);

	if (state == RFT_SVO_REGION_EMPTY)
	{
		return false;
	}

	chunk->nodes[node_idx] = (rft_svo_node) { 0, 0, 0, { 0, 0 } };

	if (state == RFT_SVO_REGION_SOLID || size == 1U)
	{
		chunk->nodes[node_idx].is_leaf = 1;
		return true;
	}

	uint32_t child_size	 = size / 2U;
	uint8_t	 child_mask	 = 0;
	uint32_t child_count = 0;

	for (uint32_t child = 0; child < 8U; ++child)
	{
		uint32_t child_x = x0 + ((child & 1U) ? child_size : 0U);
		uint32_t child_y = y0 + ((child & 2U) ? child_size : 0U);
		uint32_t child_z = z0 + ((child & 4U) ? child_size : 0U);

		rft_svo_region_state child_state = rft_svo_classify_chunk_region(source_chunk, child_x, child_y, child_z, child_size);

		if (child_state != RFT_SVO_REGION_EMPTY)
		{
			child_mask |= (uint8_t)(1U << child);
			child_count++;
		}
	}

	ASSERT_FATAL(child_count > 0U);

	if (!rft_svo_chunk_reserve(chunk, child_count))
	{
		return false;
	}

	uint32_t first_child = chunk->node_count;

	chunk->node_count += child_count;
	chunk->nodes[node_idx].first_child = first_child;
	chunk->nodes[node_idx].child_mask  = child_mask;

	for (uint32_t child = 0; child < 8U; ++child)
	{
		if (((child_mask >> child) & 1U) == 0U)
		{
			continue;
		}

		uint32_t child_idx = first_child + rft_svo_child_rank(child_mask, child);
		uint32_t child_x   = x0 + ((child & 1U) ? child_size : 0U);
		uint32_t child_y   = y0 + ((child & 2U) ? child_size : 0U);
		uint32_t child_z   = z0 + ((child & 4U) ? child_size : 0U);

		if (!rft_svo_build_chunk_node(chunk, source_chunk, child_idx, child_x, child_y, child_z, child_size))
		{
			return false;
		}
	}

	return true;
}

bool rft_svo_chunk_build_from_heightmap(rft_svo_chunk* chunk, const uint8_t heights[RFT_CHUNK_SIZE][RFT_CHUNK_SIZE], uint8_t leaf_size)
{
	ASSERT_FATAL(chunk);
	ASSERT_FATAL(heights);

	chunk->node_count = 0;
	chunk->leaf_size  = rft_svo_sanitize_leaf_size(leaf_size);

	if (!rft_svo_chunk_reserve(chunk, 1U))
	{
		return false;
	}

	chunk->node_count = 1;

	if (!rft_svo_build_height_node(chunk, heights, 0, 0, 0, 0, RFT_CHUNK_SIZE))
	{
		chunk->node_count = 0;
		return false;
	}

	rft_svo_chunk_compact(chunk);
	return true;
}

bool rft_svo_chunk_build_from_chunk(rft_svo_chunk* chunk, const rft_chunk* source_chunk, uint8_t leaf_size)
{
	ASSERT_FATAL(chunk);
	ASSERT_FATAL(source_chunk);

	chunk->node_count = 0;
	chunk->leaf_size  = rft_svo_sanitize_leaf_size(leaf_size);

	if (!rft_svo_chunk_reserve(chunk, 1U))
	{
		return false;
	}

	chunk->node_count = 1;

	if (!rft_svo_build_chunk_node(chunk, source_chunk, 0, 0, 0, 0, RFT_CHUNK_SIZE))
	{
		chunk->node_count = 0;
		return false;
	}

	rft_svo_chunk_compact(chunk);
	return true;
}
