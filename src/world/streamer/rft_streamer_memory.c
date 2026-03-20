#include "rft_streamer_internal.h"

typedef enum rft_streamer_face_range_kind
{
	RFT_STREAMER_FACE_RANGE_FREE,
	RFT_STREAMER_FACE_RANGE_DEFERRED
} rft_streamer_face_range_kind;

typedef struct rft_streamer_face_range_audit_entry
{
	uint32_t					 offset;
	uint32_t					 count;
	rft_streamer_face_range_kind kind;
} rft_streamer_face_range_audit_entry;

static int rft_streamer_compare_face_range_audit_entries(const void* lhs, const void* rhs)
{
	const rft_streamer_face_range_audit_entry* a = lhs;
	const rft_streamer_face_range_audit_entry* b = rhs;

	if (a->offset < b->offset)
	{
		return -1;
	}

	if (a->offset > b->offset)
	{
		return 1;
	}

	if (a->count < b->count)
	{
		return -1;
	}

	if (a->count > b->count)
	{
		return 1;
	}

	return (int)a->kind - (int)b->kind;
}

void rft_streamer_validate_face_allocator(rft_streamer* streamer)
{
	ASSERT_FATAL(streamer);

	pthread_mutex_lock(&streamer->face_alloc_mutex);

	size_t								 max_ranges = (size_t)streamer->chunk_count + streamer->face_free_range_count + streamer->deferred_face_free_count;
	rft_streamer_face_range_audit_entry* entries	= NULL;

	if (max_ranges > 0U)
	{
		entries = malloc(max_ranges * sizeof(*entries));
		ASSERT_FATAL(entries);
	}

	size_t	 free_bytes		= 0U;
	size_t	 deferred_bytes = 0U;
	size_t	 entry_count	= 0U;
	uint32_t capacity_faces = (uint32_t)(streamer->face_ssbo.size / sizeof(rft_voxel_face));

	for (uint32_t i = 0; i < streamer->face_free_range_count; ++i)
	{
		const rft_face_range range = streamer->face_free_ranges[i];

		ASSERT_FATAL(range.count > 0U);
		ASSERT_FATAL(range.offset < capacity_faces);
		ASSERT_FATAL(range.count <= capacity_faces - range.offset);

		entries[entry_count++] = (rft_streamer_face_range_audit_entry) {
			.offset = range.offset,
			.count	= range.count,
			.kind	= RFT_STREAMER_FACE_RANGE_FREE,
		};

		free_bytes += (size_t)range.count * sizeof(rft_voxel_face);
	}

	for (uint32_t i = 0; i < streamer->deferred_face_free_count; ++i)
	{
		const rft_deferred_face_free range = streamer->deferred_face_frees[i];

		ASSERT_FATAL(range.count > 0U);
		ASSERT_FATAL(range.offset < capacity_faces);
		ASSERT_FATAL(range.count <= capacity_faces - range.offset);

		entries[entry_count++] = (rft_streamer_face_range_audit_entry) {
			.offset = range.offset,
			.count	= range.count,
			.kind	= RFT_STREAMER_FACE_RANGE_DEFERRED,
		};

		deferred_bytes += (size_t)range.count * sizeof(rft_voxel_face);
	}

	ASSERT_FATAL(streamer->face_live_usage <= streamer->face_tail_bytes);
	ASSERT_FATAL(free_bytes <= streamer->face_tail_bytes);
	ASSERT_FATAL(deferred_bytes <= streamer->face_tail_bytes);
	ASSERT_FATAL(free_bytes + deferred_bytes <= streamer->face_tail_bytes);

	if (entry_count > 1U)
	{
		qsort(entries, entry_count, sizeof(*entries), rft_streamer_compare_face_range_audit_entries);

		for (size_t i = 1U; i < entry_count; ++i)
		{
			const rft_streamer_face_range_audit_entry prev = entries[i - 1U];
			const rft_streamer_face_range_audit_entry curr = entries[i];

			uint64_t prev_end	= (uint64_t)prev.offset + prev.count;
			uint64_t curr_begin = curr.offset;

			ASSERT_FATAL(prev_end <= curr_begin);
		}
	}

	free(entries);
	pthread_mutex_unlock(&streamer->face_alloc_mutex);
}

