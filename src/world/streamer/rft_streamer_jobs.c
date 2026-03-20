#include "rft_streamer_internal.h"

static void rft_generate_voxel_task(void* arg);
static void rft_mesh_task(void* arg);

static const rft_svo_chunk* rft_streamer_lock_neighbor_chunk(rft_streamer* streamer, ivec3s expected_pos, uint32_t* out_idx, bool* out_locked)
{
	ASSERT_FATAL(streamer);
	ASSERT_FATAL(out_idx);
	ASSERT_FATAL(out_locked);

	*out_locked = false;

	if (!rft_get_chunk_idx(streamer, expected_pos, out_idx))
	{
		return NULL;
	}

	rft_chunk_metadata* chunk = &streamer->chunks[*out_idx];

	if (atomic_load_explicit(&chunk->state, memory_order_acquire) < CHUNK_STATE_VOXELS_READY)
	{
		return NULL;
	}

	RFT_RWLOCK_RDLOCK(&chunk->storage_lock);

	if (!rft_streamer_pos_equals(chunk->pos, expected_pos) || atomic_load_explicit(&chunk->state, memory_order_acquire) < CHUNK_STATE_VOXELS_READY)
	{
		RFT_RWLOCK_UNLOCK_RD(&chunk->storage_lock);
		return NULL;
	}

	*out_locked = true;
	return &streamer->svo_data[*out_idx];
}

static bool rft_streamer_chunk_waiting_for_face_space(const rft_streamer* streamer, rft_chunk_metadata* chunk)
{
	ASSERT_FATAL(streamer);
	ASSERT_FATAL(chunk);

	uint64_t blocked_epoch = atomic_load_explicit(&chunk->blocked_face_epoch, memory_order_acquire);

	if (blocked_epoch == 0U)
	{
		return false;
	}

	uint64_t current_epoch = rft_streamer_face_allocator_epoch(streamer);

	if (blocked_epoch != current_epoch)
	{
		atomic_store_explicit(&chunk->blocked_face_epoch, 0U, memory_order_release);
		return false;
	}

	return true;
}

void rft_streamer_schedule_chunk(rft_streamer* streamer, ivec3s cam_chunk, ivec3s pos)
{
	ASSERT_FATAL(streamer);

	uint32_t idx = 0U;

	if (rft_get_chunk_idx(streamer, pos, &idx))
	{
		rft_chunk_metadata* chunk = &streamer->chunks[idx];
		rft_chunk_state		state = atomic_load_explicit(&chunk->state, memory_order_acquire);

		if (state == CHUNK_STATE_VOXELS_READY)
		{
			if (rft_streamer_chunk_waiting_for_face_space(streamer, chunk))
			{
				return;
			}

			rft_chunk_state expected = CHUNK_STATE_VOXELS_READY;

			if (atomic_compare_exchange_strong_explicit(&chunk->state, &expected, CHUNK_STATE_MESHING, memory_order_acq_rel, memory_order_acquire))
			{
				rft_job* job = malloc(sizeof(rft_job));

				*job = (rft_job) { streamer,
								   pos,
								   idx,
								   1U,
								   { 0U, 0U, 0U },
								   atomic_load_explicit(&chunk->slot_version, memory_order_acquire),
								   atomic_load_explicit(&chunk->voxel_version, memory_order_acquire) };

				rft_thread_pool_submit(streamer->pool, rft_mesh_task, job);
			}
		}

		else if (state == CHUNK_STATE_ACTIVE && atomic_load_explicit(&chunk->remesh_pending, memory_order_acquire))
		{
			if (rft_streamer_chunk_waiting_for_face_space(streamer, chunk))
			{
				return;
			}

			rft_chunk_state expected = CHUNK_STATE_ACTIVE;

			if (atomic_compare_exchange_strong_explicit(&chunk->state, &expected, CHUNK_STATE_MESHING, memory_order_acq_rel, memory_order_acquire))
			{
				atomic_store_explicit(&chunk->remesh_pending, false, memory_order_release);
				rft_job* job = malloc(sizeof(rft_job));

				*job = (rft_job) { streamer,
								   pos,
								   idx,
								   1U,
								   { 0U, 0U, 0U },
								   atomic_load_explicit(&chunk->slot_version, memory_order_acquire),
								   atomic_load_explicit(&chunk->voxel_version, memory_order_acquire) };

				rft_thread_pool_submit(streamer->pool, rft_mesh_task, job);
			}
		}
	}
	else if (streamer->free_top > 0)
	{
		rft_chunk_metadata* chunk = NULL;

		idx					= streamer->free_stack[--streamer->free_top];
		chunk				= &streamer->chunks[idx];
		chunk->pos			= pos;
		chunk->memory_bytes = 0;

		rft_streamer_store_face_range(chunk, 0U, 0U);
		uint32_t slot_version = atomic_fetch_add_explicit(&chunk->slot_version, 1U, memory_order_acq_rel) + 1U;

		atomic_store_explicit(&chunk->voxel_version, 0U, memory_order_release);
		atomic_store_explicit(&chunk->blocked_face_epoch, 0U, memory_order_release);
		atomic_store_explicit(&chunk->state, CHUNK_STATE_GENERATING_VOXELS, memory_order_release);

		rft_hash_table_insert(streamer, pos, idx);

		rft_job* job	   = malloc(sizeof(rft_job));
		uint8_t	 leaf_size = rft_streamer_desired_leaf_size(streamer, cam_chunk, pos);
		*job			   = (rft_job) { streamer, pos, idx, leaf_size, { 0U, 0U, 0U }, slot_version, 0U };

		rft_thread_pool_submit(streamer->pool, rft_generate_voxel_task, job);
	}
}

