#include "world/rft_svo.h"
#include "utils/rft_assert.h"
#include "world/rft_terrain.h"

#include <stdlib.h>
#include <string.h>

typedef enum rft_svo_region_state
{
	RFT_SVO_REGION_EMPTY,
	RFT_SVO_REGION_SOLID,
	RFT_SVO_REGION_PARTIAL
} rft_svo_region_state;

#define RFT_SVO_MAX_FACES (RFT_CHUNK_SIZE * RFT_CHUNK_SIZE * RFT_CHUNK_SIZE * 3 / 2)

static rft_svo_region_state rft_svo_classify_height_region(const uint8_t heights[RFT_CHUNK_SIZE][RFT_CHUNK_SIZE],
														   uint32_t		 x0,
														   uint32_t		 y0,
														   uint32_t		 z0,
														   uint32_t		 size);
static bool					rft_svo_sample_chunk_source(const rft_chunk* chunk, uint32_t x, uint32_t y, uint32_t z);
static rft_svo_region_state rft_svo_classify_chunk_region(const rft_chunk* chunk, uint32_t x0, uint32_t y0, uint32_t z0, uint32_t size);
static bool					rft_svo_build_height_node(rft_svo_chunk* chunk,
													  const uint8_t	 heights[RFT_CHUNK_SIZE][RFT_CHUNK_SIZE],
													  uint32_t		 node_idx,
													  uint32_t		 x0,
													  uint32_t		 y0,
													  uint32_t		 z0,
													  uint32_t		 size);
static bool					rft_svo_build_chunk_node(rft_svo_chunk*	  chunk,
													 const rft_chunk* source_chunk,
													 uint32_t		  node_idx,
													 uint32_t		  x0,
													 uint32_t		  y0,
													 uint32_t		  z0,
													 uint32_t		  size);
static bool					rft_svo_sample_node(const rft_svo_chunk* chunk, uint32_t node_idx, uint32_t x, uint32_t y, uint32_t z);
static void					rft_svo_fill_cube(rft_chunk* out_chunk, uint32_t x0, uint32_t y0, uint32_t z0, uint32_t size);
static void rft_svo_rasterize_node(const rft_svo_chunk* chunk, uint32_t node_idx, uint32_t x0, uint32_t y0, uint32_t z0, uint32_t size, rft_chunk* out_chunk);
static void rft_svo_mesh_2d_plane(uint64_t*		  masks,
								  uint16_t		  material,
								  uint8_t		  normal,
								  uint32_t		  c_val,
								  uint8_t		  u_axis,
								  uint8_t		  v_axis,
								  uint8_t		  w_axis,
								  rft_voxel_face* out,
								  size_t*		  count);
static bool rft_svo_sample_with_neighbors(const rft_svo_chunk* self,
										  const rft_svo_chunk* neighbor_px,
										  const rft_svo_chunk* neighbor_nx,
										  const rft_svo_chunk* neighbor_py,
										  const rft_svo_chunk* neighbor_ny,
										  const rft_svo_chunk* neighbor_pz,
										  const rft_svo_chunk* neighbor_nz,
										  int32_t			   x,
										  int32_t			   y,
										  int32_t			   z);
static void rft_svo_emit_leaf_faces(const rft_svo_chunk* self,
									const rft_svo_chunk* neighbor_px,
									const rft_svo_chunk* neighbor_nx,
									const rft_svo_chunk* neighbor_py,
									const rft_svo_chunk* neighbor_ny,
									const rft_svo_chunk* neighbor_pz,
									const rft_svo_chunk* neighbor_nz,
									int32_t				 chunk_x,
									int32_t				 chunk_y,
									int32_t				 chunk_z,
									uint32_t			 x0,
									uint32_t			 y0,
									uint32_t			 z0,
									uint32_t			 size,
									rft_voxel_face*		 out_faces,
									size_t*				 count);
