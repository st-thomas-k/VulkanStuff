#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "swapchain.h"
#include "../tools/types.h"
#include "../tools/camera.h"
#include <vk_mem_alloc.h>


class Base {
private:
    void prepare();
    void initWindow();
    void initInstance();
    void initVulkan();
    void initAllocator();
    void createCommandPool();
    virtual void createCommandBuffers();
    void initFrameData();
    void initImmStructures();

    bool initialized { false };

protected:
    VkInstance                   instance       { VK_NULL_HANDLE };
    VkDebugUtilsMessengerEXT     debugMessenger { VK_NULL_HANDLE };
    VkPhysicalDevice             physicalDevice { VK_NULL_HANDLE };
    VkSurfaceKHR                 surface        { VK_NULL_HANDLE };
    QueueFamilyIndices           indices;
    VkDevice                     device         { VK_NULL_HANDLE };
    VkQueue                      graphicsQueue  { VK_NULL_HANDLE };
    VkQueue                      presentQueue   { VK_NULL_HANDLE };
    GLFWwindow*                  window;
    VmaAllocator                 allocator;
    Camera                       camera;

    VkExtent2D                   windowExtent;
    const char*                  windowName;

    VkCommandPool                commandPool;

    FrameData                    frames[MAX_FRAMES];

    VkCommandPool                immCommandPool;
    VkCommandBuffer              immCommandBuffer;
    VkFence                      immFence;

    AllocatedBuffer              vertexBuffer;
    AllocatedBuffer              indexBuffer;
    uint32_t                     indexCount;

    VkImageLayout                depthImageLayout;
    AllocatedImage               depthImage;

    VkPipelineStageFlags         submitPipelineStages { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    std::vector<VkCommandBuffer> drawCommandBuffers {};

    glm::mat4                    transform;

    std::vector<std::shared_ptr<MeshAsset>> meshes;

    VkShaderModule loadShader(VkDevice device, const char *filePath);
    MeshBuffers loadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);
    void loadObj(const char *filePath);
    AllocatedImage loadTextureImage(const char *filePath);
    void createMipmaps(VkCommandBuffer cmd, VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);
    virtual void beginCommands(VkCommandBuffer cmd, VkImageView swapchainImageView);
    virtual void endCommands(VkCommandBuffer cmd);
    virtual void updatePerFrameData(uint32_t frameIndex);
    void initCamera(float x, float y, float z);
    void initDepthImage();

public:
    Swapchain swapchain;

    Base(uint32_t _width, uint32_t _height, const char* _windowName);
    virtual ~Base();

    bool isInitialized() const { return initialized; }
    AllocatedImage  createAllocatedImage(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    AllocatedBuffer createAllocatedBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);

    void destroyAllocatedImage(VkImage image, VmaAllocation allocation);
    void destroyAllocatedBuffer(VkBuffer buffer, VmaAllocation allocation);
    void immediateSubmit(std::function<void(VkCommandBuffer cmd)> &&function);
};
