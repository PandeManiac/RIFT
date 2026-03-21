#include "world/rft_streamer.h"

#include "render/rft_buffer.h"
#include "utils/rft_assert.h"
#include "utils/rft_gen_utils.h"
#include "utils/rft_platform_threads.h"
#include "utils/rft_thread_pool.h"
#include "world/rft_chunk.h"
#include "world/rft_svo.h"
#include "world/rft_terrain.h"

#include <cglm/box.h>
#include <cglm/frustum.h>
#include <cglm/struct/cam.h>
#include <glad/gl.h>
#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CHUNK_FACES (RFT_CHUNK_SIZE * RFT_CHUNK_SIZE * RFT_CHUNK_SIZE * 3 / 2)
#define RFT_STREAMER_BUFFERED_FRAMES 3U
#define RFT_STREAMER_FAR_SVO_LEAF_SIZE 4U
#define RFT_STREAMER_FACE_BUDGET_PER_CHUNK 4096ULL
#define RFT_STREAMER_HEIGHT_GUARD_BLOCKS 16U
#define RFT_STREAMER_HEIGHT_SAMPLES_PER_AXIS 3U
#define RFT_STREAMER_FRUSTUM_CULL_PADDING 2.0f
#define RFT_STREAMER_MIN_ACTIVE_TASK_BUDGET 32
#define RFT_STREAMER_TASKS_PER_THREAD 4
#define RFT_STREAMER_MIN_FACE_BUFFER_SIZE MB(16)
#define RFT_STREAMER_MAX_FACE_BUFFER_SIZE GB(1)

typedef struct DrawArraysIndirectCommand
{
	uint32_t count;
	uint32_t instanceCount;
	uint32_t first;
	uint32_t baseInstance;
} DrawArraysIndirectCommand;

typedef enum rft_chunk_state
{
	CHUNK_STATE_EMPTY,
	CHUNK_STATE_GENERATING_VOXELS,
	CHUNK_STATE_VOXELS_READY,
	CHUNK_STATE_MESHING,
	CHUNK_STATE_ACTIVE
} rft_chunk_state;

typedef struct rft_chunk_metadata
{
	ivec3s					pos;
	_Atomic uint64_t		face_range;
	size_t					memory_bytes;
	pthread_rwlock_t		storage_lock;
	_Atomic rft_chunk_state state;
	_Atomic bool			remesh_pending;
	_Atomic uint32_t		slot_version;
	_Atomic uint32_t		voxel_version;
	_Atomic uint64_t		blocked_face_epoch;
} rft_chunk_metadata;

typedef struct rft_chunk_gpu_metadata
{
	ivec4s	 pos_and_offset;
	uint32_t face_count;
	uint32_t padding[3];
} rft_chunk_gpu_metadata;

typedef struct rft_streamer_grid_pos
{
	int x;
	int z;
} rft_streamer_grid_pos;

typedef struct rft_streamer_column_height_cache_entry
{
	int	 x;
	int	 z;
	int	 top_chunk_y;
	bool occupied;
} rft_streamer_column_height_cache_entry;

typedef struct rft_face_range
{
	uint32_t offset;
	uint32_t count;
} rft_face_range;

typedef struct rft_chunk_face_range
{
	uint32_t offset;
	uint32_t count;
} rft_chunk_face_range;

typedef struct rft_deferred_face_free
{
	uint32_t offset;
	uint32_t count;
	uint64_t retire_after_submit_count;
} rft_deferred_face_free;

typedef enum rft_streamer_upload_result
{
	RFT_STREAMER_UPLOAD_SUCCESS,
	RFT_STREAMER_UPLOAD_RETRY_AFTER_RESIZE,
	RFT_STREAMER_UPLOAD_EXHAUSTED
} rft_streamer_upload_result;

typedef struct rft_job
{
	rft_streamer* streamer;
	ivec3s		  pos;
	uint32_t	  chunk_idx;
	uint8_t		  leaf_size;
	uint8_t		  padding[3];
	uint32_t	  slot_version;
	uint32_t	  voxel_version;
} rft_job;

struct rft_streamer
{
	rft_streamer_config cfg;
	rft_thread_pool*	pool;

