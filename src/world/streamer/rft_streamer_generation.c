#include "rft_streamer_internal.h"

uint8_t rft_streamer_desired_leaf_size(const rft_streamer* streamer, ivec3s camera_chunk, ivec3s chunk_pos)
{
	ASSERT_FATAL(streamer);

	float dx = (float)(chunk_pos.x - camera_chunk.x);
	float dy = (float)(chunk_pos.y - camera_chunk.y);
	float dz = (float)(chunk_pos.z - camera_chunk.z);
	float d2 = dx * dx + dy * dy + dz * dz;
	float r	 = (float)streamer->cfg.near_chunk_radius;

	return d2 <= r * r ? 1U : RFT_STREAMER_FAR_SVO_LEAF_SIZE;
}

bool rft_generate_dense_chunk(rft_chunk* chunk, ivec3s pos)
{
	ASSERT_FATAL(chunk);

	memset(chunk, 0, sizeof(*chunk));

	bool has_voxels = false;
	int	 base_x		= pos.x * RFT_CHUNK_SIZE;
	int	 base_y		= pos.y * RFT_CHUNK_SIZE;
	int	 base_z		= pos.z * RFT_CHUNK_SIZE;

	for (uint32_t x = 0; x < RFT_CHUNK_SIZE; ++x)
	{
		for (uint32_t z = 0; z < RFT_CHUNK_SIZE; ++z)
		{
			int height = (int)rft_terrain_height(base_x + (int)x, base_z + (int)z) - base_y;
			if (height <= 0)
			{
				continue;
			}

			uint64_t column		 = height >= RFT_CHUNK_SIZE ? ~0ULL : ((1ULL << height) - 1ULL);
			chunk->columns[x][z] = column;
			has_voxels |= column != 0ULL;
		}
	}

	return has_voxels;
}
