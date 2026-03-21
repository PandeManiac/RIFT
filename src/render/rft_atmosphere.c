#include "render/rft_atmosphere.h"

#include <cglm/cglm.h>
#include <cglm/struct/vec3.h>

#include <math.h>

static float rft_smoothstep(float a, float b, float x)
{
	float t = (x - a) / (b - a);
	t		= fminf(fmaxf(t, 0.0f), 1.0f);
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

rft_atmosphere rft_atmosphere_sample(float time_of_day, float day_cycle_seconds)
{
	float phase		  = fmodf(time_of_day / day_cycle_seconds, 1.0f);
	float angle		  = phase * glm_rad(360.0f);
	float orbit_tilt  = glm_rad(23.5f);
	vec3s orbit_right = { { cosf(orbit_tilt), 0.0f, -sinf(orbit_tilt) } };
	vec3s orbit_up	  = { { 0.0f, 1.0f, 0.0f } };
	vec3s sun_dir	  = glms_vec3_normalize(rft_vec3_add(rft_vec3_scale(orbit_right, cosf(angle)), rft_vec3_scale(orbit_up, sinf(angle))));

	vec3s moon_dir = glms_vec3_negate(sun_dir);

	float day_amount	  = rft_smoothstep(-0.10f, 0.24f, sun_dir.y);
	float night_amount	  = 1.0f - rft_smoothstep(-0.26f, 0.02f, sun_dir.y);
	float twilight_amount = fmaxf(0.0f, 1.0f - day_amount - night_amount);
	float stars_amount	  = night_amount * night_amount;

	vec3s day_zenith	= { { 0.22f, 0.53f, 0.92f } };
	vec3s day_horizon	= { { 0.78f, 0.88f, 1.00f } };
	vec3s sunset_glow	= { { 1.00f, 0.50f, 0.18f } };
	vec3s night_zenith	= { { 0.02f, 0.04f, 0.10f } };
	vec3s night_horizon = { { 0.05f, 0.08f, 0.16f } };

	vec3s sun_color		= rft_vec3_mix((vec3s) { { 1.00f, 0.58f, 0.30f } }, (vec3s) { { 1.00f, 0.97f, 0.90f } }, day_amount);
	vec3s moon_color	= { { 0.35f, 0.44f, 0.60f } };
	vec3s ambient_color = rft_vec3_add(rft_vec3_scale(day_horizon, 0.18f + day_amount * 0.20f), rft_vec3_scale(night_horizon, 0.10f + stars_amount * 0.12f));
	vec3s fog_near		= rft_vec3_mix(rft_vec3_mix(night_horizon, day_horizon, day_amount), sunset_glow, twilight_amount * 0.35f);
	vec3s fog_far		= rft_vec3_mix(rft_vec3_mix(night_zenith, day_zenith, day_amount), sunset_glow, twilight_amount * 0.55f);

	return (rft_atmosphere) {
		.sun_dir		 = sun_dir,
		.moon_dir		 = moon_dir,
		.sun_color		 = sun_color,
		.moon_color		 = moon_color,
		.ambient_color	 = ambient_color,
		.fog_near		 = fog_near,
		.fog_far		 = fog_far,
		.day_zenith		 = day_zenith,
		.day_horizon	 = day_horizon,
		.night_zenith	 = night_zenith,
		.night_horizon	 = night_horizon,
		.day_amount		 = day_amount,
		.twilight_amount = twilight_amount,
		.night_amount	 = night_amount,
		.stars_amount	 = stars_amount,
	};
}
