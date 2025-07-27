#pragma once

#include <chrono>
#include <memory>
#include <array>
#include "../base/base.h"

class GLTFLoader;

using namespace std::chrono;

class Mesh : public Base {
public:
    Mesh(uint32_t _width, uint32_t _height, const char* _windowName);
    ~Mesh();

    void run();

private:
    void createDescriptors();
    void initDescriptorSets();
    void initMeshPipeline();
    void loadObj(const char *filePath);
    void loadTextureImage(const char* filePath);
    void createIndirectBuffer();
    void recordCommands(VkCommandBuffer cmd, uint32_t frameNumber, VkImageView swapchainImageView);
    void drawFrame();
    void updateTransformMatrix();

    VkPipelineLayout              meshPipelineLayout;
    VkPipeline                    meshPipeline;

    VkPipelineLayout              instancePipelineLayout;
    VkPipeline                    instancePipeline;


    MeshPushConstants             pushConstants;
    glm::mat4                     transform;

    VkDescriptorSet               meshDescriptorSet;
    VkDescriptorSetLayout         meshDescriptorLayout { VK_NULL_HANDLE} ;
    std::array<VkDescriptorSet, MAX_FRAMES> imageDescriptorSets;

    VkSampler                     texSampler;
    std::vector<AllocatedImage>   objTextures;

    glm::mat4                     transformMatrix;

    AllocatedBuffer               vertexBuffer;
    AllocatedBuffer               indexBuffer;
    uint32_t                      indexCount;
    AllocatedImage                texImage;
    MeshBuffers                   meshBuffer;

    AllocatedBuffer               indirectBuffer;
    DrawIndexedIndirectCommand    indirectCommand;

    steady_clock::time_point      lastFrameTime;
    bool                          firstFrame {true};

    uint32_t                      currentFrame { 0 };
};