static void rft_svo_mesh_node(const rft_svo_chunk* self,
							  const rft_svo_chunk* neighbor_px,
							  const rft_svo_chunk* neighbor_nx,
							  const rft_svo_chunk* neighbor_py,
							  const rft_svo_chunk* neighbor_ny,
							  const rft_svo_chunk* neighbor_pz,
							  const rft_svo_chunk* neighbor_nz,
							  int32_t			   chunk_x,
							  int32_t			   chunk_y,
							  int32_t			   chunk_z,
							  uint32_t			   node_idx,
							  uint32_t			   x0,
							  uint32_t			   y0,
							  uint32_t			   z0,
							  uint32_t			   size,
							  rft_voxel_face*	   out_faces,
							  size_t*			   count);

static uint8_t rft_svo_sanitize_leaf_size(uint8_t leaf_size)
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

static bool rft_svo_chunk_reserve(rft_svo_chunk* chunk, uint32_t additional)
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

static void rft_svo_chunk_compact(rft_svo_chunk* chunk)
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

static uint32_t rft_svo_child_rank(uint8_t child_mask, uint32_t child)
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

static void rft_svo_mesh_2d_plane(uint64_t*		  masks,
								  uint16_t		  material,
								  uint8_t		  normal,
								  uint32_t		  c_val,
								  uint8_t		  u_axis,
								  uint8_t		  v_axis,
								  uint8_t		  w_axis,
								  rft_voxel_face* out,
								  size_t*		  count)
{
	for (uint32_t v = 0; v < 64U; ++v)
	{
		while (masks[v])
		{
			uint64_t row		 = masks[v];
			uint32_t u			 = (uint32_t)__builtin_ctzll(row);
			uint64_t mask_from_u = row >> u;
			uint32_t w			 = (mask_from_u == ~0ULL) ? (64U - u) : (uint32_t)__builtin_ctzll(~mask_from_u);
			uint64_t u_mask		 = (w == 64U) ? ~0ULL : (((1ULL << w) - 1ULL) << u);
			uint32_t h			 = 1U;

			while (v + h < 64U && (masks[v + h] & u_mask) == u_mask)
			{
				masks[v + h] &= ~u_mask;
				h++;
			}

			uint32_t coords[3] = { 0, 0, 0 };
			coords[u_axis]	   = u;
			coords[v_axis]	   = v;
			coords[w_axis]	   = c_val;

			uint32_t final_w = w;
			uint32_t final_h = h;

			if (normal >= 2U)
			{
				final_w = h;
				final_h = w;
			}

			ASSERT_FATAL(*count < RFT_SVO_MAX_FACES);
			out[(*count)++] = rft_voxel_face_pack(coords[0], coords[1], coords[2], final_w, final_h, normal, material);

			masks[v] &= ~u_mask;
		}
	}
}

static bool rft_svo_sample_with_neighbors(const rft_svo_chunk* self,
										  const rft_svo_chunk* neighbor_px,
										  const rft_svo_chunk* neighbor_nx,
										  const rft_svo_chunk* neighbor_py,
										  const rft_svo_chunk* neighbor_ny,
										  const rft_svo_chunk* neighbor_pz,
										  const rft_svo_chunk* neighbor_nz,
										  int32_t			   x,
										  int32_t			   y,
										  int32_t			   z)
{
	if (x < 0)
	{
		return neighbor_nx ? rft_svo_chunk_sample(neighbor_nx, (uint32_t)(x + RFT_CHUNK_SIZE), (uint32_t)y, (uint32_t)z) : false;
	}

	if (x >= RFT_CHUNK_SIZE)
	{
		return neighbor_px ? rft_svo_chunk_sample(neighbor_px, (uint32_t)(x - RFT_CHUNK_SIZE), (uint32_t)y, (uint32_t)z) : false;
	}

	if (z < 0)
	{
		return neighbor_nz ? rft_svo_chunk_sample(neighbor_nz, (uint32_t)x, (uint32_t)y, (uint32_t)(z + RFT_CHUNK_SIZE)) : false;
	}

	if (z >= RFT_CHUNK_SIZE)
	{
		return neighbor_pz ? rft_svo_chunk_sample(neighbor_pz, (uint32_t)x, (uint32_t)y, (uint32_t)(z - RFT_CHUNK_SIZE)) : false;
	}

	if (y < 0)
	{
		return neighbor_ny ? rft_svo_chunk_sample(neighbor_ny, (uint32_t)x, (uint32_t)(y + RFT_CHUNK_SIZE), (uint32_t)z) : true;
	}

	if (y >= RFT_CHUNK_SIZE)
	{
		return neighbor_py ? rft_svo_chunk_sample(neighbor_py, (uint32_t)x, (uint32_t)(y - RFT_CHUNK_SIZE), (uint32_t)z) : false;
	}

	return rft_svo_chunk_sample(self, (uint32_t)x, (uint32_t)y, (uint32_t)z);
}