	rft_buffer		 face_ssbo;
	rft_buffer		 metadata_ssbos[RFT_STREAMER_BUFFERED_FRAMES];
	rft_buffer		 indirect_buffers[RFT_STREAMER_BUFFERED_FRAMES];
	GLsync			 frame_fences[RFT_STREAMER_BUFFERED_FRAMES];
	uint64_t		 frame_fence_submit_counts[RFT_STREAMER_BUFFERED_FRAMES];
	uint64_t		 render_frame;
	_Atomic uint64_t submitted_frame_count;
	_Atomic uint64_t completed_frame_count;
	float			 effective_render_distance;

	rft_svo_chunk*		svo_data;
	rft_chunk_metadata* chunks;
	uint32_t			chunk_count;

	pthread_mutex_t			face_alloc_mutex;
	rft_face_range*			face_free_ranges;
	uint32_t				face_free_range_count;
	uint32_t				face_free_range_capacity;
	rft_deferred_face_free* deferred_face_frees;
	uint32_t				deferred_face_free_count;
	uint32_t				deferred_face_free_capacity;
	size_t					face_tail_bytes;
	size_t					face_live_usage;
	size_t					face_peak_usage;
	size_t					face_requested_capacity;
	bool					face_resize_pending;
	_Atomic uint64_t		face_allocator_epoch;

	uint32_t* free_stack;
	uint32_t  free_top;

	uint32_t*		 hash_table;
	uint32_t		 hash_capacity;
	pthread_rwlock_t hash_mutex;

	rft_streamer_column_height_cache_entry* column_height_cache;
	uint32_t								column_height_cache_capacity;

	char*				   bfs_visited;
	rft_streamer_grid_pos* bfs_queue;
	size_t				   bfs_capacity;

	ivec3s last_camera_chunk;
};

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

static const ivec3s neighbor_offsets[] = {
	{ { 1, 0, 0 } }, { { -1, 0, 0 } }, { { 0, 1, 0 } }, { { 0, -1, 0 } }, { { 0, 0, 1 } }, { { 0, 0, -1 } },
};

