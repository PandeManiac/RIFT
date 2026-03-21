#pragma once

#include "ui/rft_debug_overlay.h"

#include "render/rft_buffer.h"
#include "render/rft_vertex_array.h"
#include "render/shader/rft_shader.h"
#include "world/rft_streamer.h"

#include <glad/gl.h>

#define RFT_DEBUG_OVERLAY_FRAME_HISTORY_SIZE 120
#define RFT_DEBUG_OVERLAY_MAX_CHARS 1024
#define RFT_DEBUG_OVERLAY_BUFFERED_FRAMES 3

#define RFT_DEBUG_FONT_SCALE 2.0f
#define RFT_DEBUG_CHAR_CELL_WIDTH (6.0f * RFT_DEBUG_FONT_SCALE)
#define RFT_DEBUG_LINE_HEIGHT 20.0f

typedef struct rft_font_vertex
{
	float	 x, y;
	float	 u, v;
	uint32_t color;
} rft_font_vertex;

struct rft_debug_overlay
{
	rft_buffer		 vbo;
	rft_vertex_array vao;
	rft_shader		 shader;
	GLuint			 font_texture;

	float frame_times[RFT_DEBUG_OVERLAY_FRAME_HISTORY_SIZE];
	int	  frame_idx;
	int	  frame_count;

	float avg_fps;
	float p99_frame_ms;
	float cpu_usage;
	float ram_usage_gb;
	float vram_usage_mb;
	bool  vram_usage_valid;

	float			   frame_time_ms;
	float			   gpu_time_ms;
	bool			   gpu_time_valid;
	float			   camera_speed;
	float			   default_camera_speed;
	float			   time_of_day;
	float			   time_speed;
	bool			   time_locked;
	rft_streamer_stats streamer_stats;

	unsigned long long prev_total_ticks;
	unsigned long long prev_idle_ticks;

	float	 timer;
	uint32_t render_frame;
};

GLuint rft_debug_overlay_create_font_texture(void);
void   rft_debug_overlay_update_system_stats(rft_debug_overlay* overlay);
