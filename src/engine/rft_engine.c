#include "engine/rft_engine.h"
#include "engine/rft_engine_configs.h"

#include "window/input/rft_input.h"
#include "window/input/rft_input_context.h"
#include "window/input/rft_input_map.h"
#include "window/input/rft_player_actions.h"
#include "window/rft_window.h"

#include "render/rft_camera.h"
#include "render/rft_vertex_array.h"
#include "render/shader/rft_shader.h"
#include "render/texture/rft_texture_array.h"

#include "ui/rft_debug_overlay.h"
#include "world/rft_streamer.h"

#include "utils/rft_assert.h"
#include "utils/rft_file.h"
#include "utils/rft_gen_utils.h"

#include <cglm/struct/frustum.h>
#include <json-c/json.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RFT_GPU_QUERY_RING_SIZE 3U
#define RFT_CAMERA_SPEED_STEP 25.0f
#define RFT_CAMERA_SPEED_MIN 25.0f
#define RFT_CHUNK_SHADER_FOG_DISTANCE_LOCATION 5
#define RFT_CHUNK_SHADER_FOG_COLOR_NEAR_LOCATION 6
#define RFT_CHUNK_SHADER_FOG_COLOR_FAR_LOCATION 7
#define RFT_CHUNK_SHADER_FOG_FRINGE_LOCATION 8
#define RFT_CHUNK_SHADER_SUN_DIR_LOCATION 9
#define RFT_CHUNK_SHADER_SUN_COLOR_LOCATION 10
#define RFT_CHUNK_SHADER_AMBIENT_COLOR_LOCATION 11
#define RFT_CHUNK_SHADER_MOON_DIR_LOCATION 12
#define RFT_CHUNK_SHADER_MOON_COLOR_LOCATION 13
#define RFT_POST_SHADER_INV_VIEWPORT_LOCATION 0
#define RFT_POST_SHADER_DEPTH_PARAMS_LOCATION 1
#define RFT_POST_SHADER_SPLIT_PARAMS_LOCATION 2
#define RFT_SKY_SHADER_INV_VIEW_PROJ_LOCATION 0
#define RFT_SKY_SHADER_CAMERA_POS_LOCATION 4
#define RFT_SKY_SHADER_SUN_DIR_LOCATION 5
#define RFT_SKY_SHADER_MOON_DIR_LOCATION 6
#define RFT_SKY_SHADER_DAY_ZENITH_LOCATION 7
#define RFT_SKY_SHADER_DAY_HORIZON_LOCATION 8
#define RFT_SKY_SHADER_NIGHT_ZENITH_LOCATION 9
#define RFT_SKY_SHADER_NIGHT_HORIZON_LOCATION 10
#define RFT_SKY_SHADER_ATMOSPHERE_PARAMS_LOCATION 11

typedef struct rft_atmosphere
{
	vec3s sun_dir;
	vec3s moon_dir;
	vec3s sun_color;
	vec3s moon_color;
	vec3s ambient_color;
	vec3s fog_near;
	vec3s fog_far;
	vec3s day_zenith;
	vec3s day_horizon;
	vec3s night_zenith;
	vec3s night_horizon;
	float day_amount;
	float twilight_amount;
	float night_amount;
	float stars_amount;
} rft_atmosphere;

typedef struct rft_post_targets
{
	GLuint framebuffer;
	GLuint color_texture;
	GLuint depth_texture;
	ivec2s size;
} rft_post_targets;

struct rft_engine
{
	rft_window window;
	rft_camera camera;
	float	   default_camera_speed;
	float	   time_of_day;

	rft_input_context input_ctx;
	rft_action_map*	  player_map;

	rft_shader		  chunk_shader;
	rft_shader		  sky_shader;
	rft_shader		  post_shader;
	rft_vertex_array  chunk_vao;
	rft_vertex_array  post_vao;
	rft_texture_array atlas;
	rft_post_targets  post_targets;

