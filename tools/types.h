#pragma once
#include <deque>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "vk_mem_alloc.h"
#include <glm/vec4.hpp>
#include <glm/matrix.hpp>



struct DescriptorLayout {
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    void addBinding(uint32_t binding, VkDescriptorType type);
    void clear();

    VkDescriptorSetLayout build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext = nullptr,
        VkDescriptorSetLayoutCreateFlags flags = 0);
};

struct DescriptorAllocator {

    struct PoolSizeRatio{
        VkDescriptorType type;
        float ratio;
    };

    VkDescriptorPool pool;

    void initPool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
    void clearDescriptors(VkDevice device);
    void destroyPool(VkDevice device);

    VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout);
};


struct DescriptorWriter {
    std::deque<VkDescriptorImageInfo> imageInfos;
    std::deque<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkWriteDescriptorSet> writes;

    void writeImage(int binding,VkImageView image,VkSampler sampler , VkImageLayout layout, VkDescriptorType type);
    void writeBuffer(int binding,VkBuffer buffer,size_t size, size_t offset,VkDescriptorType type);

    void clear();
    void updateSet(VkDevice device, VkDescriptorSet set);
};

struct DescriptorAllocatorGrowable {
public:
    struct PoolSizeRatio{
        VkDescriptorType type;
        float ratio;
    };

    void init(VkDevice device, uint32_t initialSets, std::span<PoolSizeRatio> poolRatios);
    void clear_pools(VkDevice device);
    void destroy_pools(VkDevice device);

    VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout, void* pNext = nullptr);
private:
    VkDescriptorPool get_pool(VkDevice device);
    VkDescriptorPool create_pool(VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios);

    std::vector<PoolSizeRatio> ratios;
    std::vector<VkDescriptorPool> fullPools;
    std::vector<VkDescriptorPool> readyPools;
    uint32_t setsPerPool;

};

struct QueueFamilyIndices {
    uint32_t graphicsFamily;
    uint32_t presentFamily;
    bool graphicsFamilyHasValue = false;
    bool presentFamilyHasValue = false;
    bool isComplete() const { return graphicsFamilyHasValue && presentFamilyHasValue; }
};

struct PipelineBuilder {

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly;
    VkPipelineRasterizationStateCreateInfo rasterizer;
    VkPipelineColorBlendAttachmentState colorBlendAttachment;
    VkPipelineMultisampleStateCreateInfo multisampling;
    VkPipelineLayout pipelineLayout;
    VkPipelineDepthStencilStateCreateInfo depthStencil;
    VkPipelineRenderingCreateInfo renderInfo;
    VkFormat colorAttachmentFormat;
    VkPipelineVertexInputStateCreateInfo vertexInputInfo;

    PipelineBuilder() { clear(); }

    void clear();

    VkPipeline buildPipeline(VkDevice device);
    void setShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader);
    void setInputTopology(VkPrimitiveTopology topology);
    void setPolygonMode(VkPolygonMode mode);
    void setCullMode(VkCullModeFlags cullModeFlags, VkFrontFace frontFace);
    void setMultisamplingNone();
    void setMultisamplingSampleRate(float minSampleShading);
    void disableBlending();
    void setColorAttachmentFormat(VkFormat format);
    void setDepthFormat(VkFormat format);
    void disableDepthTest();
    void enableDepthTest(bool depthWriteEnable, VkCompareOp op);
    void enableBlendingAdditive();
    void enableBlendingAlphaBlend();
};


struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};


struct FrameData {
    VkCommandBuffer commandBuffer;
    VkSemaphore imgAvailable, renderComplete;
    VkFence renderFence;

    DescriptorAllocatorGrowable _frameDescriptors;
};


struct AllocatedImage {
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
};

struct AllocatedBuffer {
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo info;
    VkDeviceAddress bufferAddress = 0;
};

struct ComputePushConstants {
    glm::vec4 data1;
    glm::vec4 data2;
    glm::vec4 data3;
    glm::vec4 data4;
};

struct DrawPushConstants {
    glm::mat4 worldMatrix;
    VkDeviceAddress vertexBuffer;
    glm::vec2 padding1;

    glm::vec4 baseColorFactor;
    glm::vec2 metallicRoughnessFactor;
    glm::vec2 padding2;
};

struct MeshPushConstants {
    glm::mat4             worldMatrix;
    VkDeviceAddress       vertexBuffer;
};

struct MeshBuffers {
    AllocatedBuffer indexBuffer;
    AllocatedBuffer vertexBuffer;
    VkDeviceAddress vertexBufferAddress;
};

struct Texture {
    AllocatedImage image;
    VkImageView imageView;
    VkSampler sampler;
};

struct Material {
    std::string name;
    glm::vec4 baseColorFactor = glm::vec4(1.0f);
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    glm::vec3 emissiveFactor = glm::vec3(0.0f);


    int baseColorTexture = -1;
    int metallicRoughnessTexture = -1;
    int normalTexture = -1;
    int occlusionTexture = -1;
    int emissiveTexture = -1;

    bool doubleSided = false;
    std::string alphaMode = "OPAQUE";
    float alphaCutoff = 0.5f;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

struct TextureResources {
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    bool valid = false;
};

struct GeoSurface {
    uint32_t startIndex;
    uint32_t count;
    std::shared_ptr<Material> material;

    VkDescriptorSet materialDescriptorSet;
};

struct MeshAsset {
    std::string name;
    std::vector<GeoSurface> surfaces;
    MeshBuffers meshBuffers;
};


struct Vertex {
    glm::vec3 position;
    float uv_x;
    glm::vec3 normal;
    float uv_y;
    glm::vec3 color;
    float pad;

    bool operator==(const Vertex& other) const {
        return position == other.position &&
               color == other.color &&
               normal == other.normal &&
               uv_x == other.uv_x &&
               uv_y == other.uv_y;
    }
};

struct SimpleMaterial {
    std::string name;
    glm::vec3 ambient;
    glm::vec3 diffuse;
    glm::vec3 specular;
    float shininess;
};

struct ObjMeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SimpleMaterial> materials;
    std::vector<uint32_t> materialIds;

};

struct DrawIndexedIndirectCommand {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t vertexOffset;
    uint32_t firstInstance;
};

struct FrameTimings {
    float waitFence;
    float acquireImage;
    float recordCommands;
    float submit;
    float present;
    float total;
};

struct InstanceData {
    glm::vec3 position;
    float scale;
};

struct CullData{
    glm::mat4 viewProj;
    glm::vec4 frustumPlanes[6]; // L, R, B, T, N, F
};

struct CullStats {
    uint32_t visibleCount;
    uint32_t occludedCount;
    uint32_t totalCount;
};

