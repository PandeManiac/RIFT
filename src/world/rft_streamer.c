#include "streamer/rft_streamer_internal.h"
#include "utils/rft_assert.h"

static void streamer_enqueue_chunk(rft_streamer_grid_pos* queue, char* visited, int* q_tail, int center, int dim, int loop_radius, int x, int z)
{
	if (x < -loop_radius || x > loop_radius || z < -loop_radius || z > loop_radius)
	{
		return;
	}

	int idx = (center + x) * dim + (center + z);

	if (visited[idx])
	{
		return;
	}

	visited[idx]   = 1;
	queue[*q_tail] = (rft_streamer_grid_pos) { x, z };
	(*q_tail)++;
}

static size_t rft_streamer_bfs_capacity(float render_distance)
{
	float  radius_chunks = ceilf(render_distance / 64.0f);
	size_t dim			 = (size_t)((radius_chunks * 2.0f) + 1.0f);
	return dim * dim;
}

static float rft_streamer_effective_render_distance(const rft_streamer* streamer, const rft_camera* camera)
{
	ASSERT_FATAL(streamer);
	ASSERT_FATAL(camera);
	return fminf(streamer->cfg.render_distance, camera->cfg.far);
}

rft_streamer* rft_streamer_create(const rft_streamer_config* cfg)
{
	rft_streamer* streamer = calloc(1, sizeof(rft_streamer));
	ASSERT_FATAL(streamer);

	streamer->cfg						= *cfg;
	streamer->pool						= rft_thread_pool_create(cfg->thread_count);
	streamer->effective_render_distance = cfg->render_distance;
	streamer->chunk_count				= cfg->max_chunks;
	streamer->chunks					= calloc(streamer->chunk_count, sizeof(rft_chunk_metadata));
	streamer->svo_data					= calloc(streamer->chunk_count, sizeof(rft_svo_chunk));

	ASSERT_FATAL(streamer->svo_data);

	for (uint32_t i = 0; i < streamer->chunk_count; ++i)
	{
		rft_svo_chunk_init(&streamer->svo_data[i]);
		pthread_rwlock_init(&streamer->chunks[i].storage_lock, NULL);

		atomic_store_explicit(&streamer->chunks[i].face_range, 0U, memory_order_relaxed);
		atomic_store_explicit(&streamer->chunks[i].remesh_pending, false, memory_order_relaxed);
		atomic_store_explicit(&streamer->chunks[i].slot_version, 1U, memory_order_relaxed);
		atomic_store_explicit(&streamer->chunks[i].voxel_version, 0U, memory_order_relaxed);
		atomic_store_explicit(&streamer->chunks[i].blocked_face_epoch, 0U, memory_order_relaxed);
	}

	pthread_mutex_init(&streamer->face_alloc_mutex, NULL);

	streamer->face_free_range_capacity = streamer->chunk_count * 2U + 1U;
	streamer->face_free_ranges		   = calloc(streamer->face_free_range_capacity, sizeof(rft_face_range));

	ASSERT_FATAL(streamer->face_free_ranges);

	streamer->deferred_face_free_capacity = streamer->chunk_count;
	streamer->deferred_face_frees		  = calloc(streamer->deferred_face_free_capacity, sizeof(rft_deferred_face_free));

	ASSERT_FATAL(streamer->deferred_face_frees);

	streamer->free_stack = malloc(streamer->chunk_count * sizeof(uint32_t));

	for (uint32_t i = 0; i < streamer->chunk_count; ++i)
	{
		streamer->free_stack[i] = i;
	}
	streamer->free_top		= streamer->chunk_count;
	streamer->hash_capacity = streamer->chunk_count * 2U;
	streamer->hash_table	= malloc(streamer->hash_capacity * sizeof(uint32_t));

	memset(streamer->hash_table, 0xFF, streamer->hash_capacity * sizeof(uint32_t));
	pthread_rwlock_init(&streamer->hash_mutex, NULL);

	streamer->column_height_cache_capacity = streamer->hash_capacity;
	streamer->column_height_cache		   = calloc(streamer->column_height_cache_capacity, sizeof(rft_streamer_column_height_cache_entry));

	ASSERT_FATAL(streamer->column_height_cache);

	streamer->bfs_capacity = rft_streamer_bfs_capacity(cfg->render_distance);
	streamer->bfs_visited  = calloc(streamer->bfs_capacity, sizeof(char));
	streamer->bfs_queue	   = calloc(streamer->bfs_capacity, sizeof(rft_streamer_grid_pos));

	ASSERT_FATAL(streamer->bfs_visited);
	ASSERT_FATAL(streamer->bfs_queue);

	rft_buffer_init(&streamer->face_ssbo, RFT_BUFFER_SHADER_STORAGE);
	rft_buffer_set_data_persistent(&streamer->face_ssbo, rft_streamer_face_buffer_size(streamer->chunk_count));

	for (uint32_t i = 0; i < RFT_STREAMER_BUFFERED_FRAMES; ++i)
	{
		rft_buffer_init(&streamer->metadata_ssbos[i], RFT_BUFFER_SHADER_STORAGE);
		rft_buffer_set_data_persistent(&streamer->metadata_ssbos[i], streamer->chunk_count * sizeof(rft_chunk_gpu_metadata));

		rft_buffer_init(&streamer->indirect_buffers[i], RFT_BUFFER_INDIRECT);
		rft_buffer_set_data_persistent(&streamer->indirect_buffers[i], streamer->chunk_count * sizeof(DrawArraysIndirectCommand));

		streamer->frame_fences[i]			   = NULL;
		streamer->frame_fence_submit_counts[i] = 0U;
	}

	streamer->last_camera_chunk = (ivec3s) { { 1000000, 1000000, 1000000 } };
	return streamer;
}

