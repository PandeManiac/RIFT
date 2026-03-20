#include "render/rft_camera.h"

#include "window/input/rft_input.h"
#include "window/input/rft_input_map.h"
#include "window/input/rft_player_actions.h"

#include "utils/rft_assert.h"

#include <cglm/struct/cam.h>
#include <cglm/struct/clipspace/persp_rh_zo.h>
#include <cglm/struct/vec2.h>
#include <cglm/struct/vec3.h>

#include <math.h>
#include <string.h>

static HOT inline ALWAYS_INLINE vec3s camera_front(const rft_camera* cam)
{
	return glms_vec3_normalize((vec3s) {
		.x = cosf(glm_rad(cam->cfg.yaw)) * cosf(glm_rad(cam->cfg.pitch)),
		.y = sinf(glm_rad(cam->cfg.pitch)),
		.z = sinf(glm_rad(cam->cfg.yaw)) * cosf(glm_rad(cam->cfg.pitch)),
	});
}

static HOT inline ALWAYS_INLINE vec2s apply_radial_deadzone(vec2s v, float deadzone)
{
	const float len = glms_vec2_norm(v);

	if (len <= deadzone)
	{
		return (vec2s) { { 0.0f, 0.0f } };
	}

	const float scaled = (len - deadzone) / (1.0f - deadzone);
	return glms_vec2_scale(glms_vec2_normalize(v), scaled);
}

void rft_camera_init(rft_camera* camera, const rft_camera_config* config)
{
	ASSERT_FATAL(camera);
	ASSERT_FATAL(config);

	memset(camera, 0, sizeof(*camera));

	camera->cfg = *config;
}

void rft_camera_update(rft_camera* camera, const rft_action_map* map, const rft_input* input, float dt)
{
	ASSERT_FATAL(camera);
	ASSERT_FATAL(map);
	ASSERT_FATAL(input);
	ASSERT_FATAL(isfinite(dt));
	ASSERT_FATAL(dt >= 0.0f);

	const float vel = camera->cfg.speed * dt;

	const float forward = rft_action_value(map, RFT_PLAYER_MOVE_FORWARD) - rft_action_value(map, RFT_PLAYER_MOVE_BACK);
	const float right	= rft_action_value(map, RFT_PLAYER_MOVE_RIGHT) - rft_action_value(map, RFT_PLAYER_MOVE_LEFT);
	const float up		= rft_action_value(map, RFT_PLAYER_MOVE_UP) - rft_action_value(map, RFT_PLAYER_MOVE_DOWN);

	vec3s front	   = camera_front(camera);
	vec3s world_up = { { 0.0f, 1.0f, 0.0f } };
	vec3s right_v  = glms_vec3_normalize(glms_vec3_cross(front, world_up));

	vec3s move = glms_vec3_zero();

	move = glms_vec3_add(move, glms_vec3_scale(front, forward));
	move = glms_vec3_add(move, glms_vec3_scale(right_v, right));

	move.y += up;
	camera->cfg.pos = glms_vec3_add(camera->cfg.pos, glms_vec3_scale(move, vel));

	vec2s look = { .x = rft_action_value(map, RFT_PLAYER_LOOK_X), .y = rft_action_value(map, RFT_PLAYER_LOOK_Y) };
	look	   = apply_radial_deadzone(look, 0.15f);

	camera->cfg.yaw += look.x * 180.0f * dt;
	camera->cfg.pitch += look.y * 180.0f * dt;

	vec2s md = rft_mouse_delta(input);

	camera->cfg.yaw += md.x * camera->cfg.sens;
	camera->cfg.pitch -= md.y * camera->cfg.sens;

	camera->cfg.pitch = fminf(89.0f, fmaxf(-89.0f, camera->cfg.pitch));
}

mat4s rft_camera_view(const rft_camera* cam)
{
	ASSERT_FATAL(cam);
	vec3s front = camera_front(cam);
	return glms_lookat(cam->cfg.pos, glms_vec3_add(cam->cfg.pos, front), (vec3s) { { 0.0f, 1.0f, 0.0f } });
}

mat4s rft_camera_proj(const rft_camera* cam, float aspect_ratio)
{
	ASSERT_FATAL(cam);
	ASSERT_FATAL(isfinite(aspect_ratio));
	ASSERT_FATAL(aspect_ratio > 0.0f);

	return glms_perspective_rh_zo(glm_rad(cam->cfg.fov), aspect_ratio, cam->cfg.near, cam->cfg.far);
}
