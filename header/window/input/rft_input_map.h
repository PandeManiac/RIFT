#pragma once

#include "window/input/rft_input.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct rft_action_map
{
	rft_input* input;

	uint32_t action_count;

	rft_binding bindings[RFT_MAX_BINDINGS];
	uint32_t	binding_count;

	float values[RFT_MAX_ACTIONS];

} rft_action_map;

rft_action_map* rft_input_create_map(rft_input* input, uint32_t action_count);
void			rft_input_destroy_map(rft_action_map* map);

bool rft_action_map_bind(rft_action_map* map, rft_binding binding);
bool rft_action_map_unbind_index(rft_action_map* map, uint32_t binding_index);

void rft_action_map_clear(rft_action_map* map);
void rft_action_map_clear_action(rft_action_map* map, uint32_t action);

void rft_action_map_add_bindings(rft_action_map* map, const rft_binding* bindings, size_t count);

void rft_input_resolve(rft_input* input);

float rft_action_value(const rft_action_map* map, uint32_t action);
bool  rft_action_pressed(const rft_action_map* map, uint32_t action);