static void rft_generate_voxel_task(void* arg)
{
	rft_job*	  job	   = arg;
	rft_streamer* streamer = job->streamer;
	uint32_t	  idx	   = job->chunk_idx;

	if (atomic_load_explicit(&streamer->chunks[idx].state, memory_order_relaxed) != CHUNK_STATE_GENERATING_VOXELS ||
		streamer->chunks[idx].pos.x != job->pos.x || streamer->chunks[idx].pos.y != job->pos.y || streamer->chunks[idx].pos.z != job->pos.z ||
		atomic_load_explicit(&streamer->chunks[idx].slot_version, memory_order_acquire) != job->slot_version)
	{
		free(job);
		return;
	}

	static _Thread_local rft_chunk tl_source_chunk;

	if (!rft_generate_dense_chunk(&tl_source_chunk, job->pos))
	{
		rft_hash_table_remove(streamer, streamer->chunks[idx].pos);
		streamer->chunks[idx].memory_bytes = 0;

		rft_streamer_store_face_range(&streamer->chunks[idx], 0U, 0U);
		atomic_store_explicit(&streamer->chunks[idx].state, CHUNK_STATE_EMPTY, memory_order_release);
		streamer->free_stack[streamer->free_top++] = idx;

		free(job);
		return;
	}

	RFT_RWLOCK_WRLOCK(&streamer->chunks[idx].storage_lock);

	if (!rft_svo_chunk_build_from_chunk(&streamer->svo_data[idx], &tl_source_chunk, job->leaf_size))
	{
		RFT_RWLOCK_UNLOCK_WR(&streamer->chunks[idx].storage_lock);
		rft_hash_table_remove(streamer, streamer->chunks[idx].pos);

		streamer->chunks[idx].memory_bytes = 0;
		atomic_store_explicit(&streamer->chunks[idx].state, CHUNK_STATE_EMPTY, memory_order_release);
		streamer->free_stack[streamer->free_top++] = idx;

		free(job);
		return;
	}

	streamer->chunks[idx].memory_bytes = rft_svo_chunk_memory_usage(&streamer->svo_data[idx]);
	atomic_fetch_add_explicit(&streamer->chunks[idx].voxel_version, 1U, memory_order_acq_rel);
	RFT_RWLOCK_UNLOCK_WR(&streamer->chunks[idx].storage_lock);

	atomic_store_explicit(&streamer->chunks[idx].blocked_face_epoch, 0U, memory_order_release);
	atomic_store_explicit(&streamer->chunks[idx].remesh_pending, false, memory_order_relaxed);
	atomic_store_explicit(&streamer->chunks[idx].state, CHUNK_STATE_VOXELS_READY, memory_order_release);

	rft_streamer_request_neighbor_remeshes(streamer, job->pos);
	free(job);
}

