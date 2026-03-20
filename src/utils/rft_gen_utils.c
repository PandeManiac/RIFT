#include "utils/rft_gen_utils.h"
#include "utils/rft_assert.h"

#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <stdlib.h>
#include <windows.h>

#ifdef near
#undef near
#endif

#ifdef far
#undef far
#endif
#else
#include <limits.h>
#include <unistd.h>
#endif

void rft_set_cwd_to_executable(void)
{
#ifdef _WIN32
	char  exe_path[MAX_PATH];
	DWORD len = GetModuleFileNameA(NULL, exe_path, (DWORD)COUNT_OF(exe_path));
	ASSERT_FATAL(len > 0);
	ASSERT_FATAL(len < (DWORD)COUNT_OF(exe_path));

	char* last_slash = strrchr(exe_path, '\\');
	if (!last_slash)
	{
		last_slash = strrchr(exe_path, '/');
	}
	ASSERT_FATAL(last_slash);

	*last_slash = '\0';

	ASSERT_FATAL(_chdir(exe_path) == 0);
#else
	char exe_path[PATH_MAX];

	ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
	ASSERT_FATAL(len > 0);
	ASSERT_FATAL((size_t)len < sizeof(exe_path) - 1);

	exe_path[len] = '\0';

	char* last_slash = strrchr(exe_path, '/');
	ASSERT_FATAL(last_slash);

	*last_slash = '\0';

	ASSERT_FATAL(chdir(exe_path) == 0);
#endif
}
