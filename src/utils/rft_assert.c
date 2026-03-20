#include "utils/rft_assert.h"

#ifndef RFT_ASSERT_QUIET
#include "utils/rft_platform_stdio.h"
#include <stdio.h>
#endif // RFT_ASSERT_QUIET

#include <stdlib.h>

NORETURN void rft_assert_fail(const char* expr, const char* file, int line, const char* func)
{
#ifndef RFT_ASSERT_QUIET
	rft_flockfile(stderr);
	fprintf(stderr, "\n");
	fprintf(stderr, "============================================================\n");
	fprintf(stderr, " ASSERTION FAILED\n");
	fprintf(stderr, "============================================================\n");
	fprintf(stderr, " Expression: %s\n", expr);
	fprintf(stderr, " Location  : %s:%d\n", file, line);
	fprintf(stderr, " Function  : %s\n", func);
	fprintf(stderr, "============================================================\n");
	fflush(stderr);
	rft_funlockfile(stderr);
#endif // RFT_ASSERT_QUIET

	abort();
	UNREACHABLE;
}
