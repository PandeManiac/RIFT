#version 460 core

in vec2 v_uv;
in vec4 v_color;

layout(location = 0) uniform sampler2D u_texture;

out vec4 o_color;

void main()
{
    float mask = texture(u_texture, v_uv).r;
    if (mask < 0.1) discard; // Simple alpha test
    o_color = vec4(v_color.rgb, v_color.a * mask);
}
