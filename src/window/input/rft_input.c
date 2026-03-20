#include "window/input/rft_input.h"

#include "utils/rft_assert.h"
#include "utils/rft_math_utils.h"

#include <GLFW/glfw3.h>

#include <math.h>
#include <string.h>

static void	 assert_key_range(int key);
static void	 assert_mouse_range(int button);
static float apply_trigger_deadzone(float v, float dz);
static vec2s apply_radial_deadzone(vec2s v, float dz);

void rft_input_init(rft_input* input)
{
	ASSERT_FATAL(input);

	memset(input, 0, sizeof(*input));

	for (int i = 0; i < RFT_MAX_GAMEPADS; ++i)
	{
		input->gamepads[i].stick_deadzone	= 0.15f;
		input->gamepads[i].trigger_deadzone = 0.15f;
	}

	input->rebind.active				= false;
	input->rebind.waiting_for_release	= false;
	input->rebind.allow_keyboard		= true;
	input->rebind.allow_mouse			= true;
	input->rebind.allow_gamepad_buttons = true;
	input->rebind.allow_gamepad_axes	= true;
	input->rebind.gamepad_index			= 0;
	input->rebind.axis_threshold		= 0.55f;
}

void rft_input_begin_frame(rft_input* input)
{
	ASSERT_FATAL(input);

	memcpy(input->prev_keys, input->keys, sizeof(input->keys));
	memcpy(input->prev_mouse_buttons, input->mouse_buttons, sizeof(input->mouse_buttons));

	input->mouse_delta = (vec2s) { { 0.0f, 0.0f } };

	for (int p = 0; p < RFT_MAX_GAMEPADS; ++p)
	{
		rft_gamepad* pad = &input->gamepads[p];
		memcpy(pad->prev_axes, pad->axes, sizeof(pad->axes));
		memcpy(pad->prev_buttons, pad->buttons, sizeof(pad->buttons));
	}
}

void rft_input_key_callback(rft_input* input, int key, int action)
{
	ASSERT_FATAL(input);
	assert_key_range(key);

	input->keys[key] = (action != 0);
}

void rft_input_mouse_button_callback(rft_input* input, int button, int action)
{
	ASSERT_FATAL(input);
	assert_mouse_range(button);

	input->mouse_buttons[button] = (action != 0);
}

void rft_input_mouse_pos(rft_input* input, double x, double y)
{
	ASSERT_FATAL(input);

	vec2s current = (vec2s) { { (float)x, (float)y } };

	if (!input->mouse_initialized)
	{
		input->mouse_last		 = current;
		input->mouse_initialized = true;
		return;
	}

	vec2s delta = glms_vec2_sub(current, input->mouse_last);

	input->mouse_delta = glms_vec2_add(input->mouse_delta, delta);
	input->mouse_last  = current;
}

void rft_input_update_gamepads(rft_input* input)
{
	ASSERT_FATAL(input);

	for (int i = 0; i < RFT_MAX_GAMEPADS; ++i)
	{
		const int jid = GLFW_JOYSTICK_1 + i;

		rft_gamepad* pad = &input->gamepads[i];

		if (!glfwJoystickPresent(jid))
		{
			pad->connected = false;

			memset(pad->axes, 0, sizeof(pad->axes));
			memset(pad->buttons, 0, sizeof(pad->buttons));

			continue;
		}

		pad->connected = true;
		GLFWgamepadstate state;

		if (glfwGetGamepadState(jid, &state))
		{
			float axes[8] = { 0 };

			for (int a = 0; a <= GLFW_GAMEPAD_AXIS_LAST; ++a)
			{
				axes[a] = state.axes[a];
			}

			vec2s left	= { { axes[GLFW_GAMEPAD_AXIS_LEFT_X], axes[GLFW_GAMEPAD_AXIS_LEFT_Y] } };
			vec2s right = { { axes[GLFW_GAMEPAD_AXIS_RIGHT_X], axes[GLFW_GAMEPAD_AXIS_RIGHT_Y] } };

			left  = apply_radial_deadzone(left, pad->stick_deadzone);
			right = apply_radial_deadzone(right, pad->stick_deadzone);

			axes[GLFW_GAMEPAD_AXIS_LEFT_X]		  = left.x;
			axes[GLFW_GAMEPAD_AXIS_LEFT_Y]		  = left.y;
			axes[GLFW_GAMEPAD_AXIS_RIGHT_X]		  = right.x;
			axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]		  = right.y;
			axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]  = apply_trigger_deadzone(axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER], pad->trigger_deadzone);
			axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] = apply_trigger_deadzone(axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER], pad->trigger_deadzone);

			memcpy(pad->axes, axes, sizeof(pad->axes));

			for (int b = 0; b <= GLFW_GAMEPAD_BUTTON_LAST; ++b)
			{
				pad->buttons[b] = state.buttons[b];
			}
		}
	}
}

