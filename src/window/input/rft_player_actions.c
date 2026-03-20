#include "window/input/rft_player_actions.h"

void rft_player_default_bindings(rft_action_map* map)
{
#define X(name, str, type, code, scale) rft_action_map_bind(map, (rft_binding) { name, type, code, scale });
	RFT_PLAYER_ACTION_TABLE(X)
#undef X
}

const char* rft_player_action_name(rft_player_action action)
{
	static const char* names[RFT_PLAYER_ACTION_COUNT] = {
#define X(name, str, type, code, scale) #name,
		RFT_PLAYER_ACTION_TABLE(X)
#undef X
	};

	return names[action];
}

const char* rft_player_action_string(rft_player_action action)
{
	static const char* strings[RFT_PLAYER_ACTION_COUNT] = {
#define X(name, str, type, code, scale) str,
		RFT_PLAYER_ACTION_TABLE(X)
#undef X
	};

	return strings[action];
}