static void rft_mesh_task(void* arg)
{
	rft_job*	  job	   = arg;
	rft_streamer* streamer = job->streamer;
	uint32_t	  idx	   = job->chunk_idx;

	if (atomic_load_explicit(&streamer->chunks[idx].state, memory_order_relaxed) != CHUNK_STATE_MESHING || streamer->chunks[idx].pos.x != job->pos.x ||
		streamer->chunks[idx].pos.y != job->pos.y || streamer->chunks[idx].pos.z != job->pos.z ||
		atomic_load_explicit(&streamer->chunks[idx].slot_version, memory_order_acquire) != job->slot_version ||
		atomic_load_explicit(&streamer->chunks[idx].voxel_version, memory_order_acquire) != job->voxel_version)
	{
		free(job);
		return;
	}

	static _Thread_local rft_voxel_face tl_faces[MAX_CHUNK_FACES];

	static _Thread_local rft_chunk tl_self_chunk;
	static _Thread_local rft_chunk tl_px_chunk;
	static _Thread_local rft_chunk tl_nx_chunk;
	static _Thread_local rft_chunk tl_py_chunk;
	static _Thread_local rft_chunk tl_ny_chunk;
	static _Thread_local rft_chunk tl_pz_chunk;
	static _Thread_local rft_chunk tl_nz_chunk;

	const rft_svo_chunk *self = NULL, *px = NULL, *nx = NULL, *py = NULL, *ny = NULL, *pz = NULL, *nz = NULL;
	uint32_t			 px_idx	   = 0;
	uint32_t			 nx_idx	   = 0;
	uint32_t			 py_idx	   = 0;
	uint32_t			 ny_idx	   = 0;
	uint32_t			 pz_idx	   = 0;
	uint32_t			 nz_idx	   = 0;
	bool				 px_locked = false;
	bool				 nx_locked = false;
	bool				 py_locked = false;
	bool				 ny_locked = false;
	bool				 pz_locked = false;
	bool				 nz_locked = false;

	RFT_RWLOCK_RDLOCK(&streamer->chunks[idx].storage_lock);
	self = &streamer->svo_data[idx];

	px = rft_streamer_lock_neighbor_chunk(streamer, (ivec3s) { { job->pos.x + 1, job->pos.y, job->pos.z } }, &px_idx, &px_locked);
	nx = rft_streamer_lock_neighbor_chunk(streamer, (ivec3s) { { job->pos.x - 1, job->pos.y, job->pos.z } }, &nx_idx, &nx_locked);
	py = rft_streamer_lock_neighbor_chunk(streamer, (ivec3s) { { job->pos.x, job->pos.y + 1, job->pos.z } }, &py_idx, &py_locked);
	ny = rft_streamer_lock_neighbor_chunk(streamer, (ivec3s) { { job->pos.x, job->pos.y - 1, job->pos.z } }, &ny_idx, &ny_locked);
	pz = rft_streamer_lock_neighbor_chunk(streamer, (ivec3s) { { job->pos.x, job->pos.y, job->pos.z + 1 } }, &pz_idx, &pz_locked);
	nz = rft_streamer_lock_neighbor_chunk(streamer, (ivec3s) { { job->pos.x, job->pos.y, job->pos.z - 1 } }, &nz_idx, &nz_locked);

	rft_svo_chunk_rasterize(self, &tl_self_chunk);

	if (px)
	{
		rft_svo_chunk_rasterize(px, &tl_px_chunk);
	}

	if (nx)
	{
		rft_svo_chunk_rasterize(nx, &tl_nx_chunk);
	}

	if (py)
	{
		rft_svo_chunk_rasterize(py, &tl_py_chunk);
	}

	if (ny)
	{
		rft_svo_chunk_rasterize(ny, &tl_ny_chunk);
	}

	if (pz)
	{
		rft_svo_chunk_rasterize(pz, &tl_pz_chunk);
	}

	if (nz)
	{
		rft_svo_chunk_rasterize(nz, &tl_nz_chunk);
	}

	size_t count = rft_chunk_mesh(&tl_self_chunk,
								  px ? &tl_px_chunk : NULL,
								  nx ? &tl_nx_chunk : NULL,
								  py ? &tl_py_chunk : NULL,
								  ny ? &tl_ny_chunk : NULL,
								  pz ? &tl_pz_chunk : NULL,
								  nz ? &tl_nz_chunk : NULL,
								  job->pos.x * RFT_CHUNK_SIZE,
								  job->pos.y * RFT_CHUNK_SIZE,
								  job->pos.z * RFT_CHUNK_SIZE,
								  tl_faces);

	if (nz_locked)
	{
		RFT_RWLOCK_UNLOCK_RD(&streamer->chunks[nz_idx].storage_lock);
	}

	if (pz_locked)
	{
		RFT_RWLOCK_UNLOCK_RD(&streamer->chunks[pz_idx].storage_lock);
	}

	if (ny_locked)
	{
		RFT_RWLOCK_UNLOCK_RD(&streamer->chunks[ny_idx].storage_lock);
	}

	if (py_locked)
	{
		RFT_RWLOCK_UNLOCK_RD(&streamer->chunks[py_idx].storage_lock);
	}

	if (nx_locked)
	{
		RFT_RWLOCK_UNLOCK_RD(&streamer->chunks[nx_idx].storage_lock);
	}

	if (px_locked)
	{
		RFT_RWLOCK_UNLOCK_RD(&streamer->chunks[px_idx].storage_lock);
	}

	RFT_RWLOCK_UNLOCK_RD(&streamer->chunks[idx].storage_lock);

	rft_chunk_face_range old_face_range	 = rft_streamer_load_face_range(&streamer->chunks[idx]);
	uint32_t			 old_face_offset = old_face_range.offset;
	uint32_t			 old_face_count	 = old_face_range.count;

	if (atomic_load_explicit(&streamer->chunks[idx].slot_version, memory_order_acquire) != job->slot_version ||
		atomic_load_explicit(&streamer->chunks[idx].voxel_version, memory_order_acquire) != job->voxel_version ||
		!rft_streamer_pos_equals(streamer->chunks[idx].pos, job->pos))
	{
		free(job);
		return;
	}

	if (count > 0)
	{
		ASSERT_FATAL(count <= UINT32_MAX);
		uint64_t blocked_epoch = 0U;

		switch (rft_streamer_upload_faces(streamer, idx, tl_faces, (uint32_t)count, &blocked_epoch))
		{
			case RFT_STREAMER_UPLOAD_SUCCESS:
				atomic_store_explicit(&streamer->chunks[idx].blocked_face_epoch, 0U, memory_order_release);

				if (old_face_count > 0U)
				{
					rft_streamer_free_faces(streamer, old_face_offset, old_face_count);
				}

				atomic_store_explicit(&streamer->chunks[idx].state, CHUNK_STATE_ACTIVE, memory_order_release);
				break;

			case RFT_STREAMER_UPLOAD_RETRY_AFTER_RESIZE:
				atomic_store_explicit(&streamer->chunks[idx].blocked_face_epoch, blocked_epoch, memory_order_release);

				if (old_face_count > 0U)
				{
					atomic_store_explicit(&streamer->chunks[idx].state, CHUNK_STATE_ACTIVE, memory_order_release);
					atomic_store_explicit(&streamer->chunks[idx].remesh_pending, true, memory_order_release);
				}

				else
				{
					atomic_store_explicit(&streamer->chunks[idx].state, CHUNK_STATE_VOXELS_READY, memory_order_release);
				}

				break;

			case RFT_STREAMER_UPLOAD_EXHAUSTED:
				atomic_store_explicit(&streamer->chunks[idx].blocked_face_epoch, blocked_epoch, memory_order_release);

				if (old_face_count > 0U)
				{
					atomic_store_explicit(&streamer->chunks[idx].state, CHUNK_STATE_ACTIVE, memory_order_release);
					atomic_store_explicit(&streamer->chunks[idx].remesh_pending, true, memory_order_release);
				}

				else
				{
					atomic_store_explicit(&streamer->chunks[idx].state, CHUNK_STATE_VOXELS_READY, memory_order_release);
				}

				break;

			default:
				UNREACHABLE;
		}
	}

	else
	{
		atomic_store_explicit(&streamer->chunks[idx].blocked_face_epoch, 0U, memory_order_release);

		if (old_face_count > 0U)
		{
			rft_streamer_free_faces(streamer, old_face_offset, old_face_count);
		}

		rft_streamer_store_face_range(&streamer->chunks[idx], 0U, 0U);
		atomic_store_explicit(&streamer->chunks[idx].state, CHUNK_STATE_ACTIVE, memory_order_release);
	}

	free(job);
}
