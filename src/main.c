#include "engine/rft_engine.h"

int main(void)
{
	rft_engine* engine = rft_engine_create();
	rft_engine_run(engine);
	rft_engine_destroy(engine);
}
