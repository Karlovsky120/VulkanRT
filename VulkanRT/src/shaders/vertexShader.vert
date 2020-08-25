#version 450
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_scalar_block_layout : require

layout(scalar, set = 0, binding = 0) readonly buffer Vertices {
    vec3 vertices[];
};

layout(set = 0, binding = 1) readonly buffer Indices {
    uint16_t indices[];
};

layout(location = 0) out vec3 fragColor;

vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
);

void main() {
    gl_Position = vec4(vertices[uint(indices[gl_VertexIndex])], 1.0);
    fragColor = colors[gl_VertexIndex % 3];
}