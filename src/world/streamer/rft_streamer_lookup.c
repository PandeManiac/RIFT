#include "rft_streamer_internal.h"

static const ivec3s neighbor_offsets[] = {
	{ { 1, 0, 0 } }, { { -1, 0, 0 } }, { { 0, 1, 0 } }, { { 0, -1, 0 } }, { { 0, 0, 1 } }, { { 0, 0, -1 } },
};

static ALWAYS_INLINE inline uint32_t rft_chunk_hash(ivec3s pos)
{
	uint32_t h = (uint32_t)pos.x * 73856093U ^ (uint32_t)pos.y * 19349663U ^ (uint32_t)pos.z * 83492791U;
	return h;
}

static ALWAYS_INLINE inline uint32_t rft_streamer_column_hash(int x, int z)
{
	return (uint32_t)x * 73856093U ^ (uint32_t)z * 83492791U;
}

static int rft_streamer_compute_column_top_chunk_y(int chunk_x, int chunk_z)
{
	uint8_t max_height = 0U;
	int		base_x	   = chunk_x * RFT_CHUNK_SIZE;
	int		base_z	   = chunk_z * RFT_CHUNK_SIZE;

	for (uint32_t sample_x = 0; sample_x < RFT_STREAMER_HEIGHT_SAMPLES_PER_AXIS; ++sample_x)
	{
		uint32_t local_x = (sample_x * (RFT_CHUNK_SIZE - 1U)) / (RFT_STREAMER_HEIGHT_SAMPLES_PER_AXIS - 1U);

		for (uint32_t sample_z = 0; sample_z < RFT_STREAMER_HEIGHT_SAMPLES_PER_AXIS; ++sample_z)
		{
			uint32_t local_z = (sample_z * (RFT_CHUNK_SIZE - 1U)) / (RFT_STREAMER_HEIGHT_SAMPLES_PER_AXIS - 1U);
			uint8_t	 height	 = rft_terrain_height(base_x + (int)local_x, base_z + (int)local_z);

			if (height > max_height)
			{
				max_height = height;
			}
		}
	}

	if (max_height == 0U)
	{
		return -1;
	}

	int top_chunk_y = (((int)max_height - 1) + (int)RFT_STREAMER_HEIGHT_GUARD_BLOCKS) / RFT_CHUNK_SIZE;
	int max_chunk_y = ((int)RFT_TERRAIN_MAX_HEIGHT - 1) / RFT_CHUNK_SIZE;

	if (top_chunk_y > max_chunk_y)
	{
		top_chunk_y = max_chunk_y;
	}

	return top_chunk_y;
}

static void rft_streamer_request_chunk_remesh(rft_streamer* streamer, uint32_t idx)
{
	ASSERT_FATAL(streamer);
	ASSERT_FATAL(idx < streamer->chunk_count);

	rft_chunk_metadata* chunk = &streamer->chunks[idx];
	rft_chunk_state		state = atomic_load_explicit(&chunk->state, memory_order_acquire);

	switch (state)
	{
		case CHUNK_STATE_ACTIVE:
		case CHUNK_STATE_MESHING:
			atomic_store_explicit(&chunk->remesh_pending, true, memory_order_release);
			break;

		case CHUNK_STATE_VOXELS_READY:
		case CHUNK_STATE_EMPTY:
		case CHUNK_STATE_GENERATING_VOXELS:
			break;

		default:
			UNREACHABLE;
	}
}

int rft_streamer_estimate_column_top_chunk_y(rft_streamer* streamer, int chunk_x, int chunk_z)
{
	ASSERT_FATAL(streamer);

	if (!streamer->column_height_cache || streamer->column_height_cache_capacity == 0U)
	{
		return rft_streamer_compute_column_top_chunk_y(chunk_x, chunk_z);
	}

	uint32_t idx = rft_streamer_column_hash(chunk_x, chunk_z) % streamer->column_height_cache_capacity;

	for (uint32_t probe = 0; probe < streamer->column_height_cache_capacity; ++probe)
	{
		rft_streamer_column_height_cache_entry* entry = &streamer->column_height_cache[idx];

		if (!entry->occupied)
		{
			entry->x		   = chunk_x;
			entry->z		   = chunk_z;
			entry->top_chunk_y = rft_streamer_compute_column_top_chunk_y(chunk_x, chunk_z);
			entry->occupied	   = true;

			return entry->top_chunk_y;
		}

		if (entry->x == chunk_x && entry->z == chunk_z)
		{
			return entry->top_chunk_y;
		}

		idx = (idx + 1U) % streamer->column_height_cache_capacity;
	}

	return rft_streamer_compute_column_top_chunk_y(chunk_x, chunk_z);
}