bool rft_input_down(const rft_input* input, rft_input_type type, uint32_t code)
{
	ASSERT_FATAL(input);

	switch (type)
	{
		case RFT_INPUT_KEY:
			return input->keys[code];

		case RFT_INPUT_MOUSE_BUTTON:
			return input->mouse_buttons[code];

		case RFT_INPUT_GAMEPAD_BUTTON:
		{
			const rft_gamepad* pad = &input->gamepads[0];
			return pad->connected && pad->buttons[code];
		}

		case RFT_INPUT_GAMEPAD_AXIS:
		{
			const rft_gamepad* pad = &input->gamepads[0];
			return pad->connected && fabsf(pad->axes[code]) > 0.01f;
		}

		default:
			return false;
	}
}

bool rft_input_pressed(const rft_input* input, rft_input_type type, uint32_t code)
{
	ASSERT_FATAL(input);

	switch (type)
	{
		case RFT_INPUT_KEY:
			return input->keys[code] && !input->prev_keys[code];

		case RFT_INPUT_MOUSE_BUTTON:
			return input->mouse_buttons[code] && !input->prev_mouse_buttons[code];

		case RFT_INPUT_GAMEPAD_BUTTON:
		{
			const rft_gamepad* pad = &input->gamepads[0];
			return pad->connected && pad->buttons[code] && !pad->prev_buttons[code];
		}

		case RFT_INPUT_GAMEPAD_AXIS:
		{
			const rft_gamepad* pad = &input->gamepads[0];
			return pad->connected && fabsf(pad->axes[code]) > 0.01f && fabsf(pad->prev_axes[code]) <= 0.01f;
		}

		default:
			return false;
	}
}

bool rft_input_released(const rft_input* input, rft_input_type type, uint32_t code)
{
	ASSERT_FATAL(input);

	switch (type)
	{
		case RFT_INPUT_KEY:
			return !input->keys[code] && input->prev_keys[code];

		case RFT_INPUT_MOUSE_BUTTON:
			return !input->mouse_buttons[code] && input->prev_mouse_buttons[code];

		case RFT_INPUT_GAMEPAD_BUTTON:
		{
			const rft_gamepad* pad = &input->gamepads[0];
			return pad->connected && !pad->buttons[code] && pad->prev_buttons[code];
		}

		case RFT_INPUT_GAMEPAD_AXIS:
		{
			const rft_gamepad* pad = &input->gamepads[0];
			return pad->connected && fabsf(pad->axes[code]) <= 0.01f && fabsf(pad->prev_axes[code]) > 0.01f;
		}

		default:
			return false;
	}
}

vec2s rft_mouse_delta(const rft_input* input)
{
	ASSERT_FATAL(input);
	return input->mouse_delta;
}

static void assert_key_range(int key)
{
	ASSERT_FATAL(key >= 0);
	ASSERT_FATAL(key < RFT_MAX_KEYS);
}

static void assert_mouse_range(int button)
{
	ASSERT_FATAL(button >= 0);
	ASSERT_FATAL(button < RFT_MAX_MOUSE_BUTTONS);
}

static float apply_trigger_deadzone(float v, float dz)
{
	if (v < dz)
	{
		return 0.0f;
	}

	float denom = 1.0f - dz;

	if (denom <= 0.0f)
	{
		return 0.0f;
	}

	float out = (v - dz) / denom;

	return rft_clampf(out, 0.0f, 1.0f);
}

static vec2s apply_radial_deadzone(vec2s v, float dz)
{
	float len = glms_vec2_norm(v);

	if (len <= dz)
	{
		return (vec2s) { { 0.0f, 0.0f } };
	}

	float scaled = (len - dz) / (1.0f - dz);
	return glms_vec2_scale(glms_vec2_normalize(v), scaled);
}
