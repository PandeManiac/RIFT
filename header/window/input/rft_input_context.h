#pragma once

#include "window/input/rft_input_map.h"

#define RFT_INPUT_CONTEXT_STACK 8

typedef struct rft_input_context
{
	rft_action_map* stack[RFT_INPUT_CONTEXT_STACK];
	int				count;
} rft_input_context;

void rft_input_context_init(rft_input_context* ctx);

void rft_input_context_push(rft_input_context* ctx, rft_action_map* map);
void rft_input_context_pop(rft_input_context* ctx);

rft_action_map* rft_input_context_top(rft_input_context* ctx);

void rft_input_context_resolve(rft_input_context* ctx, rft_input* input);