static inline bool rft_streamer_pos_equals(ivec3s a, ivec3s b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

static inline uint64_t rft_streamer_pack_face_range(uint32_t offset, uint32_t count)
{
	return ((uint64_t)count << 32U) | (uint64_t)offset;
}

static inline rft_chunk_face_range rft_streamer_unpack_face_range(uint64_t packed)
{
	return (rft_chunk_face_range) {
		.offset = (uint32_t)(packed & 0xFFFFFFFFULL),
		.count	= (uint32_t)(packed >> 32U),
	};
}

static inline rft_chunk_face_range rft_streamer_load_face_range(const rft_chunk_metadata* chunk)
{
	ASSERT_FATAL(chunk);
	return rft_streamer_unpack_face_range(atomic_load_explicit(&chunk->face_range, memory_order_acquire));
}

static inline void rft_streamer_store_face_range(rft_chunk_metadata* chunk, uint32_t offset, uint32_t count)
{
	ASSERT_FATAL(chunk);
	atomic_store_explicit(&chunk->face_range, rft_streamer_pack_face_range(offset, count), memory_order_release);
}

static void	  streamer_enqueue_chunk(rft_streamer_grid_pos* queue, char* visited, int* q_tail, int center, int dim, int loop_radius, int x, int z);
static size_t rft_streamer_bfs_capacity(float render_distance);
static float  rft_streamer_effective_render_distance(const rft_streamer* streamer, const rft_camera* camera);
static ALWAYS_INLINE inline uint32_t rft_chunk_hash(ivec3s pos);
static ALWAYS_INLINE inline uint32_t rft_streamer_column_hash(int x, int z);
static int							 rft_streamer_compute_column_top_chunk_y(int chunk_x, int chunk_z);
static void							 rft_streamer_request_chunk_remesh(rft_streamer* streamer, uint32_t idx);
static int							 rft_streamer_estimate_column_top_chunk_y(rft_streamer* streamer, int chunk_x, int chunk_z);
static void							 rft_hash_table_insert(rft_streamer* streamer, ivec3s pos, uint32_t chunk_idx);
static void							 rft_hash_table_remove(rft_streamer* streamer, ivec3s pos);
static bool							 rft_get_chunk_idx(rft_streamer* streamer, ivec3s pos, uint32_t* out_idx);
static void							 rft_streamer_request_neighbor_remeshes(rft_streamer* streamer, ivec3s pos);
static void							 rft_streamer_release_chunk_storage(rft_streamer* streamer, uint32_t idx);
static uint8_t						 rft_streamer_desired_leaf_size(const rft_streamer* streamer, ivec3s camera_chunk, ivec3s chunk_pos);
static bool							 rft_generate_dense_chunk(rft_chunk* chunk, ivec3s pos);
static int							 rft_streamer_compare_face_range_audit_entries(const void* lhs, const void* rhs);
static void							 rft_streamer_validate_face_allocator(rft_streamer* streamer);
static void							 rft_streamer_advance_face_allocator_epoch(rft_streamer* streamer);
static uint64_t						 rft_streamer_face_allocator_epoch(const rft_streamer* streamer);
static size_t						 rft_streamer_face_buffer_size(uint32_t chunk_count);
static void							 rft_streamer_request_face_buffer_resize(rft_streamer* streamer, size_t required_capacity);
static void							 rft_streamer_reserve_deferred_face_free_capacity(rft_streamer* streamer, uint32_t additional);
static void							 rft_streamer_face_allocator_insert_range_locked(rft_streamer* streamer, uint32_t offset, uint32_t count);
static bool							 rft_streamer_face_allocator_reserve_locked(rft_streamer* streamer, uint32_t face_count, uint32_t* out_offset);
static rft_streamer_upload_result	 rft_streamer_upload_faces(rft_streamer*		 streamer,
															   uint32_t				 chunk_idx,
															   const rft_voxel_face* faces,
															   uint32_t				 face_count,
															   uint64_t*			 out_allocator_epoch);
static void							 rft_streamer_flush_deferred_face_frees(rft_streamer* streamer);
static void							 rft_streamer_free_faces(rft_streamer* streamer, uint32_t face_offset, uint32_t face_count);
static void							 rft_streamer_clear_chunk_mesh(rft_streamer* streamer, uint32_t idx);
static void							 rft_streamer_resize_face_buffer(rft_streamer* streamer, size_t new_capacity);
static void							 rft_streamer_wait_for_frame_slot(rft_streamer* streamer, uint32_t buffer_idx);
static void							 rft_streamer_wait_for_all_frame_fences(rft_streamer* streamer);
static const rft_svo_chunk*			 rft_streamer_lock_neighbor_chunk(rft_streamer* streamer, ivec3s expected_pos, uint32_t* out_idx, bool* out_locked);
static bool							 rft_streamer_chunk_waiting_for_face_space(const rft_streamer* streamer, rft_chunk_metadata* chunk);
static void							 rft_streamer_schedule_chunk(rft_streamer* streamer, ivec3s cam_chunk, ivec3s pos);
static void							 rft_generate_voxel_task(void* arg);
static void							 rft_mesh_task(void* arg);

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

static int rft_streamer_estimate_column_top_chunk_y(rft_streamer* streamer, int chunk_x, int chunk_z)
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

static void rft_hash_table_insert(rft_streamer* streamer, ivec3s pos, uint32_t chunk_idx)
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

static void rft_hash_table_remove(rft_streamer* streamer, ivec3s pos)
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

static bool rft_get_chunk_idx(rft_streamer* streamer, ivec3s pos, uint32_t* out_idx)
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

static void rft_streamer_request_neighbor_remeshes(rft_streamer* streamer, ivec3s pos)
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

static void rft_streamer_release_chunk_storage(rft_streamer* streamer, uint32_t idx)
{
	rft_chunk_metadata* chunk = &streamer->chunks[idx];
	RFT_RWLOCK_WRLOCK(&chunk->storage_lock);

	rft_streamer_clear_chunk_mesh(streamer, idx);
	rft_svo_chunk_destroy(&streamer->svo_data[idx]);

	chunk->memory_bytes = 0;

	atomic_store_explicit(&chunk->remesh_pending, false, memory_order_relaxed);
	RFT_RWLOCK_UNLOCK_WR(&chunk->storage_lock);
}

static uint8_t rft_streamer_desired_leaf_size(const rft_streamer* streamer, ivec3s camera_chunk, ivec3s chunk_pos)
{
	ASSERT_FATAL(streamer);

	float dx = (float)(chunk_pos.x - camera_chunk.x);
	float dy = (float)(chunk_pos.y - camera_chunk.y);
	float dz = (float)(chunk_pos.z - camera_chunk.z);
	float d2 = dx * dx + dy * dy + dz * dz;
	float r	 = (float)streamer->cfg.near_chunk_radius;

	return d2 <= r * r ? 1U : RFT_STREAMER_FAR_SVO_LEAF_SIZE;
}

static bool rft_generate_dense_chunk(rft_chunk* chunk, ivec3s pos)
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

static void rft_streamer_validate_face_allocator(rft_streamer* streamer)
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

static uint64_t rft_streamer_face_allocator_epoch(const rft_streamer* streamer)
{
	ASSERT_FATAL(streamer);
	return atomic_load_explicit(&streamer->face_allocator_epoch, memory_order_acquire);
}

static size_t rft_streamer_face_buffer_size(uint32_t chunk_count)
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

static rft_streamer_upload_result rft_streamer_upload_faces(rft_streamer*		  streamer,
															uint32_t			  chunk_idx,
															const rft_voxel_face* faces,
															uint32_t			  face_count,
															uint64_t*			  out_allocator_epoch)
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

static void rft_streamer_flush_deferred_face_frees(rft_streamer* streamer)
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

static void rft_streamer_free_faces(rft_streamer* streamer, uint32_t face_offset, uint32_t face_count)
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

static void rft_streamer_clear_chunk_mesh(rft_streamer* streamer, uint32_t idx)
{
	ASSERT_FATAL(streamer);
	ASSERT_FATAL(idx < streamer->chunk_count);

	rft_chunk_metadata*	 chunk		= &streamer->chunks[idx];
	rft_chunk_face_range face_range = rft_streamer_unpack_face_range(atomic_exchange_explicit(&chunk->face_range, 0U, memory_order_acq_rel));
	rft_streamer_free_faces(streamer, face_range.offset, face_range.count);
}

static void rft_streamer_resize_face_buffer(rft_streamer* streamer, size_t new_capacity)
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

static void rft_streamer_wait_for_all_frame_fences(rft_streamer* streamer)
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

void rft_streamer_render(rft_streamer* streamer, ivec3s camera_chunk, vec3s camera_offset, vec4s frustum[6], GLuint shader_program)
{
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
			ivec3s rel_chunk = {
				{
					chunk->pos.x - camera_chunk.x,
					chunk->pos.y - camera_chunk.y,
					chunk->pos.z - camera_chunk.z,
				},
			};

			vec3 aabb[2] = {
				{
					(float)(rel_chunk.x * RFT_CHUNK_SIZE) - camera_offset.x - RFT_STREAMER_FRUSTUM_CULL_PADDING,
					(float)(rel_chunk.y * RFT_CHUNK_SIZE) - camera_offset.y - RFT_STREAMER_FRUSTUM_CULL_PADDING,
					(float)(rel_chunk.z * RFT_CHUNK_SIZE) - camera_offset.z - RFT_STREAMER_FRUSTUM_CULL_PADDING,
				},
				{
					(float)((rel_chunk.x + 1) * RFT_CHUNK_SIZE) - camera_offset.x + RFT_STREAMER_FRUSTUM_CULL_PADDING,
					(float)((rel_chunk.y + 1) * RFT_CHUNK_SIZE) - camera_offset.y + RFT_STREAMER_FRUSTUM_CULL_PADDING,
					(float)((rel_chunk.z + 1) * RFT_CHUNK_SIZE) - camera_offset.z + RFT_STREAMER_FRUSTUM_CULL_PADDING,
				},
			};

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

static void rft_streamer_schedule_chunk(rft_streamer* streamer, ivec3s cam_chunk, ivec3s pos)
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

void rft_streamer_update(rft_streamer* streamer, const rft_camera* camera, ivec3s world_origin_chunk)
{
	float cam_xf = floorf(camera->cfg.pos.x / 64.0f);
	float cam_yf = floorf(camera->cfg.pos.y / 64.0f);
	float cam_zf = floorf(camera->cfg.pos.z / 64.0f);

	ivec3s cam_chunk = { { world_origin_chunk.x + (int)cam_xf, world_origin_chunk.y + (int)cam_yf, world_origin_chunk.z + (int)cam_zf } };

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
