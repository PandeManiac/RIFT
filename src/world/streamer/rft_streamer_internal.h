#pragma once

#include "world/rft_streamer.h"
#include "world/rft_svo.h"
#include "world/rft_terrain.h"

#include "render/rft_buffer.h"

#include "utils/rft_assert.h"
#include "utils/rft_gen_utils.h"
#include "utils/rft_platform_threads.h"
#include "utils/rft_thread_pool.h"

#include <cglm/box.h>
#include <cglm/frustum.h>
#include <cglm/struct/cam.h>
#include <glad/gl.h>
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
#define RFT_STREAMER_USE_DIRECT_SVO_MESH 0
#define RFT_STREAMER_ENABLE_FRUSTUM_CULL 1
#define RFT_STREAMER_VALIDATE_FACE_ALLOCATOR 1

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

size_t	 rft_streamer_face_buffer_size(uint32_t chunk_count);
uint64_t rft_streamer_face_allocator_epoch(const rft_streamer* streamer);
void	 rft_streamer_flush_deferred_face_frees(rft_streamer* streamer);
void	 rft_streamer_resize_face_buffer(rft_streamer* streamer, size_t new_capacity);
void	 rft_streamer_wait_for_all_frame_fences(rft_streamer* streamer);

rft_streamer_upload_result rft_streamer_upload_faces(rft_streamer*		   streamer,
													 uint32_t			   chunk_idx,
													 const rft_voxel_face* faces,
													 uint32_t			   face_count,
													 uint64_t*			   out_allocator_epoch);

void rft_streamer_validate_face_allocator(rft_streamer* streamer);
void rft_streamer_free_faces(rft_streamer* streamer, uint32_t face_offset, uint32_t face_count);
void rft_streamer_clear_chunk_mesh(rft_streamer* streamer, uint32_t idx);

bool	rft_get_chunk_idx(rft_streamer* streamer, ivec3s pos, uint32_t* out_idx);
void	rft_hash_table_insert(rft_streamer* streamer, ivec3s pos, uint32_t chunk_idx);
void	rft_hash_table_remove(rft_streamer* streamer, ivec3s pos);
int		rft_streamer_estimate_column_top_chunk_y(rft_streamer* streamer, int chunk_x, int chunk_z);
uint8_t rft_streamer_desired_leaf_size(const rft_streamer* streamer, ivec3s camera_chunk, ivec3s chunk_pos);
bool	rft_generate_dense_chunk(rft_chunk* chunk, ivec3s pos);
void	rft_streamer_schedule_chunk(rft_streamer* streamer, ivec3s cam_chunk, ivec3s pos);
void	rft_streamer_request_neighbor_remeshes(rft_streamer* streamer, ivec3s pos);
void	rft_streamer_release_chunk_storage(rft_streamer* streamer, uint32_t idx);
