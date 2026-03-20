#include "rft_streamer_internal.h"

static void rft_streamer_wait_for_frame_slot(rft_streamer* streamer, uint32_t buffer_idx)
{
	ASSERT_FATAL(streamer);
	ASSERT_FATAL(buffer_idx < RFT_STREAMER_BUFFERED_FRAMES);

	GLsync* fence = &streamer->frame_fences[buffer_idx];

	if (*fence == NULL)
	{
		return;
	}

	while (true)
	{
		GLenum result = glClientWaitSync(*fence, GL_SYNC_FLUSH_COMMANDS_BIT, 100000000ULL);
		ASSERT_FATAL(result != GL_WAIT_FAILED);

		if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED)
		{
			break;
		}
	}

	uint64_t completed_submit_count = streamer->frame_fence_submit_counts[buffer_idx];
	atomic_store_explicit(&streamer->completed_frame_count, completed_submit_count, memory_order_release);

	streamer->frame_fence_submit_counts[buffer_idx] = 0U;

	glDeleteSync(*fence);
	*fence = NULL;
}

void rft_streamer_wait_for_all_frame_fences(rft_streamer* streamer)
{
	ASSERT_FATAL(streamer);

	for (uint32_t i = 0; i < RFT_STREAMER_BUFFERED_FRAMES; ++i)
	{
		rft_streamer_wait_for_frame_slot(streamer, i);
	}
}

void rft_streamer_collect_stats(rft_streamer* streamer, rft_streamer_stats* stats)
{
	ASSERT_FATAL(streamer);
	ASSERT_FATAL(stats);

	memset(stats, 0, sizeof(*stats));

	stats->total_chunks	   = streamer->chunk_count;
	stats->free_chunks	   = streamer->free_top;
	stats->max_active_jobs = streamer->cfg.thread_count * RFT_STREAMER_TASKS_PER_THREAD;

	if (stats->max_active_jobs < RFT_STREAMER_MIN_ACTIVE_TASK_BUDGET)
	{
		stats->max_active_jobs = RFT_STREAMER_MIN_ACTIVE_TASK_BUDGET;
	}

	stats->active_jobs		  = (uint32_t)rft_thread_pool_get_active_count(streamer->pool);
	stats->streaming_pressure = stats->max_active_jobs > 0 ? fminf(1.0f, (float)stats->active_jobs / (float)stats->max_active_jobs) : 0.0f;
	stats->target_radius	  = streamer->effective_render_distance / 64.0f;

	pthread_mutex_lock(&streamer->face_alloc_mutex);

	stats->face_arena_usage_mb	  = (uint32_t)(streamer->face_live_usage >> 20);
	stats->face_arena_peak_mb	  = (uint32_t)(streamer->face_peak_usage >> 20);
	stats->face_arena_capacity_mb = (uint32_t)(streamer->face_ssbo.size >> 20);

	pthread_mutex_unlock(&streamer->face_alloc_mutex);

	float  max_dist_sq	= 0.0f;
	ivec3s cam_chunk	= streamer->last_camera_chunk;
	size_t dense_memory = 0;
	size_t svo_memory	= 0;

	for (uint32_t i = 0; i < streamer->chunk_count; ++i)
	{
		const rft_chunk_metadata* chunk = &streamer->chunks[i];
		rft_chunk_state			  state = atomic_load_explicit(&chunk->state, memory_order_relaxed);

		switch (state)
		{
			case CHUNK_STATE_EMPTY:
				stats->empty_chunks++;
				break;

			case CHUNK_STATE_GENERATING_VOXELS:
				stats->generating_chunks++;
				break;

			case CHUNK_STATE_VOXELS_READY:
				stats->ready_chunks++;
				break;

			case CHUNK_STATE_MESHING:
				stats->meshing_chunks++;
				break;

			case CHUNK_STATE_ACTIVE:
				stats->active_chunks++;
				break;

			default:
				break;
		}

		if (state != CHUNK_STATE_EMPTY)
		{
			stats->loaded_chunks++;
			stats->svo_chunks++;
			svo_memory += chunk->memory_bytes;

			float dx	  = (float)(chunk->pos.x - cam_chunk.x);
			float dz	  = (float)(chunk->pos.z - cam_chunk.z);
			float dist_sq = dx * dx + dz * dz;

			if (dist_sq > max_dist_sq)
			{
				max_dist_sq = dist_sq;
			}
		}
	}

	stats->loaded_radius   = sqrtf(max_dist_sq);
	stats->chunk_memory_mb = (uint32_t)(svo_memory >> 20);
	stats->dense_memory_mb = (uint32_t)(dense_memory >> 20);
	stats->svo_memory_mb   = (uint32_t)(svo_memory >> 20);
}

