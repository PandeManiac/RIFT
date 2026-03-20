#version 460 core
#extension GL_ARB_bindless_texture : require

layout(bindless_sampler) uniform sampler2DArray u_textures;
layout(location = 5) uniform vec4 u_fog_distance_params;
layout(location = 6) uniform vec3 u_fog_color_near;
layout(location = 7) uniform vec3 u_fog_color_far;
layout(location = 8) uniform vec4 u_fog_fringe_params;
layout(location = 9) uniform vec3 u_sun_dir;
layout(location = 10) uniform vec3 u_sun_color;
layout(location = 11) uniform vec3 u_ambient_color;
layout(location = 12) uniform vec3 u_moon_dir;
layout(location = 13) uniform vec3 u_moon_color;

flat in uint  v_material;
flat in vec3  v_normal;
in vec2       v_uv;
in vec3       v_view_pos;

out vec4 o_color;

float saturate(float x)
{
    return clamp(x, 0.0, 1.0);
}

void main()
{
    vec4 tex = texture(u_textures, vec3(v_uv, float(v_material)));
    vec3 normal = normalize(v_normal);

    float sun_diffuse = max(dot(normal, normalize(u_sun_dir)), 0.0);
    float moon_diffuse = max(dot(normal, normalize(u_moon_dir)), 0.0);
    float upward = normal.y * 0.5 + 0.5;

    vec3 lighting = u_ambient_color;
    lighting += u_sun_color * (sun_diffuse * 0.95 + upward * 0.08);
    lighting += u_moon_color * (moon_diffuse * 0.30);

    vec3 lit_color = tex.rgb * lighting;

    float view_distance = length(v_view_pos);
    float fog_range = max(u_fog_distance_params.y - u_fog_distance_params.x, 1.0);
    float fog_t = saturate((view_distance - u_fog_distance_params.x) / fog_range);
    fog_t = pow(fog_t, u_fog_distance_params.z);

    vec3 view_dir = view_distance > 0.0 ? v_view_pos / view_distance : vec3(0.0, 0.0, 1.0);
    float horizon = pow(saturate(1.0 - abs(view_dir.y)), 1.25);
    vec3 fog_color = mix(u_fog_color_near, u_fog_color_far, horizon);
    float fog_amount = saturate(fog_t * mix(0.78, 1.0, horizon));

    vec3 color = mix(lit_color, fog_color, fog_amount);
    o_color = vec4(color, tex.a);
}
