#version 460 core
#extension GL_ARB_bindless_texture : require

layout(bindless_sampler) uniform sampler2DArray u_textures;

flat in uint  v_material;
flat in vec3  v_normal;
in vec2       v_uv;

out vec4 o_color;

void main()
{
    vec4 tex = texture(u_textures, vec3(v_uv, float(v_material)));

    const vec3 sun_dir = normalize(vec3(0.45, 0.8, 0.35));

    float diffuse = max(dot(v_normal, sun_dir), 0.0);
    float hemi    = v_normal.y * 0.5 + 0.5;
    float ao      = mix(0.65, 1.0, hemi);
    float light   = 0.22 + diffuse * 0.38 + ao * 0.40;

    o_color = vec4(tex.rgb * light, tex.a);
}
