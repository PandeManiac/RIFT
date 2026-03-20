#include "rft_svo_internal.h"

#include "world/rft_terrain.h"

#include <string.h>

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
