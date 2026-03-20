#pragma once

#include "render/rft_camera.h"

#include <cglm/cglm.h>
#include <glad/gl.h>
#include <stdint.h>

typedef struct rft_streamer rft_streamer;

typedef struct rft_streamer_stats
{
	uint32_t total_chunks;
	uint32_t empty_chunks;
	uint32_t generating_chunks;
	uint32_t ready_chunks;
	uint32_t meshing_chunks;
	uint32_t active_chunks;
	uint32_t free_chunks;
	uint32_t loaded_chunks;
	uint32_t dense_chunks;
	uint32_t svo_chunks;
	uint32_t active_jobs;
	uint32_t max_active_jobs;
	float	 streaming_pressure;
	float	 loaded_radius;
	float	 target_radius;
	uint32_t chunk_memory_mb;
	uint32_t dense_memory_mb;
	uint32_t svo_memory_mb;
	uint32_t face_arena_usage_mb;
	uint32_t face_arena_peak_mb;
	uint32_t face_arena_capacity_mb;
} rft_streamer_stats;

typedef struct rft_streamer_config
{
	float	 render_distance;
	uint32_t near_chunk_radius;
	uint32_t max_chunks;
	uint32_t thread_count;
} rft_streamer_config;

rft_streamer* rft_streamer_create(const rft_streamer_config* cfg);
void		  rft_streamer_destroy(rft_streamer* streamer);

void rft_streamer_collect_stats(rft_streamer* streamer, rft_streamer_stats* stats);

void rft_streamer_update(rft_streamer* streamer, const rft_camera* camera);
void rft_streamer_render(rft_streamer* streamer, const rft_camera* camera, vec4s frustum[6], GLuint shader_program);