static void rft_streamer_advance_face_allocator_epoch(rft_streamer* streamer)
{
	ASSERT_FATAL(streamer);
	atomic_fetch_add_explicit(&streamer->face_allocator_epoch, 1U, memory_order_acq_rel);
}

uint64_t rft_streamer_face_allocator_epoch(const rft_streamer* streamer)
{
	ASSERT_FATAL(streamer);
	return atomic_load_explicit(&streamer->face_allocator_epoch, memory_order_acquire);
}

size_t rft_streamer_face_buffer_size(uint32_t chunk_count)
{
	uint32_t initial_chunk_budget = chunk_count;

	if (initial_chunk_budget > 1024U)
	{
		initial_chunk_budget = 1024U;
	}

	size_t size = (size_t)initial_chunk_budget * RFT_STREAMER_FACE_BUDGET_PER_CHUNK * sizeof(rft_voxel_face);

	if (size < RFT_STREAMER_MIN_FACE_BUFFER_SIZE)
	{
		size = RFT_STREAMER_MIN_FACE_BUFFER_SIZE;
	}

	if (size > RFT_STREAMER_MAX_FACE_BUFFER_SIZE)
	{
		size = RFT_STREAMER_MAX_FACE_BUFFER_SIZE;
	}
	return size;
}

static void rft_streamer_request_face_buffer_resize(rft_streamer* streamer, size_t required_capacity)
{
	if (required_capacity > RFT_STREAMER_MAX_FACE_BUFFER_SIZE)
	{
		required_capacity = RFT_STREAMER_MAX_FACE_BUFFER_SIZE;
	}

	if (required_capacity <= streamer->face_ssbo.size)
	{
		return;
	}

	if (required_capacity > streamer->face_requested_capacity)
	{
		streamer->face_requested_capacity = required_capacity;
	}

	streamer->face_resize_pending = true;
}

static void rft_streamer_reserve_deferred_face_free_capacity(rft_streamer* streamer, uint32_t additional)
{
	ASSERT_FATAL(streamer);

	uint32_t needed = streamer->deferred_face_free_count + additional;

	if (needed <= streamer->deferred_face_free_capacity)
	{
		return;
	}

	uint32_t new_capacity = streamer->deferred_face_free_capacity ? streamer->deferred_face_free_capacity : 64U;

	while (new_capacity < needed)
	{
		new_capacity *= 2U;
	}

	rft_deferred_face_free* entries = realloc(streamer->deferred_face_frees, (size_t)new_capacity * sizeof(rft_deferred_face_free));
	ASSERT_FATAL(entries);
	streamer->deferred_face_frees		  = entries;
	streamer->deferred_face_free_capacity = new_capacity;
}

static void rft_streamer_face_allocator_insert_range_locked(rft_streamer* streamer, uint32_t offset, uint32_t count)
{
	ASSERT_FATAL(streamer);
	ASSERT_FATAL(count > 0U);
	ASSERT_FATAL(streamer->face_free_range_count < streamer->face_free_range_capacity);

	uint32_t insert_idx = streamer->face_free_range_count;

	while (insert_idx > 0U && streamer->face_free_ranges[insert_idx - 1U].offset > offset)
	{
		streamer->face_free_ranges[insert_idx] = streamer->face_free_ranges[insert_idx - 1U];
		insert_idx--;
	}

	streamer->face_free_ranges[insert_idx] = (rft_face_range) { offset, count };
	streamer->face_free_range_count++;

	if (insert_idx > 0U)
	{
		rft_face_range* prev = &streamer->face_free_ranges[insert_idx - 1U];
		rft_face_range* curr = &streamer->face_free_ranges[insert_idx];

		if (prev->offset + prev->count == curr->offset)
		{
			prev->count += curr->count;
			memmove(curr, curr + 1, (size_t)(streamer->face_free_range_count - insert_idx - 1U) * sizeof(rft_face_range));
			streamer->face_free_range_count--;
			insert_idx--;
		}
	}

	if (insert_idx + 1U < streamer->face_free_range_count)
	{
		rft_face_range* curr = &streamer->face_free_ranges[insert_idx];
		rft_face_range* next = &streamer->face_free_ranges[insert_idx + 1U];

		if (curr->offset + curr->count == next->offset)
		{
			curr->count += next->count;
			memmove(next, next + 1, (size_t)(streamer->face_free_range_count - insert_idx - 2U) * sizeof(rft_face_range));
			streamer->face_free_range_count--;
		}
	}

	while (streamer->face_free_range_count > 0U)
	{
		rft_face_range* tail_range = &streamer->face_free_ranges[streamer->face_free_range_count - 1U];
		size_t			end_bytes  = ((size_t)tail_range->offset + tail_range->count) * sizeof(rft_voxel_face);

		if (end_bytes != streamer->face_tail_bytes)
		{
			break;
		}

		streamer->face_tail_bytes = (size_t)tail_range->offset * sizeof(rft_voxel_face);
		streamer->face_free_range_count--;
	}
}

