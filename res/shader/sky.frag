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

void main()
{
    vec2 ndc = v_uv * 2.0 - 1.0;
    vec4 world = u_inv_view_proj * vec4(ndc, 1.0, 1.0);
    vec3 view_dir = normalize(world.xyz / world.w - u_camera_pos);

    float up = saturate(view_dir.y * 0.5 + 0.5);
    float horizon = exp(-abs(view_dir.y) * 9.0);

    vec3 day_sky = mix(u_day_horizon, u_day_zenith, pow(up, 0.55));
    vec3 night_sky = mix(u_night_horizon, u_night_zenith, pow(up, 0.65));
    vec3 sky = mix(night_sky, day_sky, u_atmosphere_params.x);

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

    float stars = hash13(normalize(view_dir) * 512.0);
    stars = step(0.9975, stars) * u_atmosphere_params.w;
    sky += vec3(stars);

    o_color = vec4(clamp(sky, 0.0, 1.0), 1.0);
}