	rft_streamer*	   streamer;
	rft_debug_overlay* debug_overlay;
	GLuint			   gpu_time_queries[RFT_GPU_QUERY_RING_SIZE];
	uint32_t		   gpu_query_index;
	float			   last_gpu_time_ms;
	bool			   gpu_time_valid;
	rft_streamer_stats streamer_stats;

	bool running;
};

static const rft_shader_stage_desc stages[] = {
	{ RFT_SHADER_STAGE_VERTEX, "res/shader/chunk.vert" },
	{ RFT_SHADER_STAGE_FRAGMENT, "res/shader/chunk.frag" },
};

static const rft_shader_stage_desc sky_stages[] = {
	{ RFT_SHADER_STAGE_VERTEX, "res/shader/post.vert" },
	{ RFT_SHADER_STAGE_FRAGMENT, "res/shader/sky.frag" },
};

static const rft_shader_stage_desc post_stages[] = {
	{ RFT_SHADER_STAGE_VERTEX, "res/shader/post.vert" },
	{ RFT_SHADER_STAGE_FRAGMENT, "res/shader/post.frag" },
};

static void rft_engine_init(rft_engine* engine);
static void rft_engine_shutdown(rft_engine* engine);
static void rft_engine_frame(rft_engine* engine, float dt);
static void rft_engine_handle_speed_input(rft_engine* engine, const rft_input* input);
static void rft_post_targets_resize(rft_post_targets* targets, ivec2s size);
static void rft_post_targets_destroy(rft_post_targets* targets);
static void rft_engine_render_post(const rft_engine* engine);
static rft_atmosphere rft_engine_sample_atmosphere(float time_of_day);
static void rft_engine_render_sky(const rft_engine* engine, const rft_atmosphere* atmosphere, const mat4s* inv_view_proj);

rft_engine* rft_engine_create(void)
{
	rft_engine* engine = malloc(sizeof(rft_engine));
	ASSERT_FATAL(engine);

	memset(engine, 0, sizeof(*engine));
	rft_engine_init(engine);

	return engine;
}

void rft_engine_destroy(rft_engine* engine)
{
	ASSERT_FATAL(engine);

	rft_engine_shutdown(engine);
	free(engine);
}

void rft_engine_run(rft_engine* engine)
{
	ASSERT_FATAL(engine);

	double last_time = glfwGetTime();

	while (engine->running && !rft_window_should_close(&engine->window))
	{
		double now = glfwGetTime();

		float dt  = (float)(now - last_time);
		last_time = now;

		rft_engine_frame(engine, dt);
	}
}

static void load_block_assets(rft_texture_array* atlas)
{
	size_t json_size   = 0;
	char*  json_buffer = rft_read_file("res/textures/blocks/blocks_array.json", &json_size);
	ASSERT_FATAL(json_buffer);

	struct json_object* root = json_tokener_parse(json_buffer);
	ASSERT_FATAL(root);

	struct json_object* tile_size_obj	= NULL;
	struct json_object* layer_count_obj = NULL;

	ASSERT_FATAL(json_object_object_get_ex(root, "tile_size", &tile_size_obj));
	ASSERT_FATAL(json_object_object_get_ex(root, "layer_count", &layer_count_obj));

	int tile_size	= json_object_get_int(tile_size_obj);
	int layer_count = json_object_get_int(layer_count_obj);

	size_t bin_size = 0;
	void*  bin_data = rft_read_file("res/textures/blocks/blocks_array.bin", &bin_size);
	ASSERT_FATAL(bin_data);

	rft_texture_array_init(atlas, tile_size, tile_size, layer_count, bin_data, 1, 0);

	free(bin_data);
	json_object_put(root);
	free(json_buffer);
}

