#pragma once

#include <chrono>
#include <memory>
#include <array>
#include "../base/base.h"

#define INSTANCE_COUNT 8000
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
    void initInstancePipeline();
    void initCullPipeline();
    void createIndirectCmdBuffer();
    void recordCommands(VkCommandBuffer cmd, uint32_t frameNumber, VkImageView swapchainImageView);
    void drawFrame();
    void updateCullData(uint32_t frameIndex);
    void updatePerFrameData(uint32_t frameIndex) override;
    void readCullStats(uint32_t frameIndex);

    VkPipelineLayout              meshPipelineLayout;
    VkPipeline                    meshPipeline;

    VkPipelineLayout              cullPipelineLayout;
    VkPipeline                    cullPipeline;

    MeshPushConstants             pushConstants;

    VkDescriptorSetLayout                   meshDescriptorLayout;
    std::array<VkDescriptorSet, MAX_FRAMES> imageDescriptorSets;

    VkDescriptorSetLayout                   cullDescriptorLayout;
    std::array<AllocatedBuffer, MAX_FRAMES> cullDataBuffers;
    std::array<AllocatedBuffer, MAX_FRAMES> cullStatsBuffers;
    std::array<VkDescriptorSet, MAX_FRAMES> cullDescriptorSets;

    VkSampler                  texSampler;

    glm::mat4                  transformMatrix;
    glm::mat4                  viewProj;

    std::vector<DrawIndexedIndirectCommand> drawIndirectCmds;
    AllocatedBuffer            instanceBuffer;
    AllocatedImage             textureImage;

    AllocatedBuffer            drawCmdBuffer;
    DrawIndexedIndirectCommand indirectCommand;

    std::vector<InstanceData>                      instances;
    uint32_t                                       trueInstanceCount;
    std::vector<VkVertexInputBindingDescription>   vertexBindings;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;

    uint32_t                   currentFrame { 0 };
};