void rft_streamer_render(rft_streamer* streamer, const rft_camera* camera, vec4s frustum[6], GLuint shader_program)
{
	UNUSED(camera);

	rft_streamer_validate_face_allocator(streamer);
	uint32_t buffer_idx = (uint32_t)(streamer->render_frame % RFT_STREAMER_BUFFERED_FRAMES);

	rft_streamer_wait_for_frame_slot(streamer, buffer_idx);

	rft_buffer* metadata_ssbo	= &streamer->metadata_ssbos[buffer_idx];
	rft_buffer* indirect_buffer = &streamer->indirect_buffers[buffer_idx];

	rft_chunk_gpu_metadata*	   meta_out = (rft_chunk_gpu_metadata*)metadata_ssbo->mapped;
	DrawArraysIndirectCommand* cmd_out	= (DrawArraysIndirectCommand*)indirect_buffer->mapped;

	vec4	 raw_frustum[6];
	uint32_t count = 0;

	for (uint32_t i = 0; i < 6U; ++i)
	{
		raw_frustum[i][0] = frustum[i].raw[0];
		raw_frustum[i][1] = frustum[i].raw[1];
		raw_frustum[i][2] = frustum[i].raw[2];
		raw_frustum[i][3] = frustum[i].raw[3];
	}

	for (uint32_t i = 0; i < streamer->chunk_count; ++i)
	{
		rft_chunk_metadata*	 chunk		= &streamer->chunks[i];
		rft_chunk_state		 state		= atomic_load_explicit(&chunk->state, memory_order_relaxed);
		rft_chunk_face_range face_range = rft_streamer_load_face_range(chunk);
		if ((state == CHUNK_STATE_ACTIVE || state == CHUNK_STATE_MESHING) && face_range.count > 0U)
		{
			vec3 aabb[2] = { { (float)(chunk->pos.x * RFT_CHUNK_SIZE) - RFT_STREAMER_FRUSTUM_CULL_PADDING,
							   (float)(chunk->pos.y * RFT_CHUNK_SIZE) - RFT_STREAMER_FRUSTUM_CULL_PADDING,
							   (float)(chunk->pos.z * RFT_CHUNK_SIZE) - RFT_STREAMER_FRUSTUM_CULL_PADDING },
							 { (float)((chunk->pos.x + 1) * RFT_CHUNK_SIZE) + RFT_STREAMER_FRUSTUM_CULL_PADDING,
							   (float)((chunk->pos.y + 1) * RFT_CHUNK_SIZE) + RFT_STREAMER_FRUSTUM_CULL_PADDING,
							   (float)((chunk->pos.z + 1) * RFT_CHUNK_SIZE) + RFT_STREAMER_FRUSTUM_CULL_PADDING } };

			if (!glm_aabb_frustum(aabb, raw_frustum))
			{
				continue;
			}

			meta_out[count].pos_and_offset.x = chunk->pos.x;
			meta_out[count].pos_and_offset.y = chunk->pos.y;
			meta_out[count].pos_and_offset.z = chunk->pos.z;
			meta_out[count].pos_and_offset.w = (int32_t)face_range.offset;
			meta_out[count].face_count		 = face_range.count;

			cmd_out[count].count		 = face_range.count * 6;
			cmd_out[count].instanceCount = 1;
			cmd_out[count].first		 = 0;
			cmd_out[count].baseInstance	 = count;

			count++;
		}
	}

	glUseProgram(shader_program);
	glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);

	rft_buffer_bind_base(&streamer->face_ssbo, 0);
	rft_buffer_bind_base(metadata_ssbo, 1);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect_buffer->id);

	if (count > 0)
	{
		glMultiDrawArraysIndirect(GL_TRIANGLES, 0, (GLsizei)count, 0);
	}

	ASSERT_FATAL(streamer->frame_fences[buffer_idx] == NULL);

	streamer->frame_fence_submit_counts[buffer_idx] = streamer->render_frame + 1U;
	streamer->frame_fences[buffer_idx]				= glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

	ASSERT_FATAL(streamer->frame_fences[buffer_idx] != NULL);

	streamer->render_frame++;
	atomic_store_explicit(&streamer->submitted_frame_count, streamer->render_frame, memory_order_release);
}
