#include "utils/rft_thread_pool.h"
#include "utils/rft_assert.h"
#include "utils/rft_platform_threads.h"

#include <stdlib.h>
#include <time.h>

#define RFT_TASK_QUEUE_SIZE 4096

typedef struct rft_worker
{
	pthread_t		 thread;
	rft_thread_pool* pool;
	uint32_t		 id;

	rft_task		 queue[RFT_TASK_QUEUE_SIZE];
	_Atomic uint32_t head;
	_Atomic uint32_t tail;

	pthread_mutex_t mutex;
} rft_worker;

struct rft_thread_pool
{
	rft_worker* workers;
	uint32_t	worker_count;

	_Atomic bool running;
	_Atomic int	 active_tasks;

	pthread_mutex_t wait_mutex;
	pthread_cond_t	wait_cond;

	pthread_mutex_t global_mutex;
	pthread_cond_t	global_cond;

	_Atomic uint32_t submit_index;
};

static rft_task* worker_pop(rft_worker* worker)
{
	pthread_mutex_lock(&worker->mutex);

	uint32_t head = atomic_load_explicit(&worker->head, memory_order_relaxed);
	uint32_t tail = atomic_load_explicit(&worker->tail, memory_order_relaxed);

	if (head == tail)
	{
		pthread_mutex_unlock(&worker->mutex);
		return NULL;
	}

	rft_task* task = &worker->queue[head % RFT_TASK_QUEUE_SIZE];
	atomic_store_explicit(&worker->head, head + 1, memory_order_relaxed);

	pthread_mutex_unlock(&worker->mutex);
	return task;
}

static rft_task* worker_steal(rft_worker* worker)
{
	if (pthread_mutex_trylock(&worker->mutex) != 0)
	{
		return NULL;
	}

	uint32_t head = atomic_load_explicit(&worker->head, memory_order_relaxed);
	uint32_t tail = atomic_load_explicit(&worker->tail, memory_order_relaxed);

	if (head == tail)
	{
		pthread_mutex_unlock(&worker->mutex);
		return NULL;
	}

	rft_task* task = &worker->queue[(tail - 1) % RFT_TASK_QUEUE_SIZE];
	atomic_store_explicit(&worker->tail, tail - 1, memory_order_relaxed);

	pthread_mutex_unlock(&worker->mutex);
	return task;
}

static void* worker_proc(void* arg)
{
	rft_worker*		 self = arg;
	rft_thread_pool* pool = self->pool;

	while (atomic_load_explicit(&pool->running, memory_order_relaxed))
	{
		rft_task* task = worker_pop(self);

		if (!task)
		{
			for (uint32_t i = 0; i < pool->worker_count; ++i)
			{
				if (i == self->id)
				{
					continue;
				}

				task = worker_steal(&pool->workers[i]);

				if (task)
				{
					break;
				}
			}
		}

		if (task)
		{
			task->fn(task->arg);
			int remaining = atomic_fetch_sub_explicit(&pool->active_tasks, 1, memory_order_release) - 1;

			if (remaining == 0)
			{
				pthread_mutex_lock(&pool->wait_mutex);
				pthread_cond_broadcast(&pool->wait_cond);
				pthread_mutex_unlock(&pool->wait_mutex);
			}
		}

		else
		{
			pthread_mutex_lock(&pool->global_mutex);
			struct timespec ts;
			timespec_get(&ts, TIME_UTC);
			ts.tv_nsec += 1000000;

			if (ts.tv_nsec >= 1000000000)
			{
				ts.tv_sec++;
				ts.tv_nsec -= 1000000000;
			}

			pthread_cond_timedwait(&pool->global_cond, &pool->global_mutex, &ts);
			pthread_mutex_unlock(&pool->global_mutex);
		}
	}

	return NULL;
}

rft_thread_pool* rft_thread_pool_create(uint32_t thread_count)
{
	rft_thread_pool* pool = calloc(1, sizeof(rft_thread_pool));
	ASSERT_FATAL(pool);

	pool->worker_count = thread_count;
	pool->workers	   = calloc(thread_count, sizeof(rft_worker));
	ASSERT_FATAL(pool->workers);

	atomic_store_explicit(&pool->running, true, memory_order_relaxed);
	atomic_store_explicit(&pool->active_tasks, 0, memory_order_relaxed);

	pthread_mutex_init(&pool->wait_mutex, NULL);
	pthread_cond_init(&pool->wait_cond, NULL);
	pthread_mutex_init(&pool->global_mutex, NULL);
	pthread_cond_init(&pool->global_cond, NULL);

	for (uint32_t i = 0; i < thread_count; ++i)
	{
		pool->workers[i].pool = pool;
		pool->workers[i].id	  = i;
		pthread_mutex_init(&pool->workers[i].mutex, NULL);
		pthread_create(&pool->workers[i].thread, NULL, worker_proc, &pool->workers[i]);
	}

	return pool;
}

void rft_thread_pool_destroy(rft_thread_pool* pool)
{
	ASSERT_FATAL(pool);

	rft_thread_pool_wait(pool);
	atomic_store_explicit(&pool->running, false, memory_order_relaxed);

	pthread_mutex_lock(&pool->global_mutex);
	pthread_cond_broadcast(&pool->global_cond);
	pthread_mutex_unlock(&pool->global_mutex);

	for (uint32_t i = 0; i < pool->worker_count; ++i)
	{
		pthread_join(pool->workers[i].thread, NULL);
		pthread_mutex_destroy(&pool->workers[i].mutex);
	}

	pthread_mutex_destroy(&pool->wait_mutex);
	pthread_cond_destroy(&pool->wait_cond);
	pthread_mutex_destroy(&pool->global_mutex);
	pthread_cond_destroy(&pool->global_cond);

	free(pool->workers);
	free(pool);
}

void rft_thread_pool_submit(rft_thread_pool* pool, rft_task_fn fn, void* arg)
{
	ASSERT_FATAL(pool);

	uint32_t	index  = atomic_fetch_add_explicit(&pool->submit_index, 1, memory_order_relaxed) % pool->worker_count;
	rft_worker* worker = &pool->workers[index];

	pthread_mutex_lock(&worker->mutex);

	uint32_t tail = atomic_load_explicit(&worker->tail, memory_order_relaxed);
	uint32_t head = atomic_load_explicit(&worker->head, memory_order_relaxed);

	if (tail - head >= RFT_TASK_QUEUE_SIZE)
	{
		pthread_mutex_unlock(&worker->mutex);
		fn(arg);
		return;
	}

	worker->queue[tail % RFT_TASK_QUEUE_SIZE] = (rft_task) { fn, arg };
	atomic_fetch_add_explicit(&pool->active_tasks, 1, memory_order_release);
	atomic_store_explicit(&worker->tail, tail + 1, memory_order_release);

	pthread_mutex_unlock(&worker->mutex);

	pthread_mutex_lock(&pool->global_mutex);
	pthread_cond_signal(&pool->global_cond);
	pthread_mutex_unlock(&pool->global_mutex);
}

void rft_thread_pool_wait(rft_thread_pool* pool)
{
	ASSERT_FATAL(pool);

	pthread_mutex_lock(&pool->wait_mutex);

	while (atomic_load_explicit(&pool->active_tasks, memory_order_acquire) > 0)
	{
		pthread_cond_wait(&pool->wait_cond, &pool->wait_mutex);
	}

	pthread_mutex_unlock(&pool->wait_mutex);
}

int rft_thread_pool_get_active_count(const rft_thread_pool* pool)
{
	ASSERT_FATAL(pool);
	return atomic_load_explicit(&pool->active_tasks, memory_order_acquire);
}
