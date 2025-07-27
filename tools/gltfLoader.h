#pragma once
#include "types.h"
#include "utils.h"
#include "vk_mem_alloc.h"
#include <memory>


class Mesh;

namespace tinygltf {
    class Node;
    class Model;
}

class GLTFLoader {
public:
    struct Image {
        std::vector<unsigned char> data;
        VkDeviceSize size;
        int width;
        int height;
        int component;
        int bits;
        int pixel_type;
        std::string name;
        std::string mimeType;


        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    struct Texture {
        int source;
        int sampler;
        std::string name;
    };


    struct Primitive {
        uint32_t firstIndex;
        uint32_t indexCount;
        int32_t materialIndex;
    };

    struct GltfMesh {
        std::vector<Primitive> primitives;
    };

    struct Node {
        Node* parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
        GltfMesh mesh;
        glm::mat4 matrix{1.0f};
        glm::mat4 getWorldMatrix() const;
    };

    std::vector<Image> images;
    std::vector<Texture> textures;
    std::vector<Material> materials;

    std::vector<std::unique_ptr<Node>> nodes;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    void loadFromFile(const std::string& filename);
    void createVulkanResources(Mesh* mesh,
                              VkDevice device,
                              VkPhysicalDevice physicalDevice,
                              VkCommandPool commandPool,
                              VkQueue queue);

    void cleanup(Mesh* mesh, VkDevice device);



private:
    void loadNode(const tinygltf::Node& inputNode,
                  const tinygltf::Model& model,
                  Node* parent);

    void loadImages(tinygltf::Model& model);
    void loadTextures(tinygltf::Model& model);
    void loadMaterials(tinygltf::Model& model);

    void createVulkanImage(Mesh* mesh,
                         VkDevice device,
                         VkCommandPool commandPool,
                         VkQueue queue,
                         size_t imageIndex);

    void createMaterialDescriptorSets(VkDevice device,
                         VkDescriptorPool descriptorPool,
                         VkDescriptorSetLayout descriptorSetLayout);

    void createGLTFImage(Mesh* mesh,
                         VkDevice device,
                         uint32_t width, uint32_t height, VkFormat format,
                         VkImageTiling tiling, VkImageUsageFlags usage,
                         uint32_t mipLevels,
                         VkImage& image, VmaAllocation& allocation);

    void createGLTFBuffer(Mesh* mesh,
                          VkDevice device,
                          VkDeviceSize size, VkBufferUsageFlags usage,
                          VmaMemoryUsage memoryUsage,
                          VkBuffer& buffer, VmaAllocation& allocation);
};