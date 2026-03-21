#include "engine/rft_engine.h"
#include "engine/rft_engine_configs.h"

#include "render/rft_atmosphere.h"
#include "render/rft_camera.h"
#include "render/rft_vertex_array.h"
#include "render/shader/rft_shader.h"
#include "render/texture/rft_texture_array.h"

#include "ui/rft_debug_overlay.h"
#include "world/rft_streamer.h"

#include "window/input/rft_input.h"
#include "window/input/rft_input_context.h"
#include "window/input/rft_input_map.h"
#include "window/input/rft_player_actions.h"
#include "window/rft_window.h"

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
#include <stdlib.h>
#include <string.h>

#define RFT_GPU_QUERY_RING_SIZE 3U
#define RFT_DAY_CYCLE_SECONDS 240.0f
#define RFT_FLOATING_ORIGIN_REBASE_RADIUS_CHUNKS 64
#define RFT_CAMERA_SPEED_STEP 25.0f
#define RFT_CAMERA_SPEED_MIN 25.0f
#define RFT_TIME_SPEED_STEP 0.25f
#define RFT_TIME_SPEED_MIN 0.0f
#define RFT_TIME_NUDGE_SECONDS (RFT_DAY_CYCLE_SECONDS / 24.0f)
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
	ivec3s	   world_origin_chunk;
	float	   default_camera_speed;
	float	   time_of_day;
	float	   time_speed;
	bool	   time_locked;

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

