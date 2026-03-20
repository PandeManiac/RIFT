#include "rft_debug_overlay_internal.h"
#include "utils/rft_assert.h"
#include "world/rft_streamer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

static const rft_shader_stage_desc stages[] = {
	{ RFT_SHADER_STAGE_VERTEX, "res/shader/debug_text.vert" },
	{ RFT_SHADER_STAGE_FRAGMENT, "res/shader/debug_text.frag" },
};

static HOT inline ALWAYS_INLINE int compare_floats(const void* a, const void* b)
{
	float fa = *(const float*)a;
	float fb = *(const float*)b;
	return (fa > fb) - (fa < fb);
}

static void buffer_text(rft_font_vertex** ptr, const rft_font_vertex* end, float x, float y, const char* text, uint32_t color);

rft_debug_overlay* rft_debug_overlay_create(void)
{
	rft_debug_overlay* overlay = calloc(1, sizeof(rft_debug_overlay));
	ASSERT_FATAL(overlay);

	overlay->font_texture = rft_debug_overlay_create_font_texture();

	rft_shader_init(&overlay->shader, stages, 2);

	rft_vertex_array_init(&overlay->vao);
	rft_vertex_array_bind(&overlay->vao);

	size_t single_frame_size = RFT_DEBUG_OVERLAY_MAX_CHARS * 6 * sizeof(rft_font_vertex);
	size_t buffer_size		 = single_frame_size * RFT_DEBUG_OVERLAY_BUFFERED_FRAMES;

	rft_buffer_init(&overlay->vbo, RFT_BUFFER_ARRAY);
	rft_buffer_set_data_persistent(&overlay->vbo, buffer_size);

	rft_vertex_array_attrib_f32(0, 2, sizeof(rft_font_vertex), offsetof(rft_font_vertex, x));
	rft_vertex_array_attrib_f32(1, 2, sizeof(rft_font_vertex), offsetof(rft_font_vertex, u));
	rft_vertex_array_attrib_u32(2, 1, sizeof(rft_font_vertex), offsetof(rft_font_vertex, color));

	rft_vertex_array_bind(NULL);

	rft_debug_overlay_update_system_stats(overlay);
	return overlay;
}

void rft_debug_overlay_destroy(rft_debug_overlay* overlay)
{
	ASSERT_FATAL(overlay);

	rft_buffer_destroy(&overlay->vbo);
	rft_vertex_array_destroy(&overlay->vao);
	rft_shader_destroy(&overlay->shader);

	glDeleteTextures(1, &overlay->font_texture);
	free(overlay);
}

void rft_debug_overlay_update(rft_debug_overlay*		overlay,
							  float						dt,
							  float						gpu_ms,
							  int						gpu_valid,
							  const rft_streamer_stats* stats,
							  float						camera_speed,
							  float						default_camera_speed)
{
	ASSERT_FATAL(overlay);
	ASSERT_FATAL(stats);

	overlay->frame_time_ms					 = dt * 1000.0f;
	overlay->gpu_time_ms					 = gpu_ms;
	overlay->gpu_time_valid					 = gpu_valid != 0;
	overlay->camera_speed					 = camera_speed;
	overlay->default_camera_speed			 = default_camera_speed;
	overlay->streamer_stats					 = *stats;
	overlay->frame_times[overlay->frame_idx] = dt;
	overlay->frame_idx						 = (overlay->frame_idx + 1) % RFT_DEBUG_OVERLAY_FRAME_HISTORY_SIZE;

	if (overlay->frame_count < RFT_DEBUG_OVERLAY_FRAME_HISTORY_SIZE)
	{
		overlay->frame_count++;
	}

	overlay->timer += dt;

	if (overlay->timer >= 0.25f && overlay->frame_count > 0)
	{
		overlay->timer = 0.0f;

		float sum = 0.0f;

		for (int i = 0; i < overlay->frame_count; ++i)
		{
			sum += overlay->frame_times[i];
		}

		float avg_dt	 = sum / (float)overlay->frame_count;
		overlay->avg_fps = avg_dt > 0.0f ? 1.0f / avg_dt : 0.0f;

		float sorted_times[RFT_DEBUG_OVERLAY_FRAME_HISTORY_SIZE];
		memcpy(sorted_times, overlay->frame_times, (size_t)overlay->frame_count * sizeof(float));
		qsort(sorted_times, (size_t)overlay->frame_count, sizeof(float), compare_floats);

		int	  idx_99_percent  = (int)((float)(overlay->frame_count - 1) * 0.99f);
		float p99_dt		  = sorted_times[idx_99_percent];
		overlay->p99_frame_ms = p99_dt * 1000.0f;

		rft_debug_overlay_update_system_stats(overlay);
	}
}