static float rft_smoothstep(float a, float b, float x)
{
	float t = (x - a) / (b - a);
	t	   = fminf(fmaxf(t, 0.0f), 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

static vec3s rft_vec3_mix(vec3s a, vec3s b, float t)
{
	return (vec3s) {
		{
			a.x + (b.x - a.x) * t,
			a.y + (b.y - a.y) * t,
			a.z + (b.z - a.z) * t,
		},
	};
}

static vec3s rft_vec3_scale(vec3s v, float s)
{
	return (vec3s) {
		{
			v.x * s,
			v.y * s,
			v.z * s,
		},
	};
}

static vec3s rft_vec3_add(vec3s a, vec3s b)
{
	return (vec3s) {
		{
			a.x + b.x,
			a.y + b.y,
			a.z + b.z,
		},
	};
}

static rft_atmosphere rft_engine_sample_atmosphere(float time_of_day)
{
	const float cycle_seconds = 240.0f;
	float phase = fmodf(time_of_day / cycle_seconds, 1.0f);
	float angle = phase * glm_rad(360.0f);

	vec3s sun_dir = glms_vec3_normalize((vec3s) {
		{
			cosf(angle),
			sinf(angle),
			sinf(angle * 0.37f) * 0.22f,
		},
	});
	vec3s moon_dir = glms_vec3_negate(sun_dir);

	float day_amount = rft_smoothstep(-0.10f, 0.24f, sun_dir.y);
	float night_amount = 1.0f - rft_smoothstep(-0.26f, 0.02f, sun_dir.y);
	float twilight_amount = fmaxf(0.0f, 1.0f - day_amount - night_amount);
	float stars_amount = night_amount * night_amount;

	vec3s day_zenith = { { 0.22f, 0.53f, 0.92f } };
	vec3s day_horizon = { { 0.78f, 0.88f, 1.00f } };
	vec3s sunset_glow = { { 1.00f, 0.50f, 0.18f } };
	vec3s night_zenith = { { 0.02f, 0.04f, 0.10f } };
	vec3s night_horizon = { { 0.05f, 0.08f, 0.16f } };

	vec3s sun_color = rft_vec3_mix((vec3s) { { 1.00f, 0.58f, 0.30f } }, (vec3s) { { 1.00f, 0.97f, 0.90f } }, day_amount);
	vec3s moon_color = { { 0.35f, 0.44f, 0.60f } };
	vec3s ambient_color = rft_vec3_add(
		rft_vec3_scale(day_horizon, 0.18f + day_amount * 0.20f),
		rft_vec3_scale(night_horizon, 0.10f + stars_amount * 0.12f)
	);
	vec3s fog_near = rft_vec3_mix(
		rft_vec3_mix(night_horizon, day_horizon, day_amount),
		sunset_glow,
		twilight_amount * 0.35f
	);
	vec3s fog_far = rft_vec3_mix(
		rft_vec3_mix(night_zenith, day_zenith, day_amount),
		sunset_glow,
		twilight_amount * 0.55f
	);

	return (rft_atmosphere) {
		.sun_dir = sun_dir,
		.moon_dir = moon_dir,
		.sun_color = sun_color,
		.moon_color = moon_color,
		.ambient_color = ambient_color,
		.fog_near = fog_near,
		.fog_far = fog_far,
		.day_zenith = day_zenith,
		.day_horizon = day_horizon,
		.night_zenith = night_zenith,
		.night_horizon = night_horizon,
		.day_amount = day_amount,
		.twilight_amount = twilight_amount,
		.night_amount = night_amount,
		.stars_amount = stars_amount,
	};
}

static void rft_engine_render_sky(const rft_engine* engine, const rft_atmosphere* atmosphere, const mat4s* inv_view_proj)
{
	ASSERT_FATAL(engine);
	ASSERT_FATAL(atmosphere);
	ASSERT_FATAL(inv_view_proj);

	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	rft_shader_bind(&engine->sky_shader);
	rft_vertex_array_bind(&engine->post_vao);

	rft_shader_set_mat4(&engine->sky_shader, RFT_SKY_SHADER_INV_VIEW_PROJ_LOCATION, &inv_view_proj->raw[0][0]);
	rft_shader_set_vec3(&engine->sky_shader,
						RFT_SKY_SHADER_CAMERA_POS_LOCATION,
						engine->camera.cfg.pos.x,
						engine->camera.cfg.pos.y,
						engine->camera.cfg.pos.z);
	rft_shader_set_vec3(&engine->sky_shader,
						RFT_SKY_SHADER_SUN_DIR_LOCATION,
						atmosphere->sun_dir.x,
						atmosphere->sun_dir.y,
						atmosphere->sun_dir.z);
	rft_shader_set_vec3(&engine->sky_shader,
						RFT_SKY_SHADER_MOON_DIR_LOCATION,
						atmosphere->moon_dir.x,
						atmosphere->moon_dir.y,
						atmosphere->moon_dir.z);
	rft_shader_set_vec3(&engine->sky_shader,
						RFT_SKY_SHADER_DAY_ZENITH_LOCATION,
						atmosphere->day_zenith.x,
						atmosphere->day_zenith.y,
						atmosphere->day_zenith.z);
	rft_shader_set_vec3(&engine->sky_shader,
						RFT_SKY_SHADER_DAY_HORIZON_LOCATION,
						atmosphere->day_horizon.x,
						atmosphere->day_horizon.y,
						atmosphere->day_horizon.z);
	rft_shader_set_vec3(&engine->sky_shader,
						RFT_SKY_SHADER_NIGHT_ZENITH_LOCATION,
						atmosphere->night_zenith.x,
						atmosphere->night_zenith.y,
						atmosphere->night_zenith.z);
	rft_shader_set_vec3(&engine->sky_shader,
						RFT_SKY_SHADER_NIGHT_HORIZON_LOCATION,
						atmosphere->night_horizon.x,
						atmosphere->night_horizon.y,
						atmosphere->night_horizon.z);
	rft_shader_set_vec4(&engine->sky_shader,
						RFT_SKY_SHADER_ATMOSPHERE_PARAMS_LOCATION,
						atmosphere->day_amount,
						atmosphere->twilight_amount,
						atmosphere->night_amount,
						atmosphere->stars_amount);

	glDrawArrays(GL_TRIANGLES, 0, 3);
	glDepthMask(GL_TRUE);
}

static void rft_post_targets_resize(rft_post_targets* targets, ivec2s size)
{
	ASSERT_FATAL(targets);
	ASSERT_FATAL(size.x > 0);
	ASSERT_FATAL(size.y > 0);

	if (targets->size.x == size.x && targets->size.y == size.y && targets->framebuffer)
	{
		return;
	}

	rft_post_targets_destroy(targets);

	glGenFramebuffers(1, &targets->framebuffer);
	ASSERT_FATAL(targets->framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, targets->framebuffer);

	glGenTextures(1, &targets->color_texture);
	ASSERT_FATAL(targets->color_texture);
	glBindTexture(GL_TEXTURE_2D, targets->color_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, size.x, size.y, 0, GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targets->color_texture, 0);

	glGenTextures(1, &targets->depth_texture);
	ASSERT_FATAL(targets->depth_texture);
	glBindTexture(GL_TEXTURE_2D, targets->depth_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, size.x, size.y, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, targets->depth_texture, 0);

	GLenum draw_buffers[] = { GL_COLOR_ATTACHMENT0 };
	glDrawBuffers(1, draw_buffers);

	ASSERT_FATAL(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	targets->size = size;
}

static void rft_post_targets_destroy(rft_post_targets* targets)
{
	ASSERT_FATAL(targets);

	if (targets->depth_texture)
	{
		glDeleteTextures(1, &targets->depth_texture);
		targets->depth_texture = 0;
	}

	if (targets->color_texture)
	{
		glDeleteTextures(1, &targets->color_texture);
		targets->color_texture = 0;
	}

	if (targets->framebuffer)
	{
		glDeleteFramebuffers(1, &targets->framebuffer);
		targets->framebuffer = 0;
	}

	targets->size = (ivec2s) { { 0, 0 } };
}

static void rft_engine_render_post(const rft_engine* engine)
{
	ASSERT_FATAL(engine);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, engine->post_targets.size.x, engine->post_targets.size.y);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	rft_shader_bind(&engine->post_shader);
	rft_vertex_array_bind(&engine->post_vao);

	glBindTextureUnit(0, engine->post_targets.color_texture);
	glBindTextureUnit(1, engine->post_targets.depth_texture);

	rft_shader_set_vec2(&engine->post_shader,
						RFT_POST_SHADER_INV_VIEWPORT_LOCATION,
						1.0f / (float)engine->post_targets.size.x,
						1.0f / (float)engine->post_targets.size.y);
	rft_shader_set_vec2(&engine->post_shader, RFT_POST_SHADER_DEPTH_PARAMS_LOCATION, engine->camera.cfg.near, engine->camera.cfg.far);
	rft_shader_set_vec4(&engine->post_shader,
						RFT_POST_SHADER_SPLIT_PARAMS_LOCATION,
						engine->camera.cfg.far * 0.94f,
						engine->camera.cfg.far * 0.992f,
						1.6f,
						0.0f);

	glDrawArrays(GL_TRIANGLES, 0, 3);

	glBindTextureUnit(0, 0);
	glBindTextureUnit(1, 0);
	glDepthMask(GL_TRUE);
}

static void rft_engine_init(rft_engine* engine)
{
	ASSERT_FATAL(engine);

	rft_set_cwd_to_executable();

	ASSERT_FATAL(glfwInit());
	rft_window_init(&engine->window, &rft_default_window_cfg);
	ASSERT_FATAL(gladLoaderLoadGL());

	glGenQueries((GLsizei)RFT_GPU_QUERY_RING_SIZE, engine->gpu_time_queries);

	glViewport(0, 0, engine->window.cfg.size.x, engine->window.cfg.size.y);
	glClearColor(0.76f, 0.76f, 0.79f, 1.0f);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	glClearDepth(1.0);

	glEnable(GL_MULTISAMPLE);
	glDisable(GL_CULL_FACE);

	glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);

	glfwSetInputMode(engine->window.handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	rft_input* input = &engine->window.input;
	rft_input_context_init(&engine->input_ctx);

	engine->player_map = rft_input_create_map(input, RFT_PLAYER_ACTION_COUNT);

	rft_player_default_bindings(engine->player_map);
	rft_input_context_push(&engine->input_ctx, engine->player_map);

	rft_camera_init(&engine->camera, &rft_default_camera_cfg);
	engine->default_camera_speed = rft_default_camera_cfg.speed;
	engine->time_of_day = 42.0f;

	rft_shader_init(&engine->chunk_shader, stages, 2);
	rft_shader_init(&engine->sky_shader, sky_stages, 2);
	rft_shader_init(&engine->post_shader, post_stages, 2);
	rft_vertex_array_init(&engine->chunk_vao);
	rft_vertex_array_init(&engine->post_vao);
	rft_post_targets_resize(&engine->post_targets, engine->window.cfg.size);

	rft_shader_bind(&engine->post_shader);
	rft_shader_set_int(&engine->post_shader, 3, 0);
	rft_shader_set_int(&engine->post_shader, 4, 1);

	load_block_assets(&engine->atlas);
	ASSERT_FATAL(engine->atlas.bindless_supported);
	GLint texture_location = rft_shader_get_uniform(&engine->chunk_shader, "u_textures");
	ASSERT_FATAL(texture_location >= 0);
	rft_texture_array_upload_bindless(&engine->atlas, engine->chunk_shader.program, texture_location);

	engine->streamer	  = rft_streamer_create(&rft_default_streamer_cfg);
	engine->debug_overlay = rft_debug_overlay_create();
	engine->running		  = true;
}

static void rft_engine_shutdown(rft_engine* engine)
{
	ASSERT_FATAL(engine);
	engine->running = false;

	rft_shader_destroy(&engine->chunk_shader);
	rft_shader_destroy(&engine->sky_shader);
	rft_shader_destroy(&engine->post_shader);
	rft_vertex_array_destroy(&engine->chunk_vao);
	rft_vertex_array_destroy(&engine->post_vao);
	rft_texture_array_destroy(&engine->atlas);
	rft_post_targets_destroy(&engine->post_targets);
	rft_input_destroy_map(engine->player_map);
	rft_streamer_destroy(engine->streamer);
	rft_debug_overlay_destroy(engine->debug_overlay);

	glDeleteQueries((GLsizei)RFT_GPU_QUERY_RING_SIZE, engine->gpu_time_queries);

	rft_window_destroy(&engine->window);
	glfwTerminate();
}

static void rft_engine_frame(rft_engine* engine, float dt)
{
	ASSERT_FATAL(engine);

	rft_input* input = &engine->window.input;

	rft_window_begin_frame(&engine->window);
	rft_window_poll_events();
	glfwGetFramebufferSize(engine->window.handle, &engine->window.cfg.size.x, &engine->window.cfg.size.y);

	rft_input_update_gamepads(input);
	rft_input_context_resolve(&engine->input_ctx, input);
	rft_engine_handle_speed_input(engine, input);
	rft_camera_update(&engine->camera, engine->player_map, input, dt);
	engine->time_of_day += dt;

	rft_streamer_update(engine->streamer, &engine->camera);

	rft_post_targets_resize(&engine->post_targets, engine->window.cfg.size);

	glBindFramebuffer(GL_FRAMEBUFFER, engine->post_targets.framebuffer);
	glViewport(0, 0, engine->window.cfg.size.x, engine->window.cfg.size.y);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LESS);
	glDisable(GL_CULL_FACE);

	uint32_t current_query = engine->gpu_query_index;
	glBeginQuery(GL_TIME_ELAPSED, engine->gpu_time_queries[current_query]);

	rft_shader_bind(&engine->chunk_shader);
	rft_vertex_array_bind(&engine->chunk_vao);

	mat4s view = rft_camera_view(&engine->camera);
	mat4s proj = rft_camera_proj(&engine->camera, (float)engine->window.cfg.size.x / (float)engine->window.cfg.size.y);
	mat4s vp   = glms_mat4_mul(proj, view);
	mat4s inv_view_proj = glms_mat4_inv(vp);
	rft_atmosphere atmosphere = rft_engine_sample_atmosphere(engine->time_of_day);

	glClear(GL_DEPTH_BUFFER_BIT);
	rft_engine_render_sky(engine, &atmosphere, &inv_view_proj);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LESS);

	vec4s frustum[6];
	glms_frustum_planes(vp, frustum);

	float cam_x = floorf(engine->camera.cfg.pos.x / 64.0f);
	float cam_y = floorf(engine->camera.cfg.pos.y / 64.0f);
	float cam_z = floorf(engine->camera.cfg.pos.z / 64.0f);

	ivec3s cam_chunk = {
		{ (int)cam_x, (int)cam_y, (int)cam_z },
	};

	vec3s cam_offset = {
		{ engine->camera.cfg.pos.x - cam_x * 64.0f, engine->camera.cfg.pos.y - cam_y * 64.0f, engine->camera.cfg.pos.z - cam_z * 64.0f },
	};

	rft_shader_set_vec3i(&engine->chunk_shader, 3, cam_chunk.x, cam_chunk.y, cam_chunk.z);
	rft_shader_set_vec3(&engine->chunk_shader, 4, cam_offset.x, cam_offset.y, cam_offset.z);
	rft_shader_set_vec4(&engine->chunk_shader,
						RFT_CHUNK_SHADER_FOG_DISTANCE_LOCATION,
						rft_default_streamer_cfg.render_distance * 0.30f,
						rft_default_streamer_cfg.render_distance * 0.78f,
						1.12f,
						0.0f);
	rft_shader_set_vec3(&engine->chunk_shader,
						RFT_CHUNK_SHADER_FOG_COLOR_NEAR_LOCATION,
						atmosphere.fog_near.x,
						atmosphere.fog_near.y,
						atmosphere.fog_near.z);
	rft_shader_set_vec3(&engine->chunk_shader,
						RFT_CHUNK_SHADER_FOG_COLOR_FAR_LOCATION,
						atmosphere.fog_far.x,
						atmosphere.fog_far.y,
						atmosphere.fog_far.z);
	rft_shader_set_vec4(&engine->chunk_shader, RFT_CHUNK_SHADER_FOG_FRINGE_LOCATION, 0.62f, 1.20f, 1.10f, 0.34f);
	rft_shader_set_vec3(&engine->chunk_shader,
						RFT_CHUNK_SHADER_SUN_DIR_LOCATION,
						atmosphere.sun_dir.x,
						atmosphere.sun_dir.y,
						atmosphere.sun_dir.z);
	rft_shader_set_vec3(&engine->chunk_shader,
						RFT_CHUNK_SHADER_SUN_COLOR_LOCATION,
						atmosphere.sun_color.x,
						atmosphere.sun_color.y,
						atmosphere.sun_color.z);
	rft_shader_set_vec3(&engine->chunk_shader,
						RFT_CHUNK_SHADER_AMBIENT_COLOR_LOCATION,
						atmosphere.ambient_color.x,
						atmosphere.ambient_color.y,
						atmosphere.ambient_color.z);
	rft_shader_set_vec3(&engine->chunk_shader,
						RFT_CHUNK_SHADER_MOON_DIR_LOCATION,
						atmosphere.moon_dir.x,
						atmosphere.moon_dir.y,
						atmosphere.moon_dir.z);
	rft_shader_set_vec3(&engine->chunk_shader,
						RFT_CHUNK_SHADER_MOON_COLOR_LOCATION,
						atmosphere.moon_color.x,
						atmosphere.moon_color.y,
						atmosphere.moon_color.z);

	mat4s view_rte = view;

	view_rte.raw[3][0] = 0.0f;
	view_rte.raw[3][1] = 0.0f;
	view_rte.raw[3][2] = 0.0f;

	mat4s mvp_rte = glms_mat4_mul(proj, view_rte);

	rft_shader_set_mat4(&engine->chunk_shader, 0, &mvp_rte.raw[0][0]);
	rft_streamer_render(engine->streamer, &engine->camera, frustum, engine->chunk_shader.program);

	glEndQuery(GL_TIME_ELAPSED);

	uint32_t read_query	 = (current_query + 1U) % RFT_GPU_QUERY_RING_SIZE;
	GLint	 query_ready = 0;

	glGetQueryObjectiv(engine->gpu_time_queries[read_query], GL_QUERY_RESULT_AVAILABLE, &query_ready);

	if (query_ready == GL_TRUE)
	{
		GLuint64 gpu_ns = 0;

		glGetQueryObjectui64v(engine->gpu_time_queries[read_query], GL_QUERY_RESULT, &gpu_ns);

		engine->last_gpu_time_ms = (float)gpu_ns * 1e-6f;
		engine->gpu_time_valid	 = true;
	}

	engine->gpu_query_index = (current_query + 1U) % RFT_GPU_QUERY_RING_SIZE;

	rft_streamer_collect_stats(engine->streamer, &engine->streamer_stats);

	rft_debug_overlay_update(engine->debug_overlay,
							 dt,
							 engine->last_gpu_time_ms,
							 engine->gpu_time_valid ? 1 : 0,
							 &engine->streamer_stats,
							 engine->camera.cfg.speed,
							 engine->default_camera_speed);

	rft_engine_render_post(engine);
	rft_debug_overlay_render(engine->debug_overlay);

	rft_window_swap_buffers(&engine->window);
}

static void rft_engine_handle_speed_input(rft_engine* engine, const rft_input* input)
{
	ASSERT_FATAL(engine);
	ASSERT_FATAL(input);

	if (rft_input_pressed(input, RFT_INPUT_KEY, GLFW_KEY_MINUS))
	{
		engine->camera.cfg.speed = fmaxf(RFT_CAMERA_SPEED_MIN, engine->camera.cfg.speed - RFT_CAMERA_SPEED_STEP);
	}

	if (rft_input_pressed(input, RFT_INPUT_KEY, GLFW_KEY_EQUAL))
	{
		engine->camera.cfg.speed += RFT_CAMERA_SPEED_STEP;
	}

	if (rft_input_pressed(input, RFT_INPUT_KEY, GLFW_KEY_0))
	{
		engine->camera.cfg.speed = engine->default_camera_speed;
	}
}
