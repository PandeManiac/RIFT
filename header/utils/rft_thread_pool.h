#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*rft_task_fn)(void* arg);

typedef struct rft_task
{
	rft_task_fn fn;
	void*		arg;
} rft_task;

typedef struct rft_thread_pool rft_thread_pool;

rft_thread_pool* rft_thread_pool_create(uint32_t thread_count);
void			 rft_thread_pool_destroy(rft_thread_pool* pool);

void rft_thread_pool_submit(rft_thread_pool* pool, rft_task_fn fn, void* arg);
void rft_thread_pool_wait(rft_thread_pool* pool);

int rft_thread_pool_get_active_count(const rft_thread_pool* pool);
