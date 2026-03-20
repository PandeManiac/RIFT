#version 460 core

layout(location = 0) uniform vec2 u_inv_viewport;
layout(location = 1) uniform vec2 u_depth_params;
layout(location = 2) uniform vec4 u_split_params;
layout(location = 3) uniform sampler2D u_scene_color;
layout(location = 4) uniform sampler2D u_scene_depth;

in vec2 v_uv;

out vec4 o_color;

float saturate(float x)
{
    return clamp(x, 0.0, 1.0);
}

float linearize_depth(float depth)
{
    float z_near = u_depth_params.x;
    float z_far = u_depth_params.y;
    return (z_near * z_far) / max(z_far - depth * (z_far - z_near), 1e-4);
}

vec3 sample_scene(vec2 uv)
{
    return texture(u_scene_color, clamp(uv, vec2(0.0), vec2(1.0))).rgb;
}

float sample_linear_depth(vec2 uv)
{
    return linearize_depth(texture(u_scene_depth, clamp(uv, vec2(0.0), vec2(1.0))).r);
}

void main()
{
    float depth = texture(u_scene_depth, v_uv).r;
    vec3 base = sample_scene(v_uv);

    if (depth >= 1.0)
    {
        o_color = vec4(base, 1.0);
        return;
    }

    float view_depth = linearize_depth(depth);
    float split_t = saturate((view_depth - u_split_params.x) / max(u_split_params.y - u_split_params.x, 1.0));
    split_t = pow(split_t, 1.6);

    vec2 pixel = u_inv_viewport;
    float depth_l = sample_linear_depth(v_uv - vec2(pixel.x, 0.0));
    float depth_r = sample_linear_depth(v_uv + vec2(pixel.x, 0.0));
    float depth_u = sample_linear_depth(v_uv + vec2(0.0, pixel.y));
    float depth_d = sample_linear_depth(v_uv - vec2(0.0, pixel.y));

    vec2 depth_grad = vec2(depth_r - depth_l, depth_u - depth_d);
    vec2 edge_dir = normalize(depth_grad + vec2(1e-5, 0.0));
    vec2 offset = edge_dir * u_inv_viewport * u_split_params.z;

    float front_depth = view_depth;
    float far_depth_pos = sample_linear_depth(v_uv + offset);
    float far_depth_neg = sample_linear_depth(v_uv - offset);
    float far_depth_pos2 = sample_linear_depth(v_uv + offset * 2.0);
    float far_depth_neg2 = sample_linear_depth(v_uv - offset * 2.0);

    float forward_gap = max(far_depth_pos - front_depth, far_depth_pos2 - front_depth);
    float backward_gap = max(far_depth_neg - front_depth, far_depth_neg2 - front_depth);
    float silhouette_gap = max(forward_gap, backward_gap);
    float silhouette_side = step(forward_gap, backward_gap);
    float intensity = split_t * smoothstep(4.0, 20.0, silhouette_gap);

    if (intensity <= 1e-4)
    {
        o_color = vec4(base, 1.0);
        return;
    }

    vec3 color = base;
    float red_gap = mix(forward_gap, backward_gap, silhouette_side);
    float blue_gap = mix(backward_gap, forward_gap, silhouette_side);
    float red_band = smoothstep(0.8, 6.0, red_gap) * intensity;
    float blue_band = smoothstep(0.8, 6.0, blue_gap) * intensity;
    float green_band = max(0.0, intensity - max(red_band, blue_band) * 0.78);

    vec3 red_color = vec3(1.00, 0.10, 0.04);
    vec3 green_color = vec3(0.12, 1.00, 0.28);
    vec3 blue_color = vec3(0.06, 0.36, 1.00);

    vec3 split_sample = vec3(
        sample_scene(v_uv + offset).r,
        sample_scene(v_uv).g,
        sample_scene(v_uv - offset).b
    );

    color = base;
    color += max(split_sample - base, vec3(0.0)) * (intensity * 0.18);
    color += red_color * (red_band * 1.15);
    color += green_color * (green_band * 0.30);
    color += blue_color * (blue_band * 1.20);
    color = clamp(color, 0.0, 1.0);

    o_color = vec4(color, 1.0);
}
