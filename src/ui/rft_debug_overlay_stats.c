#include "rft_debug_overlay_internal.h"
#include "utils/rft_assert.h"
#include <stdio.h>

static void rft_debug_overlay_update_cpu_usage(rft_debug_overlay* overlay)
{
	FILE* file = fopen("/proc/stat", "r");
	if (!file)
	{
		return;
	}

	unsigned long long user	   = 0U;
	unsigned long long nice	   = 0U;
	unsigned long long system  = 0U;
	unsigned long long idle	   = 0U;
	unsigned long long iowait  = 0U;
	unsigned long long irq	   = 0U;
	unsigned long long softirq = 0U;
	unsigned long long steal   = 0U;

	if (fscanf(file, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) == 8)
	{
		unsigned long long total	  = user + nice + system + idle + iowait + irq + softirq + steal;
		unsigned long long idle_ticks = idle + iowait;

		if (overlay->prev_total_ticks == 0U)
		{
			overlay->prev_total_ticks = total;
			overlay->prev_idle_ticks  = idle_ticks;
			fclose(file);
			return;
		}

		unsigned long long total_diff = total - overlay->prev_total_ticks;
		unsigned long long idle_diff  = idle_ticks - overlay->prev_idle_ticks;

		if (total_diff > 0U)
		{
			overlay->cpu_usage = 100.0f * (float)(total_diff - idle_diff) / (float)total_diff;
		}

		overlay->prev_total_ticks = total;
		overlay->prev_idle_ticks  = idle_ticks;
	}

	fclose(file);
}

static void rft_debug_overlay_update_ram_usage(rft_debug_overlay* overlay)
{
	FILE* file = fopen("/proc/self/status", "r");

	if (!file)
	{
		return;
	}

	char line[256];

	while (fgets(line, sizeof(line), file))
	{
		unsigned long rss_kb = 0UL;

		if (sscanf(line, "VmRSS: %lu kB", &rss_kb) == 1)
		{
			overlay->ram_usage_gb = (float)rss_kb / (1024.0f * 1024.0f);
			fclose(file);
			return;
		}
	}

	fclose(file);
}

static void rft_debug_overlay_update_vram_usage(rft_debug_overlay* overlay)
{
	GLint total_mem			  = 0;
	GLint avail_mem			  = 0;
	overlay->vram_usage_valid = false;

	while (glGetError() != GL_NO_ERROR)
	{
	}

	glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &total_mem);

	if (glGetError() == GL_NO_ERROR)
	{
		glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &avail_mem);
		overlay->vram_usage_mb	  = (float)(total_mem - avail_mem) / 1024.0f;
		overlay->vram_usage_valid = true;
		return;
	}

	GLint free_mem_ati[4];
	glGetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI, free_mem_ati);

	if (glGetError() == GL_NO_ERROR)
	{
		(void)free_mem_ati;
	}
}

void rft_debug_overlay_update_system_stats(rft_debug_overlay* overlay)
{
	ASSERT_FATAL(overlay);

	rft_debug_overlay_update_cpu_usage(overlay);
	rft_debug_overlay_update_ram_usage(overlay);
	rft_debug_overlay_update_vram_usage(overlay);
}
