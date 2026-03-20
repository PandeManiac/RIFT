#pragma once

#include <stdint.h>

typedef struct rft_streamer_stats rft_streamer_stats;
typedef struct rft_debug_overlay  rft_debug_overlay;

rft_debug_overlay* rft_debug_overlay_create(void);
void			   rft_debug_overlay_destroy(rft_debug_overlay* overlay);

void rft_debug_overlay_update(rft_debug_overlay*		overlay,
							  float						dt,
							  float						gpu_ms,
							  int						gpu_valid,
							  const rft_streamer_stats* stats,
							  float						camera_speed,
							  float						default_camera_speed);

void rft_debug_overlay_render(rft_debug_overlay* overlay);
