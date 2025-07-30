#version 450

layout(local_size_x = 128) in;

layout(set = 0, binding = 0) uniform CullData {
    mat4 viewProj;
    vec4 frustumPlanes[6];
} cullData;

struct InstanceData {
    vec3 position;
    float scale;
};

layout(set = 0, binding = 1) readonly buffer InstanceBuffer {
    InstanceData instances[];
} instanceBuffer;

struct DrawIndexedIndirectCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

layout(set = 0, binding = 2) buffer IndirectCommands {
    DrawIndexedIndirectCommand commands[];
} indirectCommands;

layout(set = 0, binding = 3) buffer CullStats {
    uint visibleCount;
    uint occludedCount;
    uint totalCount;
} stats;

bool isVisible(vec3 center, float radius) {
    for (int i = 0; i < 6; i++) {
        float distance = dot(cullData.frustumPlanes[i].xyz, center) +
        cullData.frustumPlanes[i].w;
        if (distance < -radius) {
            return false;
        }
    }
    return true;
}

void main() {
    if (gl_GlobalInvocationID.x == 0) {
        stats.visibleCount = 0;
        stats.occludedCount = 0;
        stats.totalCount = 8000;
    }

    uint idx = gl_GlobalInvocationID.x;
    if (idx >= 8000) return;

    vec3 position = instanceBuffer.instances[idx].position;
    float scale = instanceBuffer.instances[idx].scale;
    float radius = scale * 1.732; // Conservative bounding sphere

    if (isVisible(position, radius)) {
        indirectCommands.commands[idx].instanceCount = 1;
        atomicAdd(stats.visibleCount, 1);
    } else {
        indirectCommands.commands[idx].instanceCount = 0;
    }
}