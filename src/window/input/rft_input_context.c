#include "window/input/rft_input_context.h"
#include "utils/rft_assert.h"

void rft_input_context_init(rft_input_context* ctx)
{
	ASSERT_FATAL(ctx);
	ctx->count = 0;
}

void rft_input_context_push(rft_input_context* ctx, rft_action_map* map)
{
	ASSERT_FATAL(ctx);
	ASSERT_FATAL(map);
	ASSERT_FATAL(ctx->count < RFT_INPUT_CONTEXT_STACK);

	ctx->stack[ctx->count++] = map;
}

void rft_input_context_pop(rft_input_context* ctx)
{
	ASSERT_FATAL(ctx);
	ASSERT_FATAL(ctx->count > 0);

	ctx->count--;
}

rft_action_map* rft_input_context_top(rft_input_context* ctx)
{
	ASSERT_FATAL(ctx);
	ASSERT_FATAL(ctx->count > 0);

	return ctx->stack[ctx->count - 1];
}

void rft_input_context_resolve(rft_input_context* ctx, rft_input* input)
{
	ASSERT_FATAL(ctx);
	ASSERT_FATAL(input);

	for (int i = 0; i < ctx->count; ++i)
	{
		rft_action_map* map = ctx->stack[i];
		rft_input_resolve(input);
		(void)map;
	}
}
