#pragma once
#include <vulkan/vulkan.h>
#include <cassert>
#include <functional>
#include "types.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#define MAX_FRAMES 3

#define VK_CHECK(x)                                                 \
do {                                                                \
VkResult err = x;                                                   \
assert(err == VK_SUCCESS);                                          \
} while (0)

namespace std {
    template<>
    struct hash<glm::vec3> {
        size_t operator()(const glm::vec3& v) const noexcept {
            size_t h1 = hash<float>()(v.x);
            size_t h2 = hash<float>()(v.y);
            size_t h3 = hash<float>()(v.z);
            return ((h1 ^ (h2 << 1)) >> 1) ^ (h3 << 1);
        }
    };

    template<>
    struct hash<Vertex> {
        size_t operator()(const Vertex& vertex) const noexcept {
            size_t seed = 0;
            seed ^= hash<glm::vec3>()(vertex.position) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= hash<float>()(vertex.uv_x) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= hash<glm::vec3>()(vertex.normal) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= hash<float>()(vertex.uv_y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= hash<glm::vec3>()(vertex.color) + 0x9e3779b9 + (seed << 6) + (seed >> 2);

            return seed;
        }
    };
}

VkSemaphore createSemaphore(VkDevice device, VkSemaphoreCreateFlags flags = 0);
VkFence createFence(VkDevice device, VkFenceCreateFlags flags = 0);

void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
void copyImageToImage(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D srcSize,
    VkExtent2D dstSize);



