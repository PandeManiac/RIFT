#pragma once

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <time.h>
#include <windows.h>

#ifdef near
#undef near
#endif

#ifdef far
#undef far
#endif

typedef HANDLE			   pthread_t;
typedef CRITICAL_SECTION   pthread_mutex_t;
typedef CONDITION_VARIABLE pthread_cond_t;
typedef SRWLOCK			   pthread_rwlock_t;

typedef void* (*rft_pthread_start_routine)(void*);

typedef struct rft_pthread_start_ctx
{
	rft_pthread_start_routine fn;
	void*					  arg;
} rft_pthread_start_ctx;

static DWORD WINAPI rft_pthread_start_proc(LPVOID arg)
{
	rft_pthread_start_ctx*	  ctx	 = arg;
	rft_pthread_start_routine fn	 = ctx->fn;
	void*					  fn_arg = ctx->arg;
	HeapFree(GetProcessHeap(), 0, ctx);
	fn(fn_arg);
	return 0;
}

static inline int pthread_create(pthread_t* thread, const void* attr, void* (*start_routine)(void*), void* arg)
{
	(void)attr;

	rft_pthread_start_ctx* ctx = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*ctx));

	if (!ctx)
	{
		return ENOMEM;
	}

	ctx->fn	 = start_routine;
	ctx->arg = arg;

	HANDLE handle = CreateThread(NULL, 0, rft_pthread_start_proc, ctx, 0, NULL);

	if (!handle)
	{
		HeapFree(GetProcessHeap(), 0, ctx);
		return (int)GetLastError();
	}

	*thread = handle;
	return 0;
}

static inline int pthread_join(pthread_t thread, void** retval)
{
	if (WaitForSingleObject(thread, INFINITE) != WAIT_OBJECT_0)
	{
		return (int)GetLastError();
	}

	if (retval)
	{
		*retval = NULL;
	}

	return CloseHandle(thread) ? 0 : (int)GetLastError();
}

static inline int pthread_mutex_init(pthread_mutex_t* mutex, const void* attr)
{
	(void)attr;
	InitializeCriticalSection(mutex);
	return 0;
}

static inline int pthread_mutex_destroy(pthread_mutex_t* mutex)
{
	DeleteCriticalSection(mutex);
	return 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t* mutex)
{
	EnterCriticalSection(mutex);
	return 0;
}

static inline int pthread_mutex_trylock(pthread_mutex_t* mutex)
{
	return TryEnterCriticalSection(mutex) ? 0 : EBUSY;
}

static inline int pthread_mutex_unlock(pthread_mutex_t* mutex)
{
	LeaveCriticalSection(mutex);
	return 0;
}

static inline int pthread_cond_init(pthread_cond_t* cond, const void* attr)
{
	(void)attr;
	InitializeConditionVariable(cond);
	return 0;
}

static inline int pthread_cond_destroy(pthread_cond_t* cond)
{
	(void)cond;
	return 0;
}

static inline int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex)
{
	return SleepConditionVariableCS(cond, mutex, INFINITE) ? 0 : (int)GetLastError();
}

static inline DWORD rft_pthread_timeout_from_abstime(const struct timespec* abstime)
{
	struct timespec now;
	timespec_get(&now, TIME_UTC);

	int64_t diff_sec  = (int64_t)abstime->tv_sec - (int64_t)now.tv_sec;
	int64_t diff_nsec = (int64_t)abstime->tv_nsec - (int64_t)now.tv_nsec;
	int64_t diff_ms	  = diff_sec * 1000LL + diff_nsec / 1000000LL;

	if (diff_nsec > 0 && (diff_nsec % 1000000LL) != 0)
	{
		diff_ms += 1;
	}

	if (diff_ms <= 0)
	{
		return 0;
	}

	if (diff_ms > (int64_t)UINT32_MAX)
	{
		return INFINITE - 1;
	}

	return (DWORD)diff_ms;
}

static inline int pthread_cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex, const struct timespec* abstime)
{
	DWORD timeout_ms = rft_pthread_timeout_from_abstime(abstime);

	if (SleepConditionVariableCS(cond, mutex, timeout_ms))
	{
		return 0;
	}

	DWORD error = GetLastError();
	return error == ERROR_TIMEOUT ? ETIMEDOUT : (int)error;
}

static inline int pthread_cond_signal(pthread_cond_t* cond)
{
	WakeConditionVariable(cond);
	return 0;
}

static inline int pthread_cond_broadcast(pthread_cond_t* cond)
{
	WakeAllConditionVariable(cond);
	return 0;
}

static inline int pthread_rwlock_init(pthread_rwlock_t* lock, const void* attr)
{
	(void)attr;
	InitializeSRWLock(lock);
	return 0;
}

static inline int pthread_rwlock_destroy(pthread_rwlock_t* lock)
{
	(void)lock;
	return 0;
}

static inline int pthread_rwlock_rdlock(pthread_rwlock_t* lock)
{
	AcquireSRWLockShared(lock);
	return 0;
}

static inline int pthread_rwlock_wrlock(pthread_rwlock_t* lock)
{
	AcquireSRWLockExclusive(lock);
	return 0;
}

static inline int pthread_rwlock_unlock(pthread_rwlock_t* lock)
{
	ReleaseSRWLockExclusive(lock);
	return 0;
}

static inline int pthread_rwlock_unlock_shared(pthread_rwlock_t* lock)
{
	ReleaseSRWLockShared(lock);
	return 0;
}

#define RFT_RWLOCK_RDLOCK(lock) pthread_rwlock_rdlock(lock)
#define RFT_RWLOCK_WRLOCK(lock) pthread_rwlock_wrlock(lock)
#define RFT_RWLOCK_UNLOCK_RD(lock) pthread_rwlock_unlock_shared(lock)
#define RFT_RWLOCK_UNLOCK_WR(lock) pthread_rwlock_unlock(lock)

#else

#include <pthread.h>

#define RFT_RWLOCK_RDLOCK(lock) pthread_rwlock_rdlock(lock)
#define RFT_RWLOCK_WRLOCK(lock) pthread_rwlock_wrlock(lock)
#define RFT_RWLOCK_UNLOCK_RD(lock) pthread_rwlock_unlock(lock)
#define RFT_RWLOCK_UNLOCK_WR(lock) pthread_rwlock_unlock(lock)

#endif