static void load_block_assets(rft_texture_array* atlas);
static void rft_engine_init(rft_engine* engine);
static void rft_engine_shutdown(rft_engine* engine);
static void rft_engine_frame(rft_engine* engine, float dt);
static void rft_engine_handle_speed_input(rft_engine* engine, const rft_input* input);
static void rft_engine_handle_time_input(rft_engine* engine, const rft_input* input);
static void rft_engine_apply_floating_origin(rft_engine* engine);
static void rft_post_targets_resize(rft_post_targets* targets, ivec2s size);
static void rft_post_targets_destroy(rft_post_targets* targets);
static void rft_engine_render_post(const rft_engine* engine);
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

	rft_texture_array_init(atlas, tile_size, tile_size, layer_count, bin_data, 1, 1);

	free(bin_data);
	json_object_put(root);
	free(json_buffer);
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
	rft_shader_set_vec3(&engine->sky_shader, RFT_SKY_SHADER_CAMERA_POS_LOCATION, 0.0f, 0.0f, 0.0f);
	rft_shader_set_vec3(&engine->sky_shader, RFT_SKY_SHADER_SUN_DIR_LOCATION, atmosphere->sun_dir.x, atmosphere->sun_dir.y, atmosphere->sun_dir.z);
	rft_shader_set_vec3(&engine->sky_shader, RFT_SKY_SHADER_MOON_DIR_LOCATION, atmosphere->moon_dir.x, atmosphere->moon_dir.y, atmosphere->moon_dir.z);
	rft_shader_set_vec3(&engine->sky_shader, RFT_SKY_SHADER_DAY_ZENITH_LOCATION, atmosphere->day_zenith.x, atmosphere->day_zenith.y, atmosphere->day_zenith.z);
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
	engine->world_origin_chunk	 = (ivec3s) { { 0, 0, 0 } };
	engine->default_camera_speed = rft_default_camera_cfg.speed;
	engine->time_of_day			 = 42.0f;
	engine->time_speed			 = 1.0f;
	engine->time_locked			 = false;

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
	rft_engine_handle_time_input(engine, input);
	rft_camera_update(&engine->camera, engine->player_map, input, dt);
	rft_engine_apply_floating_origin(engine);

	if (!engine->time_locked)
	{
		engine->time_of_day += dt * engine->time_speed;
	}

	rft_streamer_update(engine->streamer, &engine->camera, engine->world_origin_chunk);
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

	mat4s		   view		  = rft_camera_view(&engine->camera);
	mat4s		   proj		  = rft_camera_proj(&engine->camera, (float)engine->window.cfg.size.x / (float)engine->window.cfg.size.y);
	rft_atmosphere atmosphere = rft_atmosphere_sample(engine->time_of_day, RFT_DAY_CYCLE_SECONDS);

	float cam_x = floorf(engine->camera.cfg.pos.x / 64.0f);
	float cam_y = floorf(engine->camera.cfg.pos.y / 64.0f);
	float cam_z = floorf(engine->camera.cfg.pos.z / 64.0f);

	ivec3s cam_chunk = {
		{ engine->world_origin_chunk.x + (int)cam_x, engine->world_origin_chunk.y + (int)cam_y, engine->world_origin_chunk.z + (int)cam_z },
	};

	vec3s cam_offset = {
		{ engine->camera.cfg.pos.x - cam_x * 64.0f, engine->camera.cfg.pos.y - cam_y * 64.0f, engine->camera.cfg.pos.z - cam_z * 64.0f },
	};

	mat4s view_rte = view;

	view_rte.raw[3][0] = 0.0f;
	view_rte.raw[3][1] = 0.0f;
	view_rte.raw[3][2] = 0.0f;

	mat4s mvp_rte		= glms_mat4_mul(proj, view_rte);
	mat4s inv_view_proj = glms_mat4_inv(mvp_rte);

	glClear(GL_DEPTH_BUFFER_BIT);
	rft_engine_render_sky(engine, &atmosphere, &inv_view_proj);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LESS);
	rft_shader_bind(&engine->chunk_shader);
	rft_vertex_array_bind(&engine->chunk_vao);

	vec4s frustum[6];
	glms_frustum_planes(mvp_rte, frustum);

	rft_shader_set_vec3i(&engine->chunk_shader, 3, cam_chunk.x, cam_chunk.y, cam_chunk.z);
	rft_shader_set_vec3(&engine->chunk_shader, 4, cam_offset.x, cam_offset.y, cam_offset.z);
	rft_shader_set_vec4(&engine->chunk_shader,
						RFT_CHUNK_SHADER_FOG_DISTANCE_LOCATION,
						rft_default_streamer_cfg.render_distance * 0.30f,
						rft_default_streamer_cfg.render_distance * 0.78f,
						1.12f,
						0.0f);
	rft_shader_set_vec3(&engine->chunk_shader, RFT_CHUNK_SHADER_FOG_COLOR_NEAR_LOCATION, atmosphere.fog_near.x, atmosphere.fog_near.y, atmosphere.fog_near.z);
	rft_shader_set_vec3(&engine->chunk_shader, RFT_CHUNK_SHADER_FOG_COLOR_FAR_LOCATION, atmosphere.fog_far.x, atmosphere.fog_far.y, atmosphere.fog_far.z);
	rft_shader_set_vec4(&engine->chunk_shader, RFT_CHUNK_SHADER_FOG_FRINGE_LOCATION, 0.62f, 1.20f, 1.10f, 0.34f);
	rft_shader_set_vec3(&engine->chunk_shader, RFT_CHUNK_SHADER_SUN_DIR_LOCATION, atmosphere.sun_dir.x, atmosphere.sun_dir.y, atmosphere.sun_dir.z);
	rft_shader_set_vec3(&engine->chunk_shader, RFT_CHUNK_SHADER_SUN_COLOR_LOCATION, atmosphere.sun_color.x, atmosphere.sun_color.y, atmosphere.sun_color.z);
	rft_shader_set_vec3(&engine->chunk_shader,
						RFT_CHUNK_SHADER_AMBIENT_COLOR_LOCATION,
						atmosphere.ambient_color.x,
						atmosphere.ambient_color.y,
						atmosphere.ambient_color.z);
	rft_shader_set_vec3(&engine->chunk_shader, RFT_CHUNK_SHADER_MOON_DIR_LOCATION, atmosphere.moon_dir.x, atmosphere.moon_dir.y, atmosphere.moon_dir.z);
	rft_shader_set_vec3(&engine->chunk_shader, RFT_CHUNK_SHADER_MOON_COLOR_LOCATION, atmosphere.moon_color.x, atmosphere.moon_color.y, atmosphere.moon_color.z);

	rft_shader_set_mat4(&engine->chunk_shader, 0, &mvp_rte.raw[0][0]);
	rft_streamer_render(engine->streamer, cam_chunk, cam_offset, frustum, engine->chunk_shader.program);

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
							 engine->default_camera_speed,
							 engine->time_of_day,
							 engine->time_speed,
							 engine->time_locked ? 1 : 0);

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

