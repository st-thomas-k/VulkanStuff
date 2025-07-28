#version 450
#extension GL_EXT_buffer_reference : require

struct Vertex {
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec3 color;
    float pad;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
};

// Updated instance attributes
layout(location = 0) in vec3 instancePos;
layout(location = 1) in float instanceScale;  // Now just a float

layout(push_constant) uniform PushConstants {
    mat4 worldMatrix;
    VertexBuffer vertexBuffer;
} pushConstants;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    Vertex v = pushConstants.vertexBuffer.vertices[gl_VertexIndex];

    // Apply uniform scale
    vec3 worldPos = v.position * instanceScale + instancePos;

    gl_Position = pushConstants.worldMatrix * vec4(worldPos, 1.0);
    fragColor = v.color;
    fragTexCoord = vec2(v.uv_x, v.uv_y);
}