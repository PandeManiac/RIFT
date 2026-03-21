#pragma once

#include <cglm/struct/vec3.h>

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

rft_atmosphere rft_atmosphere_sample(float time_of_day, float day_cycle_seconds);