static void rft_engine_handle_time_input(rft_engine* engine, const rft_input* input)
{
	ASSERT_FATAL(engine);
	ASSERT_FATAL(input);

	if (rft_input_pressed(input, RFT_INPUT_KEY, GLFW_KEY_LEFT_BRACKET))
	{
		engine->time_speed = fmaxf(RFT_TIME_SPEED_MIN, engine->time_speed - RFT_TIME_SPEED_STEP);
	}

	if (rft_input_pressed(input, RFT_INPUT_KEY, GLFW_KEY_RIGHT_BRACKET))
	{
		engine->time_speed += RFT_TIME_SPEED_STEP;
	}

	if (rft_input_pressed(input, RFT_INPUT_KEY, GLFW_KEY_BACKSLASH))
	{
		engine->time_locked = !engine->time_locked;
	}

	if (rft_input_pressed(input, RFT_INPUT_KEY, GLFW_KEY_COMMA))
	{
		engine->time_of_day -= RFT_TIME_NUDGE_SECONDS;
	}

	if (rft_input_pressed(input, RFT_INPUT_KEY, GLFW_KEY_PERIOD))
	{
		engine->time_of_day += RFT_TIME_NUDGE_SECONDS;
	}

	engine->time_of_day = fmodf(engine->time_of_day, RFT_DAY_CYCLE_SECONDS);

	if (engine->time_of_day < 0.0f)
	{
		engine->time_of_day += RFT_DAY_CYCLE_SECONDS;
	}
}

static void rft_engine_apply_floating_origin(rft_engine* engine)
{
	ASSERT_FATAL(engine);

	int chunk_x = (int)(engine->camera.cfg.pos.x / 64.0f);
	int chunk_y = (int)(engine->camera.cfg.pos.y / 64.0f);
	int chunk_z = (int)(engine->camera.cfg.pos.z / 64.0f);

	if (engine->camera.cfg.pos.x < 0.0f && fabsf(fmodf(engine->camera.cfg.pos.x, 64.0f)) > 1e-6f)
	{
		chunk_x--;
	}

	if (engine->camera.cfg.pos.y < 0.0f && fabsf(fmodf(engine->camera.cfg.pos.y, 64.0f)) > 1e-6f)
	{
		chunk_y--;
	}

	if (engine->camera.cfg.pos.z < 0.0f && fabsf(fmodf(engine->camera.cfg.pos.z, 64.0f)) > 1e-6f)
	{
		chunk_z--;
	}

	if (abs(chunk_x) < RFT_FLOATING_ORIGIN_REBASE_RADIUS_CHUNKS && abs(chunk_y) < RFT_FLOATING_ORIGIN_REBASE_RADIUS_CHUNKS &&
		abs(chunk_z) < RFT_FLOATING_ORIGIN_REBASE_RADIUS_CHUNKS)
	{
		return;
	}

	engine->world_origin_chunk.x += chunk_x;
	engine->world_origin_chunk.y += chunk_y;
	engine->world_origin_chunk.z += chunk_z;

	engine->camera.cfg.pos.x -= (float)(chunk_x * 64);
	engine->camera.cfg.pos.y -= (float)(chunk_y * 64);
	engine->camera.cfg.pos.z -= (float)(chunk_z * 64);
}
