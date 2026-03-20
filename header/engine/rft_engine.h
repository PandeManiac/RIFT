#pragma once

typedef struct rft_engine rft_engine;

rft_engine* rft_engine_create(void);
void		rft_engine_destroy(rft_engine* engine);
void		rft_engine_run(rft_engine* engine);
