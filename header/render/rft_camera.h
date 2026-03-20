#pragma once

#include <cglm/struct/mat4.h>
#include <cglm/struct/vec2.h>
#include <cglm/struct/vec3.h>

typedef struct rft_input	  rft_input;
typedef struct rft_action_map rft_action_map;

typedef struct rft_camera_config
{
	vec3s pos;

	float pitch;
	float yaw;

	float speed;
	float sens;

	float fov;
	float near;
	float far;
} rft_camera_config;

typedef struct rft_camera
{
	rft_camera_config cfg;
} rft_camera;

void rft_camera_init(rft_camera* camera, const rft_camera_config* config);
void rft_camera_update(rft_camera* camera, const rft_action_map* map, const rft_input* input, float dt);

mat4s rft_camera_view(const rft_camera* cam);
mat4s rft_camera_proj(const rft_camera* cam, float aspect_ratio);