static void rft_svo_emit_leaf_faces(const rft_svo_chunk* self,
									const rft_svo_chunk* neighbor_px,
									const rft_svo_chunk* neighbor_nx,
									const rft_svo_chunk* neighbor_py,
									const rft_svo_chunk* neighbor_ny,
									const rft_svo_chunk* neighbor_pz,
									const rft_svo_chunk* neighbor_nz,
									int32_t				 chunk_x,
									int32_t				 chunk_y,
									int32_t				 chunk_z,
									uint32_t			 x0,
									uint32_t			 y0,
									uint32_t			 z0,
									uint32_t			 size,
									rft_voxel_face*		 out_faces,
									size_t*				 count)
{
	uint64_t masks[RFT_TERRAIN_MATERIAL_COUNT][64];
	memset(masks, 0, sizeof(masks));

	for (uint32_t z = 0; z < size; ++z)
	{
		for (uint32_t y = 0; y < size; ++y)
		{
			int32_t sample_x = (int32_t)(x0 + size);
			int32_t sample_y = (int32_t)(y0 + y);
			int32_t sample_z = (int32_t)(z0 + z);

			if (!rft_svo_sample_with_neighbors(self,
											   neighbor_px,
											   neighbor_nx,
											   neighbor_py,
											   neighbor_ny,
											   neighbor_pz,
											   neighbor_nz,
											   sample_x,
											   sample_y,
											   sample_z))
			{
				int world_x = chunk_x * RFT_CHUNK_SIZE + (int)(x0 + size - 1U);
				int world_y = chunk_y * RFT_CHUNK_SIZE + (int)(y0 + y);
				int world_z = chunk_z * RFT_CHUNK_SIZE + (int)(z0 + z);

				uint8_t material = rft_terrain_material(world_x, world_y, world_z, 0);
				masks[material][z] |= 1ULL << y;
			}
		}
	}

	for (uint32_t material = 0; material < RFT_TERRAIN_MATERIAL_COUNT; ++material)
	{
		rft_svo_mesh_2d_plane(masks[material], (uint16_t)material, 0, x0 + size - 1U, 1, 2, 0, out_faces, count);
	}

	memset(masks, 0, sizeof(masks));

	for (uint32_t z = 0; z < size; ++z)
	{
		for (uint32_t y = 0; y < size; ++y)
		{
			int32_t sample_x = (int32_t)x0 - 1;
			int32_t sample_y = (int32_t)(y0 + y);
			int32_t sample_z = (int32_t)(z0 + z);

			if (!rft_svo_sample_with_neighbors(self,
											   neighbor_px,
											   neighbor_nx,
											   neighbor_py,
											   neighbor_ny,
											   neighbor_pz,
											   neighbor_nz,
											   sample_x,
											   sample_y,
											   sample_z))
			{
				int		world_x	 = chunk_x * RFT_CHUNK_SIZE + (int)x0;
				int		world_y	 = chunk_y * RFT_CHUNK_SIZE + (int)(y0 + y);
				int		world_z	 = chunk_z * RFT_CHUNK_SIZE + (int)(z0 + z);
				uint8_t material = rft_terrain_material(world_x, world_y, world_z, 1);
				masks[material][z] |= 1ULL << y;
			}
		}
	}

	for (uint32_t material = 0; material < RFT_TERRAIN_MATERIAL_COUNT; ++material)
	{
		rft_svo_mesh_2d_plane(masks[material], (uint16_t)material, 1, x0, 1, 2, 0, out_faces, count);
	}

	memset(masks, 0, sizeof(masks));

	for (uint32_t z = 0; z < size; ++z)
	{
		for (uint32_t x = 0; x < size; ++x)
		{
			int32_t sample_x = (int32_t)(x0 + x);
			int32_t sample_y = (int32_t)(y0 + size);
			int32_t sample_z = (int32_t)(z0 + z);

			if (!rft_svo_sample_with_neighbors(self,
											   neighbor_px,
											   neighbor_nx,
											   neighbor_py,
											   neighbor_ny,
											   neighbor_pz,
											   neighbor_nz,
											   sample_x,
											   sample_y,
											   sample_z))
			{
				int world_x = chunk_x * RFT_CHUNK_SIZE + (int)(x0 + x);
				int world_y = chunk_y * RFT_CHUNK_SIZE + (int)(y0 + size - 1U);
				int world_z = chunk_z * RFT_CHUNK_SIZE + (int)(z0 + z);

				uint8_t material = rft_terrain_material(world_x, world_y, world_z, 2);
				masks[material][z] |= 1ULL << x;
			}
		}
	}

	for (uint32_t material = 0; material < RFT_TERRAIN_MATERIAL_COUNT; ++material)
	{
		rft_svo_mesh_2d_plane(masks[material], (uint16_t)material, 2, y0 + size - 1U, 0, 2, 1, out_faces, count);
	}

	memset(masks, 0, sizeof(masks));

	for (uint32_t z = 0; z < size; ++z)
	{
		for (uint32_t x = 0; x < size; ++x)
		{
			int32_t sample_x = (int32_t)(x0 + x);
			int32_t sample_y = (int32_t)y0 - 1;
			int32_t sample_z = (int32_t)(z0 + z);

			if (!rft_svo_sample_with_neighbors(self,
											   neighbor_px,
											   neighbor_nx,
											   neighbor_py,
											   neighbor_ny,
											   neighbor_pz,
											   neighbor_nz,
											   sample_x,
											   sample_y,
											   sample_z))
			{
				int world_x = chunk_x * RFT_CHUNK_SIZE + (int)(x0 + x);
				int world_y = chunk_y * RFT_CHUNK_SIZE + (int)y0;
				int world_z = chunk_z * RFT_CHUNK_SIZE + (int)(z0 + z);

				uint8_t material = rft_terrain_material(world_x, world_y, world_z, 3);
				masks[material][z] |= 1ULL << x;
			}
		}
	}

	for (uint32_t material = 0; material < RFT_TERRAIN_MATERIAL_COUNT; ++material)
	{
		rft_svo_mesh_2d_plane(masks[material], (uint16_t)material, 3, y0, 0, 2, 1, out_faces, count);
	}

	memset(masks, 0, sizeof(masks));

	for (uint32_t x = 0; x < size; ++x)
	{
		for (uint32_t y = 0; y < size; ++y)
		{
			int32_t sample_x = (int32_t)(x0 + x);
			int32_t sample_y = (int32_t)(y0 + y);
			int32_t sample_z = (int32_t)(z0 + size);

			if (!rft_svo_sample_with_neighbors(self,
											   neighbor_px,
											   neighbor_nx,
											   neighbor_py,
											   neighbor_ny,
											   neighbor_pz,
											   neighbor_nz,
											   sample_x,
											   sample_y,
											   sample_z))
			{
				int world_x = chunk_x * RFT_CHUNK_SIZE + (int)(x0 + x);
				int world_y = chunk_y * RFT_CHUNK_SIZE + (int)(y0 + y);
				int world_z = chunk_z * RFT_CHUNK_SIZE + (int)(z0 + size - 1U);

				uint8_t material = rft_terrain_material(world_x, world_y, world_z, 4);
				masks[material][x] |= 1ULL << y;
			}
		}
	}

	for (uint32_t material = 0; material < RFT_TERRAIN_MATERIAL_COUNT; ++material)
	{
		rft_svo_mesh_2d_plane(masks[material], (uint16_t)material, 4, z0 + size - 1U, 1, 0, 2, out_faces, count);
	}

	memset(masks, 0, sizeof(masks));

	for (uint32_t x = 0; x < size; ++x)
	{
		for (uint32_t y = 0; y < size; ++y)
		{
			int32_t sample_x = (int32_t)(x0 + x);
			int32_t sample_y = (int32_t)(y0 + y);
			int32_t sample_z = (int32_t)z0 - 1;

			if (!rft_svo_sample_with_neighbors(self,
											   neighbor_px,
											   neighbor_nx,
											   neighbor_py,
											   neighbor_ny,
											   neighbor_pz,
											   neighbor_nz,
											   sample_x,
											   sample_y,
											   sample_z))
			{
				int world_x = chunk_x * RFT_CHUNK_SIZE + (int)(x0 + x);
				int world_y = chunk_y * RFT_CHUNK_SIZE + (int)(y0 + y);
				int world_z = chunk_z * RFT_CHUNK_SIZE + (int)z0;

				uint8_t material = rft_terrain_material(world_x, world_y, world_z, 5);
				masks[material][x] |= 1ULL << y;
			}
		}
	}

	for (uint32_t material = 0; material < RFT_TERRAIN_MATERIAL_COUNT; ++material)
	{
		rft_svo_mesh_2d_plane(masks[material], (uint16_t)material, 5, z0, 1, 0, 2, out_faces, count);
	}
}