void rft_hash_table_insert(rft_streamer* streamer, ivec3s pos, uint32_t chunk_idx)
{
	RFT_RWLOCK_WRLOCK(&streamer->hash_mutex);
	uint32_t h	 = rft_chunk_hash(pos);
	uint32_t idx = h % streamer->hash_capacity;

	while (streamer->hash_table[idx] != 0xFFFFFFFFU)
	{
		idx = (idx + 1U) % streamer->hash_capacity;
	}

	streamer->hash_table[idx] = chunk_idx;
	RFT_RWLOCK_UNLOCK_WR(&streamer->hash_mutex);
}

void rft_hash_table_remove(rft_streamer* streamer, ivec3s pos)
{
	RFT_RWLOCK_WRLOCK(&streamer->hash_mutex);
	uint32_t h	 = rft_chunk_hash(pos);
	uint32_t idx = h % streamer->hash_capacity;

	while (streamer->hash_table[idx] != 0xFFFFFFFFU)
	{
		uint32_t chunk_idx = streamer->hash_table[idx];

		if (streamer->chunks[chunk_idx].pos.x == pos.x && streamer->chunks[chunk_idx].pos.y == pos.y && streamer->chunks[chunk_idx].pos.z == pos.z)
		{
			streamer->hash_table[idx] = 0xFFFFFFFFU;
			uint32_t next			  = (idx + 1U) % streamer->hash_capacity;

			while (streamer->hash_table[next] != 0xFFFFFFFFU)
			{
				uint32_t move_idx		   = streamer->hash_table[next];
				streamer->hash_table[next] = 0xFFFFFFFFU;

				uint32_t h2	  = rft_chunk_hash(streamer->chunks[move_idx].pos);
				uint32_t idx2 = h2 % streamer->hash_capacity;

				while (streamer->hash_table[idx2] != 0xFFFFFFFFU)
				{
					idx2 = (idx2 + 1U) % streamer->hash_capacity;
				}

				streamer->hash_table[idx2] = move_idx;

				next = (next + 1U) % streamer->hash_capacity;
			}

			RFT_RWLOCK_UNLOCK_WR(&streamer->hash_mutex);
			return;
		}

		idx = (idx + 1U) % streamer->hash_capacity;
	}

	RFT_RWLOCK_UNLOCK_WR(&streamer->hash_mutex);
}

bool rft_get_chunk_idx(rft_streamer* streamer, ivec3s pos, uint32_t* out_idx)
{
	RFT_RWLOCK_RDLOCK(&streamer->hash_mutex);
	uint32_t h	 = rft_chunk_hash(pos);
	uint32_t idx = h % streamer->hash_capacity;

	while (streamer->hash_table[idx] != 0xFFFFFFFFU)
	{
		uint32_t chunk_idx = streamer->hash_table[idx];

		if (streamer->chunks[chunk_idx].pos.x == pos.x && streamer->chunks[chunk_idx].pos.y == pos.y && streamer->chunks[chunk_idx].pos.z == pos.z)
		{
			if (out_idx)
			{
				*out_idx = chunk_idx;
			}

			RFT_RWLOCK_UNLOCK_RD(&streamer->hash_mutex);
			return true;
		}

		idx = (idx + 1U) % streamer->hash_capacity;
	}

	RFT_RWLOCK_UNLOCK_RD(&streamer->hash_mutex);
	return false;
}

void rft_streamer_request_neighbor_remeshes(rft_streamer* streamer, ivec3s pos)
{
	for (size_t i = 0; i < sizeof(neighbor_offsets) / sizeof(neighbor_offsets[0]); ++i)
	{
		uint32_t idx		  = 0U;
		ivec3s	 neighbor_pos = { { pos.x + neighbor_offsets[i].x, pos.y + neighbor_offsets[i].y, pos.z + neighbor_offsets[i].z } };

		if (rft_get_chunk_idx(streamer, neighbor_pos, &idx))
		{
			rft_streamer_request_chunk_remesh(streamer, idx);
		}
	}
}

void rft_streamer_release_chunk_storage(rft_streamer* streamer, uint32_t idx)
{
	rft_chunk_metadata* chunk = &streamer->chunks[idx];
	RFT_RWLOCK_WRLOCK(&chunk->storage_lock);

	rft_streamer_clear_chunk_mesh(streamer, idx);
	rft_svo_chunk_destroy(&streamer->svo_data[idx]);

	chunk->memory_bytes = 0;

	atomic_store_explicit(&chunk->remesh_pending, false, memory_order_relaxed);
	RFT_RWLOCK_UNLOCK_WR(&chunk->storage_lock);
}
