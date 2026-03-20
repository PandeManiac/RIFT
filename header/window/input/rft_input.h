#pragma once

#include <cglm/struct/vec2.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFT_MAX_KEYS 512
#define RFT_MAX_MOUSE_BUTTONS 16

#define RFT_MAX_GAMEPADS 4
#define RFT_MAX_ACTIONS 64
#define RFT_MAX_BINDINGS 256
#define RFT_MAX_MAPS 8

typedef enum rft_input_type
{
	RFT_INPUT_KEY,
	RFT_INPUT_MOUSE_BUTTON,
	RFT_INPUT_GAMEPAD_BUTTON,
	RFT_INPUT_GAMEPAD_AXIS
} rft_input_type;

typedef struct rft_binding
{
	uint32_t	   action;
	rft_input_type type;
	uint32_t	   code;
	float		   scale;
} rft_binding;

typedef struct rft_gamepad
{
	bool connected;

	float axes[8];
	float prev_axes[8];

	uint8_t buttons[16];
	uint8_t prev_buttons[16];

	float stick_deadzone;
	float trigger_deadzone;
} rft_gamepad;

struct rft_action_map;

typedef struct rft_rebind
{
	bool active;
	bool waiting_for_release;

	struct rft_action_map* map;
	uint32_t			   action;

	bool allow_keyboard;
	bool allow_mouse;
	bool allow_gamepad_buttons;
	bool allow_gamepad_axes;

	uint32_t gamepad_index;
	float	 axis_threshold;

} rft_rebind;

typedef struct rft_input
{
	uint8_t keys[RFT_MAX_KEYS];
	uint8_t prev_keys[RFT_MAX_KEYS];

	uint8_t mouse_buttons[RFT_MAX_MOUSE_BUTTONS];
	uint8_t prev_mouse_buttons[RFT_MAX_MOUSE_BUTTONS];

	vec2s mouse_last;
	vec2s mouse_delta;
	bool  mouse_initialized;

	rft_gamepad gamepads[RFT_MAX_GAMEPADS];

	struct rft_action_map* maps[RFT_MAX_MAPS];
	uint32_t			   map_count;

	rft_rebind rebind;

} rft_input;

void rft_input_init(rft_input* input);
void rft_input_begin_frame(rft_input* input);

void rft_input_key_callback(rft_input* input, int key, int action);
void rft_input_mouse_button_callback(rft_input* input, int button, int action);
void rft_input_mouse_pos(rft_input* input, double x, double y);

void rft_input_update_gamepads(rft_input* input);

bool rft_input_down(const rft_input* input, rft_input_type type, uint32_t code);
bool rft_input_pressed(const rft_input* input, rft_input_type type, uint32_t code);
bool rft_input_released(const rft_input* input, rft_input_type type, uint32_t code);

vec2s rft_mouse_delta(const rft_input* input);

void rft_input_rebind_begin(rft_input* input, struct rft_action_map* map, uint32_t action);
void rft_input_rebind_cancel(rft_input* input);
bool rft_input_rebind_is_active(const rft_input* input);
bool rft_input_rebind_update(rft_input* input);