static void rft_svo_mesh_node(const rft_svo_chunk* self,
							  const rft_svo_chunk* neighbor_px,
							  const rft_svo_chunk* neighbor_nx,
							  const rft_svo_chunk* neighbor_py,
							  const rft_svo_chunk* neighbor_ny,
							  const rft_svo_chunk* neighbor_pz,
							  const rft_svo_chunk* neighbor_nz,
							  int32_t			   chunk_x,
							  int32_t			   chunk_y,
							  int32_t			   chunk_z,
							  uint32_t			   node_idx,
							  uint32_t			   x0,
							  uint32_t			   y0,
							  uint32_t			   z0,
							  uint32_t			   size,
							  rft_voxel_face*	   out_faces,
							  size_t*			   count)
{
	const rft_svo_node* node = &self->nodes[node_idx];

	if (node->is_leaf)
	{
		rft_svo_emit_leaf_faces(self,
								neighbor_px,
								neighbor_nx,
								neighbor_py,
								neighbor_ny,
								neighbor_pz,
								neighbor_nz,
								chunk_x,
								chunk_y,
								chunk_z,
								x0,
								y0,
								z0,
								size,
								out_faces,
								count);
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

		rft_svo_mesh_node(self,
						  neighbor_px,
						  neighbor_nx,
						  neighbor_py,
						  neighbor_ny,
						  neighbor_pz,
						  neighbor_nz,
						  chunk_x,
						  chunk_y,
						  chunk_z,
						  child_idx,
						  child_x,
						  child_y,
						  child_z,
						  child_size,
						  out_faces,
						  count);
	}
}

size_t rft_svo_chunk_mesh(const rft_svo_chunk* self,
						  const rft_svo_chunk* neighbor_px,
						  const rft_svo_chunk* neighbor_nx,
						  const rft_svo_chunk* neighbor_py,
						  const rft_svo_chunk* neighbor_ny,
						  const rft_svo_chunk* neighbor_pz,
						  const rft_svo_chunk* neighbor_nz,
						  int32_t			   chunk_x,
						  int32_t			   chunk_y,
						  int32_t			   chunk_z,
						  rft_voxel_face*	   out_faces)
{
	ASSERT_FATAL(self);
	ASSERT_FATAL(out_faces);

	if (self->node_count == 0)
	{
		return 0;
	}

	size_t count = 0;
	rft_svo_mesh_node(self,
					  neighbor_px,
					  neighbor_nx,
					  neighbor_py,
					  neighbor_ny,
					  neighbor_pz,
					  neighbor_nz,
					  chunk_x,
					  chunk_y,
					  chunk_z,
					  0,
					  0,
					  0,
					  0,
					  RFT_CHUNK_SIZE,
					  out_faces,
					  &count);
	return count;
}
