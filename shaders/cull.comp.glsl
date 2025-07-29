#version 450

layout(local_size_x = 64) in;

// Binding 0: Frustum data
layout(set = 0, binding = 0) uniform CullData {
    mat4 viewProj;
    vec4 frustumPlanes[6];
} cullData;

// Binding 1: Instance data
struct InstanceData {
    vec3 position;
    float scale;
};

layout(set = 0, binding = 1) readonly buffer InstanceBuffer {
    InstanceData instances[];
} instanceBuffer;

// Binding 2: Indirect commands
layout(set = 0, binding = 2) buffer IndirectBuffer {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
} indirectCmd;

// Binding 3: Stats buffer
layout(set = 0, binding = 3) buffer StatsBuffer {
    uint visibleCount;
    uint culledCount;
    uint totalCount;
} stats;

layout(set = 0, binding = 4) buffer VisibilityBuffer {
    uint visible[];  // 1 if visible, 0 if culled
} visibility;

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
        stats.culledCount = 0;
        stats.totalCount = 100000;
        indirectCmd.instanceCount = 0;
    }

    barrier();

    uint idx = gl_GlobalInvocationID.x;
    if (idx >= 100000) return;

    vec3 position = instanceBuffer.instances[idx].position;
    float scale = instanceBuffer.instances[idx].scale;
    float radius = scale * 1.5; // Conservative bounding sphere

    if (isVisible(position, radius)) {
        atomicAdd(stats.visibleCount, 1);
        atomicAdd(indirectCmd.instanceCount, 1);
    } else {
        atomicAdd(stats.culledCount, 1);
    }


}