void rft_streamer_destroy(rft_streamer* streamer)
{
	rft_thread_pool_destroy(streamer->pool);
	rft_buffer_destroy(&streamer->face_ssbo);

	for (uint32_t i = 0; i < RFT_STREAMER_BUFFERED_FRAMES; ++i)
	{
		if (streamer->frame_fences[i] != NULL)
		{
			glDeleteSync(streamer->frame_fences[i]);
			streamer->frame_fences[i] = NULL;
		}

		rft_buffer_destroy(&streamer->metadata_ssbos[i]);
		rft_buffer_destroy(&streamer->indirect_buffers[i]);
	}

	pthread_rwlock_destroy(&streamer->hash_mutex);
	pthread_mutex_destroy(&streamer->face_alloc_mutex);

	free(streamer->hash_table);
	free(streamer->column_height_cache);
	free(streamer->free_stack);
	free(streamer->face_free_ranges);
	free(streamer->deferred_face_frees);
	free(streamer->bfs_queue);
	free(streamer->bfs_visited);

	for (uint32_t i = 0; i < streamer->chunk_count; ++i)
	{
		rft_svo_chunk_destroy(&streamer->svo_data[i]);
		pthread_rwlock_destroy(&streamer->chunks[i].storage_lock);
	}

	free(streamer->svo_data);
	free(streamer->chunks);
	free(streamer);
}

