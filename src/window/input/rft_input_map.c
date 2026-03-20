#include "window/input/rft_input_map.h"

#include "utils/rft_assert.h"

#include <stdlib.h>
#include <string.h>

static float binding_value(const rft_binding* b, const rft_input* input);

rft_action_map* rft_input_create_map(rft_input* input, uint32_t action_count)
{
	ASSERT_FATAL(input);
	ASSERT_FATAL(action_count <= RFT_MAX_ACTIONS);

	rft_action_map* map = malloc(sizeof(rft_action_map));
	ASSERT_FATAL(map);

	memset(map, 0, sizeof(*map));

	map->input		  = input;
	map->action_count = action_count;

	ASSERT_FATAL(input->map_count < RFT_MAX_MAPS);
	input->maps[input->map_count++] = map;

	return map;
}

void rft_input_destroy_map(rft_action_map* map)
{
	ASSERT_FATAL(map);
	ASSERT_FATAL(map->input);

	rft_input* input = map->input;

	for (uint32_t i = 0; i < input->map_count; ++i)
	{
		if (input->maps[i] == map)
		{
			uint32_t last = input->map_count - 1;

			if (i != last)
			{
				input->maps[i] = input->maps[last];
			}

			input->maps[last] = NULL;
			input->map_count--;

			free(map);
			return;
		}
	}

	ASSERT_FATAL(false && "Attempted to destroy unknown input map");
}

bool rft_action_map_bind(rft_action_map* map, rft_binding binding)
{
	ASSERT_FATAL(map);

	if (map->binding_count >= RFT_MAX_BINDINGS)
	{
		return false;
	}

	map->bindings[map->binding_count++] = binding;
	return true;
}

bool rft_action_map_unbind_index(rft_action_map* map, uint32_t index)
{
	ASSERT_FATAL(map);

	if (index >= map->binding_count)
	{
		return false;
	}

	uint32_t last = map->binding_count - 1;

	if (index != last)
	{
		map->bindings[index] = map->bindings[last];
	}

	map->binding_count--;
	return true;
}

void rft_action_map_clear(rft_action_map* map)
{
	ASSERT_FATAL(map);

	map->binding_count = 0;
	memset(map->bindings, 0, sizeof(map->bindings));
}

void rft_action_map_clear_action(rft_action_map* map, uint32_t action)
{
	ASSERT_FATAL(map);

	for (uint32_t i = 0; i < map->binding_count;)
	{
		if (map->bindings[i].action == action)
		{
			rft_action_map_unbind_index(map, i);
			continue;
		}

		i++;
	}
}

void rft_action_map_add_bindings(rft_action_map* map, const rft_binding* bindings, size_t count)
{
	ASSERT_FATAL(map);

	for (size_t i = 0; i < count; ++i)
	{
		rft_action_map_bind(map, bindings[i]);
	}
}

void rft_input_resolve(rft_input* input)
{
	ASSERT_FATAL(input);

	for (uint32_t m = 0; m < input->map_count; ++m)
	{
		rft_action_map* map = input->maps[m];
		memset(map->values, 0, sizeof(map->values));

		for (uint32_t i = 0; i < map->binding_count; ++i)
		{
			const rft_binding* b = &map->bindings[i];

			map->values[b->action] += binding_value(b, input);
		}
	}
}

float rft_action_value(const rft_action_map* map, uint32_t action)
{
	ASSERT_FATAL(map);
	ASSERT_FATAL(action < map->action_count);

	return map->values[action];
}

bool rft_action_pressed(const rft_action_map* map, uint32_t action)
{
	ASSERT_FATAL(map);
	ASSERT_FATAL(action < map->action_count);

	return map->values[action] > 0.5f;
}

static float binding_value(const rft_binding* b, const rft_input* input)
{
	float value = 0.0f;

	switch (b->type)
	{
		case RFT_INPUT_KEY:
		case RFT_INPUT_MOUSE_BUTTON:
		case RFT_INPUT_GAMEPAD_BUTTON:
		{
			if (rft_input_down(input, b->type, b->code))
			{
				value = 1.0f;
			}
		}
		break;

		case RFT_INPUT_GAMEPAD_AXIS:
		{
			const rft_gamepad* pad = &input->gamepads[0];

			if (pad->connected)
			{
				value = pad->axes[b->code];
			}
		}
		break;

		default:
			ASSERT_FATAL(false);
	}

	return value * b->scale;
}
