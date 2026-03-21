#version 460 core

layout(location = 0) uniform mat4 u_inv_view_proj;
layout(location = 4) uniform vec3 u_camera_pos;
layout(location = 5) uniform vec3 u_sun_dir;
layout(location = 6) uniform vec3 u_moon_dir;
layout(location = 7) uniform vec3 u_day_zenith;
layout(location = 8) uniform vec3 u_day_horizon;
layout(location = 9) uniform vec3 u_night_zenith;
layout(location = 10) uniform vec3 u_night_horizon;
layout(location = 11) uniform vec4 u_atmosphere_params;

in vec2 v_uv;

out vec4 o_color;

float saturate(float x)
{
    return clamp(x, 0.0, 1.0);
}

float hash13(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 dir_to_octa_uv(vec3 dir)
{
    dir /= abs(dir.x) + abs(dir.y) + abs(dir.z);
    vec2 uv = dir.xz;

    if (dir.y < 0.0)
    {
        uv = (1.0 - abs(uv.yx)) * sign(uv.xy + vec2(1e-6));
    }

    return uv * 0.5 + 0.5;
}

void main()
{
    vec2 ndc = v_uv * 2.0 - 1.0;
    vec4 world = u_inv_view_proj * vec4(ndc, 1.0, 1.0);
    vec3 view_dir = normalize(world.xyz / world.w - u_camera_pos);

    float up = saturate(view_dir.y * 0.5 + 0.5);
    float horizon = exp(-abs(view_dir.y) * 9.0);
    float zenith = pow(up, 0.65);

    vec3 day_sky = mix(u_day_horizon, u_day_zenith, pow(up, 0.55));
    vec3 night_sky = mix(u_night_horizon, u_night_zenith, pow(up, 0.65));
    vec3 sky = mix(night_sky, day_sky, u_atmosphere_params.x);

    float haze = horizon * horizon * (0.025 + u_atmosphere_params.y * 0.06);
    sky += vec3(1.00, 0.70, 0.44) * haze;

    float sun_align = max(dot(view_dir, normalize(u_sun_dir)), 0.0);
    float moon_align = max(dot(view_dir, normalize(u_moon_dir)), 0.0);

    vec3 view_h = normalize(vec3(view_dir.x, 0.0001, view_dir.z));
    vec3 sun_h = normalize(vec3(u_sun_dir.x, 0.0001, u_sun_dir.z));
    float sunset_band = horizon * pow(max(dot(view_h, sun_h), 0.0), 5.0);
    sunset_band *= u_atmosphere_params.y;
    sky += vec3(1.00, 0.42, 0.14) * sunset_band * 0.95;

    float sun_disk = pow(sun_align, 1200.0);
    float sun_glow = pow(sun_align, 32.0) * 0.35;
    sky += vec3(1.00, 0.92, 0.78) * (sun_disk * 8.0 + sun_glow);

    float moon_disk = pow(moon_align, 1800.0);
    float moon_glow = pow(moon_align, 48.0) * 0.18;
    sky += vec3(0.62, 0.72, 1.00) * (moon_disk * 2.6 + moon_glow);

    vec2 star_uv = dir_to_octa_uv(view_dir);
    vec2 star_grid_a = floor(star_uv * vec2(1200.0, 1200.0));
    vec2 star_grid_b = floor(star_uv * vec2(820.0, 820.0) + vec2(71.0, 29.0));
    vec2 star_grid_c = floor(star_uv * vec2(560.0, 560.0) + vec2(191.0, 83.0));
    float star_seed_a = hash13(vec3(star_grid_a, 17.0));
    float star_seed_b = hash13(vec3(star_grid_b, 31.0));
    float star_seed_c = hash13(vec3(star_grid_c, 47.0));
    float star_twinkle_a = hash13(vec3(star_grid_a, 53.0));
    float star_twinkle_b = hash13(vec3(star_grid_b, 79.0));
    float star_twinkle_c = hash13(vec3(star_grid_c, 97.0));
    float stars_a = step(0.9976, star_seed_a) * mix(0.55, 1.0, star_twinkle_a);
    float stars_b = step(0.9984, star_seed_b) * mix(0.45, 0.9, star_twinkle_b);
    float stars_c = step(0.9990, star_seed_c) * mix(0.35, 0.8, star_twinkle_c);
    float star_field = (stars_a + stars_b * 0.90 + stars_c * 0.75) * u_atmosphere_params.w * (0.30 + zenith * 0.90);

    vec3 star_color = mix(vec3(0.80, 0.87, 1.0), vec3(1.0, 0.92, 0.84), hash12(star_grid_a + 7.0));
    sky += star_color * star_field;

    o_color = vec4(clamp(sky, 0.0, 1.0), 1.0);
}
