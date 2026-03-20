#version 460 core

layout(std430, binding = 0) readonly buffer Faces
{
    uvec2 faces[];
};

struct ChunkMetadata
{
    ivec4 pos_and_offset;
    uint  face_count;
    uint  padding[3];
};

layout(std430, binding = 1) readonly buffer InMetadata
{
    ChunkMetadata chunk_metadata[];
};

layout (location = 0) uniform mat4  u_mvp;
layout (location = 3) uniform ivec3 u_cam_chunk;
layout (location = 4) uniform vec3  u_cam_offset;

flat out uint  v_material;
flat out vec3  v_normal;
out vec2       v_uv;

const vec2 QUAD_CCW[6] = vec2[](
    vec2(0,0),
    vec2(1,0),
    vec2(0,1),

    vec2(1,0),
    vec2(1,1),
    vec2(0,1)
);

const vec2 QUAD_FLIP[6] = vec2[](
    vec2(0,0),
    vec2(0,1),
    vec2(1,0),

    vec2(1,0),
    vec2(0,1),
    vec2(1,1)
);

const vec3 U_TABLE[3] = vec3[](
    vec3(0,1,0),
    vec3(0,0,1),
    vec3(1,0,0)
);

const vec3 V_TABLE[3] = vec3[](
    vec3(0,0,1),
    vec3(1,0,0),
    vec3(0,1,0)
);

const float SNAP_SCALE = 256.0;

void main()
{
    ivec4 metadata = chunk_metadata[gl_DrawID].pos_and_offset;
    ivec3 u_chunk_coord = metadata.xyz;
    uint face_offset = uint(metadata.w);

    uint face_index = face_offset + uint(gl_VertexID / 6);
    uint corner     = uint(gl_VertexID % 6);

    uvec2 face = faces[face_index];
    uint lo = face.x;
    uint hi = face.y;

    uint x        =  lo        & 63u;
    uint y        = (lo >> 6)  & 63u;
    uint z        = (lo >> 12) & 63u;
    uint face_dir = (lo >> 18) & 7u;
    uint material = (lo >> 21);

    uint width  = (hi        & 63u) + 1u;
    uint height = ((hi >> 6) & 63u) + 1u;

    uint group = face_dir >> 1u;

    vec3 base = vec3(x, y, z);

    vec3 normal = vec3(0.0);
    normal[group] = (face_dir & 1u) != 0u ? -1.0 : 1.0;

    if (face_dir == 0u)
    {
        base.x += 1.0;
    }

    if (face_dir == 2u)
    {
        base.y += 1.0;
    }

    if (face_dir == 4u)
    {
        base.z += 1.0;
    }

    vec3 u = U_TABLE[group];
    vec3 v = V_TABLE[group];

    vec2 q = ((face_dir & 1u) == 0u) ? QUAD_CCW[corner] : QUAD_FLIP[corner];

    vec2 face_size = vec2(float(width), float(height));
    vec2 face_pos  = q * face_size;

    ivec3 chunk_delta = u_chunk_coord - u_cam_chunk;

    vec3 voxel_pos = base + u * face_pos.x + v * face_pos.y;
    
    vec3 pos_rel = vec3(chunk_delta * 64) + voxel_pos - u_cam_offset;
    pos_rel = floor(pos_rel * SNAP_SCALE) / SNAP_SCALE;

    v_uv       = q * face_size;
    v_material = material;
    v_normal   = normal;

    gl_Position = u_mvp * vec4(pos_rel, 1.0);
}
