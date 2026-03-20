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
#define RFT_CHUNK_SHADER_FOG_ABERRATION_LOCATION 8

static const vec3s RFT_ATMOSPHERE_FOG_COLOR_NEAR = { { 0.62f, 0.73f, 0.78f } };
static const vec3s RFT_ATMOSPHERE_FOG_COLOR_FAR  = { { 0.94f, 0.72f, 0.59f } };

struct rft_engine
{
	rft_window window;
	rft_camera camera;
	float	   default_camera_speed;

	rft_input_context input_ctx;
	rft_action_map*	  player_map;

	rft_shader		  chunk_shader;
	rft_vertex_array  chunk_vao;
	rft_texture_array atlas;

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

static void rft_engine_init(rft_engine* engine);
static void rft_engine_shutdown(rft_engine* engine);
static void rft_engine_frame(rft_engine* engine, float dt);
static void rft_engine_handle_speed_input(rft_engine* engine, const rft_input* input);

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

	rft_shader_init(&engine->chunk_shader, stages, 2);
	rft_vertex_array_init(&engine->chunk_vao);

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
	rft_vertex_array_destroy(&engine->chunk_vao);
	rft_texture_array_destroy(&engine->atlas);
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

	rft_input_update_gamepads(input);
	rft_input_context_resolve(&engine->input_ctx, input);
	rft_engine_handle_speed_input(engine, input);
	rft_camera_update(&engine->camera, engine->player_map, input, dt);

	rft_streamer_update(engine->streamer, &engine->camera);

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
						rft_default_streamer_cfg.render_distance * 0.42f,
						rft_default_streamer_cfg.render_distance * 0.92f,
						1.65f,
						0.0f);
	rft_shader_set_vec3(&engine->chunk_shader,
						RFT_CHUNK_SHADER_FOG_COLOR_NEAR_LOCATION,
						RFT_ATMOSPHERE_FOG_COLOR_NEAR.x,
						RFT_ATMOSPHERE_FOG_COLOR_NEAR.y,
						RFT_ATMOSPHERE_FOG_COLOR_NEAR.z);
	rft_shader_set_vec3(&engine->chunk_shader,
						RFT_CHUNK_SHADER_FOG_COLOR_FAR_LOCATION,
						RFT_ATMOSPHERE_FOG_COLOR_FAR.x,
						RFT_ATMOSPHERE_FOG_COLOR_FAR.y,
						RFT_ATMOSPHERE_FOG_COLOR_FAR.z);
	rft_shader_set_vec4(&engine->chunk_shader, RFT_CHUNK_SHADER_FOG_ABERRATION_LOCATION, 0.28f, 1.9f, 0.65f, 0.0f);

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