static float buffer_text_line_width(const char* text)
{
	size_t length = strlen(text);
	return RFT_DEBUG_CHAR_CELL_WIDTH * (float)length;
}

static void buffer_text_right(rft_font_vertex** ptr, const rft_font_vertex* end, float base_x, float y, const char* text, uint32_t color)
{
	float width = buffer_text_line_width(text);
	float x		= base_x - width;

	if (x < 0.0f)
	{
		x = 0.0f;
	}

	buffer_text(ptr, end, x, y, text, color);
}

static void buffer_text(rft_font_vertex** ptr, const rft_font_vertex* end, float x, float y, const char* text, uint32_t color)
{
	float cw	= RFT_DEBUG_CHAR_CELL_WIDTH;
	float scale = RFT_DEBUG_FONT_SCALE;

	while (*text)
	{
		if ((*ptr + 6) > end)
		{
			return;
		}

		unsigned char c = (unsigned char)*text++;

		if (c < 32U || c > 127U)
		{
			c = '?';
		}

		int i = (int)c - 32;

		int cx = (i % 16) * 6;
		int cy = (i / 16) * 8;

		float u0 = (float)cx / 96.0f;
		float v0 = (float)cy / 48.0f;
		float u1 = (float)(cx + 5) / 96.0f;
		float v1 = (float)(cy + 7) / 48.0f;

		float x0 = x;
		float y0 = y;
		float x1 = x + 5.0f * scale;
		float y1 = y + 7.0f * scale;

		rft_font_vertex* v = *ptr;

		v[0] = (rft_font_vertex) { x0, y0, u0, v0, color };
		v[1] = (rft_font_vertex) { x1, y0, u1, v0, color };
		v[2] = (rft_font_vertex) { x0, y1, u0, v1, color };

		v[3] = (rft_font_vertex) { x1, y0, u1, v0, color };
		v[4] = (rft_font_vertex) { x1, y1, u1, v1, color };
		v[5] = (rft_font_vertex) { x0, y1, u0, v1, color };

		*ptr += 6;
		x += cw;
	}
}