static bool rft_streamer_face_allocator_reserve_locked(rft_streamer* streamer, uint32_t face_count, uint32_t* out_offset)
{
	ASSERT_FATAL(streamer);
	ASSERT_FATAL(face_count > 0U);
	ASSERT_FATAL(out_offset);

	uint32_t best_idx	= UINT32_MAX;
	uint32_t best_count = UINT32_MAX;

	for (uint32_t i = 0; i < streamer->face_free_range_count; ++i)
	{
		const rft_face_range range = streamer->face_free_ranges[i];

		if (range.count >= face_count && range.count < best_count)
		{
			best_idx   = i;
			best_count = range.count;
		}
	}

	if (best_idx != UINT32_MAX)
	{
		rft_face_range* range = &streamer->face_free_ranges[best_idx];
		*out_offset			  = range->offset;

		range->offset += face_count;
		range->count -= face_count;

		if (range->count == 0U)
		{
			memmove(range, range + 1, (size_t)(streamer->face_free_range_count - best_idx - 1U) * sizeof(rft_face_range));
			streamer->face_free_range_count--;
		}

		return true;
	}

	size_t size_bytes = (size_t)face_count * sizeof(rft_voxel_face);

	if (streamer->face_tail_bytes + size_bytes > streamer->face_ssbo.size)
	{
		size_t required = streamer->face_ssbo.size * 2U;

		if (required < streamer->face_tail_bytes + size_bytes)
		{
			required = streamer->face_tail_bytes + size_bytes;
		}

		rft_streamer_request_face_buffer_resize(streamer, required);
		return false;
	}

	*out_offset = (uint32_t)(streamer->face_tail_bytes / sizeof(rft_voxel_face));
	streamer->face_tail_bytes += size_bytes;
	return true;
}

rft_streamer_upload_result rft_streamer_upload_faces(rft_streamer*		   streamer,
													 uint32_t			   chunk_idx,
													 const rft_voxel_face* faces,
													 uint32_t			   face_count,
													 uint64_t*			   out_allocator_epoch)
{
	ASSERT_FATAL(streamer);
	ASSERT_FATAL(faces);
	ASSERT_FATAL(face_count > 0U);

	pthread_mutex_lock(&streamer->face_alloc_mutex);

	uint32_t				   offset = 0;
	bool					   ok	  = rft_streamer_face_allocator_reserve_locked(streamer, face_count, &offset);
	rft_streamer_upload_result result = RFT_STREAMER_UPLOAD_EXHAUSTED;

	if (ok)
	{
		rft_voxel_face* dst = (rft_voxel_face*)streamer->face_ssbo.mapped + offset;
		memcpy(dst, faces, (size_t)face_count * sizeof(rft_voxel_face));

		rft_streamer_store_face_range(&streamer->chunks[chunk_idx], offset, face_count);

		streamer->face_live_usage += (size_t)face_count * sizeof(rft_voxel_face);

		if (streamer->face_live_usage > streamer->face_peak_usage)
		{
			streamer->face_peak_usage = streamer->face_live_usage;
		}

		result = RFT_STREAMER_UPLOAD_SUCCESS;
	}

	else if (streamer->face_resize_pending)
	{
		result = RFT_STREAMER_UPLOAD_RETRY_AFTER_RESIZE;
	}

	if (out_allocator_epoch)
	{
		*out_allocator_epoch = rft_streamer_face_allocator_epoch(streamer);
	}

	pthread_mutex_unlock(&streamer->face_alloc_mutex);
	return result;
}

