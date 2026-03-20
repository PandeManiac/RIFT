#pragma once

#include "window/input/rft_input_map.h"
#include "window/input/rft_player_action_table.h"

typedef enum rft_player_action
{
#define X(name, str, type, code, scale) name,
	RFT_PLAYER_ACTION_TABLE(X)
#undef X
	RFT_PLAYER_ACTION_COUNT
} rft_player_action;

void		rft_player_default_bindings(rft_action_map* map);
const char* rft_player_action_name(rft_player_action action);
const char* rft_player_action_string(rft_player_action action);