void rft_debug_overlay_render(rft_debug_overlay* overlay)
{
	ASSERT_FATAL(overlay);

	uint32_t idx = overlay->render_frame % RFT_DEBUG_OVERLAY_BUFFERED_FRAMES;
	overlay->render_frame++;

	size_t			 base_vert = idx * RFT_DEBUG_OVERLAY_MAX_CHARS * 6;
	rft_font_vertex* v_ptr	   = (rft_font_vertex*)overlay->vbo.mapped + base_vert;
	rft_font_vertex* start	   = v_ptr;
	rft_font_vertex* end	   = start + RFT_DEBUG_OVERLAY_MAX_CHARS * 6;

	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);

	float		w			 = (float)viewport[2];
	float		h			 = (float)viewport[3];
	float		margin_right = 12.0f;
	float		base_x		 = w - margin_right;
	float		y			 = 40.0f;
	uint32_t	color		 = 0xFFFFFFFF;
	char		buf[128];
	const float line_height = RFT_DEBUG_LINE_HEIGHT;

	snprintf(buf, sizeof(buf), "Frame (wall): %.1f ms", (double)overlay->frame_time_ms);
	buffer_text_right(&v_ptr, end, base_x, y, buf, color);
	y += line_height;

	if (overlay->gpu_time_valid)
	{
		snprintf(buf, sizeof(buf), "GPU (recent): %.1f ms", (double)overlay->gpu_time_ms);
	}

	else
	{
		snprintf(buf, sizeof(buf), "GPU (recent): n/a");
	}

	buffer_text_right(&v_ptr, end, base_x, y, buf, color);
	y += line_height;

	snprintf(buf, sizeof(buf), "FPS avg: %.1f", (double)overlay->avg_fps);
	buffer_text_right(&v_ptr, end, base_x, y, buf, color);
	y += line_height;

	snprintf(buf, sizeof(buf), "Frame p99: %.1f ms", (double)overlay->p99_frame_ms);
	buffer_text_right(&v_ptr, end, base_x, y, buf, color);
	y += line_height;

	snprintf(buf, sizeof(buf), "Move speed: %.1f (default %.1f)", (double)overlay->camera_speed, (double)overlay->default_camera_speed);
	buffer_text_right(&v_ptr, end, base_x, y, buf, color);
	y += line_height;

	snprintf(buf, sizeof(buf), "Speed keys: - / = / 0");
	buffer_text_right(&v_ptr, end, base_x, y, buf, color);
	y += line_height;

	snprintf(buf, sizeof(buf), "Sys CPU: %.1f%%", (double)overlay->cpu_usage);
	buffer_text_right(&v_ptr, end, base_x, y, buf, color);
	y += line_height;

	snprintf(buf, sizeof(buf), "Proc RAM: %.2f GB", (double)overlay->ram_usage_gb);
	buffer_text_right(&v_ptr, end, base_x, y, buf, color);
	y += line_height;

	if (overlay->vram_usage_valid)
	{
		snprintf(buf, sizeof(buf), "VRAM: %.0f MB", (double)overlay->vram_usage_mb);
		buffer_text_right(&v_ptr, end, base_x, y, buf, color);
		y += line_height;
	}

	snprintf(buf, sizeof(buf), "Chunks R: %.1f/%.1f", (double)overlay->streamer_stats.loaded_radius, (double)overlay->streamer_stats.target_radius);
	buffer_text_right(&v_ptr, end, base_x, y, buf, color);
	y += line_height;

	snprintf(buf, sizeof(buf), "Chunks: %u loaded, %u active", overlay->streamer_stats.loaded_chunks, overlay->streamer_stats.active_chunks);
	buffer_text_right(&v_ptr, end, base_x, y, buf, color);
	y += line_height;

	snprintf(buf, sizeof(buf), "Chunk RAM: %u MB", overlay->streamer_stats.chunk_memory_mb);
	buffer_text_right(&v_ptr, end, base_x, y, buf, color);
	y += line_height;

	snprintf(buf,
			 sizeof(buf),
			 "Face arena: %u/%u MB (peak %u)",
			 overlay->streamer_stats.face_arena_usage_mb,
			 overlay->streamer_stats.face_arena_capacity_mb,
			 overlay->streamer_stats.face_arena_peak_mb);

	buffer_text_right(&v_ptr, end, base_x, y, buf, color);
	y += line_height;

	float pressure_pct = overlay->streamer_stats.streaming_pressure * 100.0f;
	snprintf(buf,
			 sizeof(buf),
			 "Streaming %.0f%% (%u/%u tasks)",
			 (double)pressure_pct,
			 overlay->streamer_stats.active_jobs,
			 overlay->streamer_stats.max_active_jobs);

	buffer_text_right(&v_ptr, end, base_x, y, buf, color);
	y += line_height;

	size_t count = (size_t)(v_ptr - start);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	rft_shader_bind(&overlay->shader);
	rft_vertex_array_bind(&overlay->vao);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, overlay->font_texture);
	rft_shader_set_int(&overlay->shader, 0, 0);

	float ortho[16] = { 2.0f / w, 0.0f, 0.0f, 0.0f, 0.0f, -2.0f / h, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 1.0f };
	rft_shader_set_mat4(&overlay->shader, 1, ortho);

	glDrawArrays(GL_TRIANGLES, (GLint)base_vert, (GLsizei)count);

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}
