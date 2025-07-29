#pragma once

#include <chrono>
#include <memory>
#include <array>
#include "../base/base.h"

#define INSTANCE_COUNT 100000
class GLTFLoader;

using namespace std::chrono;

class Mesh : public Base {
public:
    Mesh(uint32_t _width, uint32_t _height, const char* _windowName);
    ~Mesh();

    void run();

private:
    void createInstances();
    void createCullBuffers();
    void initDescriptorSets();
    void initMeshPipeline();
    void initCullPipeline();
    void loadObj(const char *filePath);
    void loadTextureImage(const char* filePath);
    void createIndirectBuffer();
    void recordCommands(VkCommandBuffer cmd, uint32_t frameNumber, VkImageView swapchainImageView);
    void drawFrame();
    void updateCullData(uint32_t frameIndex);
    void updatePerFrameData(uint32_t frameIndex);
    void readCullStats(uint32_t frameIndex);
    void debugIndirectBuffer(VkCommandBuffer cmd);
    void createMipmaps(VkCommandBuffer cmd, VkImage image, VkFormat imageFormat,
                          int32_t texWidth, int32_t texHeight, uint32_t mipLevels);

    VkPipelineLayout              meshPipelineLayout;
    VkPipeline                    meshPipeline;

    VkPipelineLayout              cullPipelineLayout;
    VkPipeline                    cullPipeline;

    MeshPushConstants             pushConstants;
    glm::mat4                     transform;

    VkDescriptorSetLayout                   meshDescriptorLayout;
    std::array<VkDescriptorSet, MAX_FRAMES> imageDescriptorSets;

    VkDescriptorSetLayout                   cullDescriptorLayout;
    std::array<AllocatedBuffer, MAX_FRAMES> cullDataBuffers;
    std::array<AllocatedBuffer, MAX_FRAMES> cullStatsBuffers;
    std::array<VkDescriptorSet, MAX_FRAMES> cullDescriptorSets;

    AllocatedBuffer                         visibilityBuffer;

    VkSampler                     texSampler;
    VkSampler                     depthSampler;

    glm::mat4                     transformMatrix;
    glm::mat4                     viewProj;

    AllocatedBuffer               vertexBuffer;
    AllocatedBuffer               indexBuffer;
    AllocatedBuffer               instanceBuffer;
    uint32_t                      indexCount;
    AllocatedImage                texImage;

    uint32_t                      trueInstanceCount;

    AllocatedBuffer               indirectBuffer;
    DrawIndexedIndirectCommand    indirectCommand;

    std::vector<InstanceData>                      instances;
    std::vector<VkVertexInputBindingDescription>   vertexBindings;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;

    uint32_t                      currentFrame { 0 };
};
