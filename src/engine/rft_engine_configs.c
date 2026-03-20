#include "engine/rft_engine_configs.h"

const rft_window_config rft_default_window_cfg = {
	.title		  = "RFT WINDOW",
	.size		  = { { 1920, 1080 } },
	.fullscreen	  = true,
	.vsync		  = false,
	.gl_major	  = 4,
	.gl_minor	  = 6,
	.core_profile = true,
	.samples	  = 4,
};

const rft_camera_config rft_default_camera_cfg = {
	.pos   = { { 32.0f, 100.0f, 32.0f } },
	.pitch = -90.0f,
	.yaw   = -90.0f,
	.speed = 100.0f,
	.sens  = 0.1f,
	.fov   = 70.0f,
	.near  = 0.1f,
	.far   = 5000.0f,
};

const rft_streamer_config rft_default_streamer_cfg = {
	.render_distance   = 10000.0f,
	.near_chunk_radius = 8,
	.max_chunks		   = 32768,
	.thread_count	   = 4,
};
