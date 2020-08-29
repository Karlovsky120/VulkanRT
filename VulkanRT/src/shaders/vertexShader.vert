#version 450
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_scalar_block_layout : require

layout(scalar, set = 0, binding = 0) readonly buffer Vertices {
    float vertices[];
};

layout(set = 0, binding = 1) readonly buffer Indices {
    uint16_t indices[];
};

layout(location = 0) out vec3 fragColor;

layout(push_constant) uniform PushConstants {
	mat4 rotation;
	vec3 position;
    float oneOverTanOfHalfFov;
    float oneOverAspectRatio;
    float near;
} pushConstants;

vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
);

void main() {
    ivec3 indices = ivec3((int(indices[gl_VertexIndex]) * 3) + 0,
                          (int(indices[gl_VertexIndex]) * 3) + 1,
                          (int(indices[gl_VertexIndex]) * 3) + 2);

    vec3 vertex = vec3(vertices[indices.x],
                       vertices[indices.y],
                       vertices[indices.z]);

    gl_Position = vec4(vertex - pushConstants.position, 1.0) * pushConstants.rotation;

    gl_Position.x *= pushConstants.oneOverTanOfHalfFov * pushConstants.oneOverAspectRatio;
    gl_Position.y *= pushConstants.oneOverTanOfHalfFov;
    gl_Position.w = -gl_Position.z; 
    gl_Position.z = pushConstants.near;

    fragColor = colors[gl_VertexIndex % 3];
}
