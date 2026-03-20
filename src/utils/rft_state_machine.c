#include "utils/rft_state_machine.h"
#include "utils/rft_assert.h"

bool rft_state_try_transition(_Atomic uint32_t* state, const rft_state_rules* rules, uint32_t to)
{
	uint32_t expected = atomic_load_explicit(state, memory_order_acquire);

	ASSERT_FATAL(expected < rules->count);
	ASSERT_FATAL(to < rules->count);

	const uint32_t legal = (rules->rules[expected] >> to) & 1u;

	if (UNLIKELY(!legal))
	{
		return false;
	}

	return atomic_compare_exchange_strong_explicit(state, &expected, to, memory_order_acq_rel, memory_order_acquire);
}

uint32_t rft_state_load(const _Atomic uint32_t* state)
{
	return atomic_load_explicit(state, memory_order_acquire);
}

void rft_state_store(_Atomic uint32_t* state, uint32_t value)
{
	atomic_store_explicit(state, value, memory_order_release);
}
