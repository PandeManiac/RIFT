#version 460 core

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in uint a_color;

layout(location = 1) uniform mat4 u_projection;

out vec2 v_uv;
out vec4 v_color;

void main()
{
    v_uv = a_uv;
    // Unpack color 0xAABBGGRR (Little Endian uint32 from bytes)
    float r = float(a_color & 0xFFu) / 255.0;
    float g = float((a_color >> 8) & 0xFFu) / 255.0;
    float b = float((a_color >> 16) & 0xFFu) / 255.0;
    float a = float((a_color >> 24) & 0xFFu) / 255.0;
    v_color = vec4(r, g, b, a);

    gl_Position = u_projection * vec4(a_pos, 0.0, 1.0);
}
