#pragma once

#include <stdio.h>

#ifdef _WIN32

static inline void rft_flockfile(FILE* stream)
{
	_lock_file(stream);
}

static inline void rft_funlockfile(FILE* stream)
{
	_unlock_file(stream);
}

#else

static inline void rft_flockfile(FILE* stream)
{
	flockfile(stream);
}

static inline void rft_funlockfile(FILE* stream)
{
	funlockfile(stream);
}

#endif
