#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_scalar_block_layout  : require
#extension GL_EXT_shader_16bit_storage : require

layout(set = 0, binding = 0, scalar) readonly buffer Vertices {
    float vertices[];
};

layout(set = 0, binding = 1) readonly buffer Indices {
    uint16_t indices[];
};

layout(location = 0) rayPayloadInEXT vec3 hitValue;

void main() {
    ivec3 ind = ivec3(int(indices[3 * gl_PrimitiveID + 0]),
                      int(indices[3 * gl_PrimitiveID + 1]),
                      int(indices[3 * gl_PrimitiveID + 2]));

    vec3 v0 = vec3(vertices[ind.x * 3], vertices[ind.x * 3 + 1], vertices[ind.x * 3 + 2]);
    vec3 v1 = vec3(vertices[ind.y * 3], vertices[ind.y * 3 + 1], vertices[ind.y * 3 + 2]);
    vec3 v2 = vec3(vertices[ind.z * 3], vertices[ind.z * 3 + 1], vertices[ind.z * 3 + 2]);

    vec3 first = v1 - v0;
    vec3 second = v2 - v0;
    vec3 normal = normalize(cross(first, second));

    normal.y = -normal.y;

    hitValue = (normal + 3) * 0.25 * abs(normal);
}