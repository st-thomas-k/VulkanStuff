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
VkSemaphoreSubmitInfo semaphoreSubmitInfo(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore);

VkCommandBufferBeginInfo commandBufferBeginInfo(VkCommandBufferUsageFlags flags = 0);
VkCommandBufferSubmitInfo submitCommandBufferInfo(VkCommandBuffer cmd);
VkSubmitInfo2 submitInfo(VkCommandBufferSubmitInfo* cmd, VkSemaphoreSubmitInfo* signalSemaphoreInfo,
    VkSemaphoreSubmitInfo* waitSemaphoreInfo);

void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
void copyImageToImage(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D srcSize,
    VkExtent2D dstSize);


VkImageCreateInfo imageCreateInfo(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent);
VkImageViewCreateInfo imageviewCreateInfo(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags);

VkDescriptorSetLayoutBinding descriptorSetLayoutBinding(
            VkDescriptorType type,
            VkShaderStageFlags stageFlags,
            uint32_t binding,
            uint32_t descriptorCount = 1);

VkPipelineShaderStageCreateInfo pipelineShaderStageCreateInfo(VkShaderStageFlagBits stage,
    VkShaderModule shaderModule, const char * entry = "main");

VkSubmitInfo2 createSubmitInfo(VkCommandBufferSubmitInfo* cmd, VkSemaphoreSubmitInfo* signalSemaphoreInfo,
    VkSemaphoreSubmitInfo* waitSemaphoreInfo);

VkRenderingAttachmentInfo getColorAttachment(VkImageView imageView, VkClearValue *clear, VkImageLayout layout);
VkRenderingAttachmentInfo getDepthAttachment(VkImageView imageView, VkClearValue* clear, VkImageLayout layout);
VkRenderingInfo getRenderingInfo(VkExtent2D renderExtent, VkRenderingAttachmentInfo *colorAttachment,
    VkRenderingAttachmentInfo *depthAttachment);

VkViewport initViewport(VkExtent2D renderExtent);
VkRect2D initScissor(VkViewport viewport);


