#pragma once

#include "window/input/rft_input.h"

#include <cglm/struct/vec2.h>
#include <stdbool.h>

typedef struct GLFWwindow GLFWwindow;

typedef struct rft_window_config
{
	const char* title;
	ivec2s		size;
	bool		fullscreen;
	bool		vsync;

	int	 gl_major;
	int	 gl_minor;
	bool core_profile;
	int	 samples;
} rft_window_config;

typedef struct
{
	rft_window_config cfg;

	ivec2s		pos;
	GLFWwindow* handle;
	rft_input	input;
} rft_window;

void rft_window_init(rft_window* window, const rft_window_config* config);
void rft_window_destroy(rft_window* window);

void rft_window_begin_frame(rft_window* window);

bool rft_window_should_close(const rft_window* window);
void rft_window_swap_buffers(rft_window* window);
void rft_window_poll_events(void);