void rft_streamer_flush_deferred_face_frees(rft_streamer* streamer)
{
	ASSERT_FATAL(streamer);
	pthread_mutex_lock(&streamer->face_alloc_mutex);

	bool	 freed_any			   = false;
	uint32_t write_idx			   = 0U;
	uint64_t completed_frame_count = atomic_load_explicit(&streamer->completed_frame_count, memory_order_acquire);

	for (uint32_t i = 0; i < streamer->deferred_face_free_count; ++i)
	{
		rft_deferred_face_free entry = streamer->deferred_face_frees[i];

		if (entry.retire_after_submit_count <= completed_frame_count)
		{
			rft_streamer_face_allocator_insert_range_locked(streamer, entry.offset, entry.count);
			freed_any = true;
			continue;
		}

		streamer->deferred_face_frees[write_idx++] = entry;
	}

	streamer->deferred_face_free_count = write_idx;
	pthread_mutex_unlock(&streamer->face_alloc_mutex);

	if (freed_any)
	{
		rft_streamer_advance_face_allocator_epoch(streamer);
	}

	rft_streamer_validate_face_allocator(streamer);
}

void rft_streamer_free_faces(rft_streamer* streamer, uint32_t face_offset, uint32_t face_count)
{
	ASSERT_FATAL(streamer);

	if (face_count == 0U)
	{
		return;
	}

	pthread_mutex_lock(&streamer->face_alloc_mutex);
	rft_streamer_reserve_deferred_face_free_capacity(streamer, 1U);

	uint64_t retire_after_submit_count									= atomic_load_explicit(&streamer->submitted_frame_count, memory_order_acquire) + 1U;
	streamer->deferred_face_frees[streamer->deferred_face_free_count++] = (rft_deferred_face_free) { face_offset, face_count, retire_after_submit_count };

	size_t freed_bytes = (size_t)face_count * sizeof(rft_voxel_face);
	ASSERT_FATAL(streamer->face_live_usage >= freed_bytes);
	streamer->face_live_usage -= freed_bytes;

	pthread_mutex_unlock(&streamer->face_alloc_mutex);
	rft_streamer_validate_face_allocator(streamer);
}

void rft_streamer_clear_chunk_mesh(rft_streamer* streamer, uint32_t idx)
{
	ASSERT_FATAL(streamer);
	ASSERT_FATAL(idx < streamer->chunk_count);

	rft_chunk_metadata*	 chunk		= &streamer->chunks[idx];
	rft_chunk_face_range face_range = rft_streamer_unpack_face_range(atomic_exchange_explicit(&chunk->face_range, 0U, memory_order_acq_rel));
	rft_streamer_free_faces(streamer, face_range.offset, face_range.count);
}

void rft_streamer_resize_face_buffer(rft_streamer* streamer, size_t new_capacity)
{
	if (new_capacity <= streamer->face_ssbo.size)
	{
		return;
	}

	if (new_capacity > RFT_STREAMER_MAX_FACE_BUFFER_SIZE)
	{
		new_capacity = RFT_STREAMER_MAX_FACE_BUFFER_SIZE;
	}

	rft_buffer new_face_ssbo;
	rft_buffer_init(&new_face_ssbo, RFT_BUFFER_SHADER_STORAGE);
	rft_buffer_set_data_persistent(&new_face_ssbo, new_capacity);

	rft_streamer_wait_for_all_frame_fences(streamer);

	pthread_mutex_lock(&streamer->face_alloc_mutex);

	if (new_capacity > streamer->face_ssbo.size)
	{
		memcpy(new_face_ssbo.mapped, streamer->face_ssbo.mapped, streamer->face_tail_bytes);

		rft_buffer_destroy(&streamer->face_ssbo);

		streamer->face_ssbo				  = new_face_ssbo;
		streamer->face_requested_capacity = 0U;
		streamer->face_resize_pending	  = false;

		pthread_mutex_unlock(&streamer->face_alloc_mutex);

		rft_streamer_advance_face_allocator_epoch(streamer);
		rft_streamer_validate_face_allocator(streamer);

		return;
	}

	pthread_mutex_unlock(&streamer->face_alloc_mutex);
	rft_buffer_destroy(&new_face_ssbo);
}