void rft_streamer_update(rft_streamer* streamer, const rft_camera* camera)
{
	float cam_xf = floorf(camera->cfg.pos.x / 64.0f);
	float cam_yf = floorf(camera->cfg.pos.y / 64.0f);
	float cam_zf = floorf(camera->cfg.pos.z / 64.0f);

	ivec3s cam_chunk = { { (int)cam_xf, (int)cam_yf, (int)cam_zf } };

	bool moved = (cam_chunk.x != streamer->last_camera_chunk.x || cam_chunk.y != streamer->last_camera_chunk.y || cam_chunk.z != streamer->last_camera_chunk.z);
	bool resize_pending = false;

	size_t requested_capacity = 0U;

	pthread_mutex_lock(&streamer->face_alloc_mutex);

	resize_pending	   = streamer->face_resize_pending;
	requested_capacity = streamer->face_requested_capacity;

	pthread_mutex_unlock(&streamer->face_alloc_mutex);

	rft_streamer_flush_deferred_face_frees(streamer);

	if (resize_pending && requested_capacity > streamer->face_ssbo.size)
	{
		rft_streamer_resize_face_buffer(streamer, requested_capacity);
	}

	float effective_render_distance		= rft_streamer_effective_render_distance(streamer, camera);
	streamer->effective_render_distance = effective_render_distance;

	float outer_r	  = effective_render_distance / 64.0f;
	float outer_r_sq  = outer_r * outer_r;
	float unload_r_sq = outer_r_sq * 1.10f;

	if (moved)
	{
		for (uint32_t i = 0; i < streamer->chunk_count; ++i)
		{
			rft_chunk_state st = atomic_load_explicit(&streamer->chunks[i].state, memory_order_relaxed);

			if (st != CHUNK_STATE_EMPTY && st != CHUNK_STATE_GENERATING_VOXELS && st != CHUNK_STATE_MESHING)
			{
				float dx = (float)(streamer->chunks[i].pos.x - cam_chunk.x);
				float dz = (float)(streamer->chunks[i].pos.z - cam_chunk.z);
				float d2 = dx * dx + dz * dz;

				if (d2 > unload_r_sq)
				{
					ivec3s old_pos = streamer->chunks[i].pos;

					rft_hash_table_remove(streamer, old_pos);
					atomic_store_explicit(&streamer->chunks[i].state, CHUNK_STATE_EMPTY, memory_order_relaxed);

					rft_streamer_release_chunk_storage(streamer, i);
					streamer->free_stack[streamer->free_top++] = i;

					rft_streamer_request_neighbor_remeshes(streamer, old_pos);
				}
			}
		}
		streamer->last_camera_chunk = cam_chunk;
	}

	int max_active_tasks = (int)streamer->cfg.thread_count * RFT_STREAMER_TASKS_PER_THREAD;

	if (max_active_tasks < RFT_STREAMER_MIN_ACTIVE_TASK_BUDGET)
	{
		max_active_tasks = RFT_STREAMER_MIN_ACTIVE_TASK_BUDGET;
	}

	float				   radius_limit = ceilf(outer_r);
	int					   loop_radius	= (int)radius_limit;
	const int			   dim			= loop_radius * 2 + 1;
	const size_t		   grid_elems	= (size_t)dim * (size_t)dim;
	char*				   visited		= streamer->bfs_visited;
	rft_streamer_grid_pos* queue		= streamer->bfs_queue;
	int					   q_head		= 0;
	int					   q_tail		= 0;
	int					   center		= loop_radius;
	bool				   stop			= false;

	ASSERT_FATAL(grid_elems <= streamer->bfs_capacity);
	memset(visited, 0, grid_elems * sizeof(char));

	visited[center * dim + center] = 1;
	queue[q_tail++]				   = (rft_streamer_grid_pos) { 0, 0 };

	while (q_head < q_tail && !stop)
	{
		if (rft_thread_pool_get_active_count(streamer->pool) >= max_active_tasks)
		{
			stop = true;
			break;
		}

		rft_streamer_grid_pos offset  = queue[q_head++];
		int					  x_pos	  = offset.x;
		int					  z_pos	  = offset.z;
		float				  dist_sq = (float)(x_pos * x_pos + z_pos * z_pos);

		if (dist_sq > outer_r_sq)
		{
			continue;
		}

		int top_chunk_y = rft_streamer_estimate_column_top_chunk_y(streamer, cam_chunk.x + x_pos, cam_chunk.z + z_pos);

		if (top_chunk_y < 0)
		{
			continue;
		}

		int start_y = cam_chunk.y;

		if (start_y < 0)
		{
			start_y = 0;
		}

		if (start_y > top_chunk_y)
		{
			start_y = top_chunk_y;
		}

		for (int y_pos = start_y; y_pos >= 0; --y_pos)
		{
			rft_streamer_schedule_chunk(streamer, cam_chunk, (ivec3s) { { cam_chunk.x + x_pos, y_pos, cam_chunk.z + z_pos } });
		}

		for (int y_pos = start_y + 1; y_pos <= top_chunk_y; ++y_pos)
		{
			rft_streamer_schedule_chunk(streamer, cam_chunk, (ivec3s) { { cam_chunk.x + x_pos, y_pos, cam_chunk.z + z_pos } });
		}

		streamer_enqueue_chunk(queue, visited, &q_tail, center, dim, loop_radius, x_pos + 1, z_pos);
		streamer_enqueue_chunk(queue, visited, &q_tail, center, dim, loop_radius, x_pos - 1, z_pos);
		streamer_enqueue_chunk(queue, visited, &q_tail, center, dim, loop_radius, x_pos, z_pos + 1);
		streamer_enqueue_chunk(queue, visited, &q_tail, center, dim, loop_radius, x_pos, z_pos - 1);
	}
}
