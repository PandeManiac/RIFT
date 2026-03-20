#version 460 core
#extension GL_ARB_bindless_texture : require

layout(bindless_sampler) uniform sampler2DArray u_textures;
layout(location = 5) uniform vec4 u_fog_distance_params;
layout(location = 6) uniform vec3 u_fog_color_near;
layout(location = 7) uniform vec3 u_fog_color_far;
layout(location = 8) uniform vec4 u_fog_aberration_params;

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

    const vec3 sun_dir = normalize(vec3(0.45, 0.8, 0.35));

    float diffuse = max(dot(v_normal, sun_dir), 0.0);
    float hemi    = v_normal.y * 0.5 + 0.5;
    float ao      = mix(0.65, 1.0, hemi);
    float light   = 0.22 + diffuse * 0.38 + ao * 0.40;
    vec3 base_color = tex.rgb * light;

    float view_distance = length(v_view_pos);
    vec3 view_dir       = view_distance > 0.0 ? v_view_pos / view_distance : vec3(0.0, 0.0, 1.0);

    float fog_range = max(u_fog_distance_params.y - u_fog_distance_params.x, 1.0);
    float fog_t = saturate((view_distance - u_fog_distance_params.x) / fog_range);
    fog_t = pow(fog_t, u_fog_distance_params.z);

    float horizon = pow(saturate(1.0 - abs(view_dir.y)), u_fog_aberration_params.y);
    float upward  = saturate(view_dir.y * 0.5 + 0.5);

    vec3 fog_color = mix(u_fog_color_near, u_fog_color_far, saturate(horizon * 0.8 + upward * 0.35));
    float fog_amount = saturate(fog_t * mix(0.72, 1.0, horizon));

    float aberration = pow(fog_t, 1.2) * horizon * u_fog_aberration_params.x;
    vec3 fog_channels = clamp(
        vec3(fog_amount + aberration, fog_amount, fog_amount - aberration * u_fog_aberration_params.z),
        0.0,
        1.0
    );

    vec3 fogged_color = vec3(
        mix(base_color.r, fog_color.r, fog_channels.r),
        mix(base_color.g, fog_color.g, fog_channels.g),
        mix(base_color.b, fog_color.b, fog_channels.b)
    );

    o_color = vec4(fogged_color, tex.a);
}
