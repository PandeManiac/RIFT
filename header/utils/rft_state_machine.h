#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define RFT_STATE_MACHINE_MAX_STATES 32
#define RFT_STATE_BIT(x) (1u << (x))

typedef struct rft_state_rules
{
	const uint32_t* rules;
	uint32_t		count;
} rft_state_rules;

bool	 rft_state_try_transition(_Atomic uint32_t* state, const rft_state_rules* rules, uint32_t to);
uint32_t rft_state_load(const _Atomic uint32_t* state);
void	 rft_state_store(_Atomic uint32_t* state, uint32_t value);
