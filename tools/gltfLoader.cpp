#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"
#include "gltfLoader.h"
#include "../src/mesh.h"
#include "utils.h"


#include <iostream>
#include <queue>

glm::mat4 GLTFLoader::Node::getWorldMatrix() const {
    glm::mat4 m = matrix;
    Node* p = parent;
    while (p) {
        m = p->matrix * m;
        p = p->parent;
    }
    return m;
}

void GLTFLoader::loadFromFile(const std::string& filename) {
    tinygltf::Model gltfModel;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ret = false;
    std::string ext = filename.substr(filename.find_last_of(".") + 1);

    if (ext == "glb") {
        ret = loader.LoadBinaryFromFile(&gltfModel, &err, &warn, filename);
    } else if (ext == "gltf") {
        ret = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, filename);
    } else {
        throw std::runtime_error("Unknown file extension: " + ext);
    }

    if (!warn.empty()) {
        std::cout << "glTF Warning: " << warn << std::endl;
    }

    if (!ret) {
        throw std::runtime_error("Failed to load glTF: " + err);
    }

    if (gltfModel.scenes.empty()) {
        throw std::runtime_error("glTF file contains no scenes");
    }

    loadImages(gltfModel);
    loadTextures(gltfModel);
    loadMaterials(gltfModel);

    int sceneIndex = 0;
    if (gltfModel.defaultScene >= 0 && gltfModel.defaultScene < static_cast<int>(gltfModel.scenes.size())) {
        sceneIndex = gltfModel.defaultScene;
    }

    const tinygltf::Scene& scene = gltfModel.scenes[sceneIndex];

    for (size_t i = 0; i < scene.nodes.size(); i++) {
        int nodeIndex = scene.nodes[i];
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(gltfModel.nodes.size())) {
            std::cerr << "Invalid node index in scene: " << nodeIndex << std::endl;
            continue;
        }
        const tinygltf::Node& node = gltfModel.nodes[nodeIndex];
        loadNode(node, gltfModel, nullptr);
    }

    if (vertices.empty()) {
        throw std::runtime_error("No vertices loaded from glTF file");
    }
    if (indices.empty()) {
        throw std::runtime_error("No indices loaded from glTF file");
    }

}




void GLTFLoader::loadNode(const tinygltf::Node& inputNode,
                          const tinygltf::Model& model,
                          Node* parent) {
    auto node = std::make_unique<Node>();
    node->parent = parent;
    node->matrix = glm::mat4(1.0f);

    if (inputNode.matrix.size() == 16) {

        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                node->matrix[col][row] = static_cast<float>(inputNode.matrix[col * 4 + row]);
            }
        }
    } else {

        if (inputNode.translation.size() == 3) {
            glm::vec3 translation(
                static_cast<float>(inputNode.translation[0]),
                static_cast<float>(inputNode.translation[1]),
                static_cast<float>(inputNode.translation[2])
            );
            node->matrix = glm::translate(node->matrix, translation);
        }
        if (inputNode.rotation.size() == 4) {
            glm::quat q(
                static_cast<float>(inputNode.rotation[3]),
                static_cast<float>(inputNode.rotation[0]),
                static_cast<float>(inputNode.rotation[1]),
                static_cast<float>(inputNode.rotation[2])
            );
            node->matrix *= glm::mat4_cast(q);
        }
        if (inputNode.scale.size() == 3) {
            glm::vec3 scale(
                static_cast<float>(inputNode.scale[0]),
                static_cast<float>(inputNode.scale[1]),
                static_cast<float>(inputNode.scale[2])
            );
            node->matrix = glm::scale(node->matrix, scale);
        }
    }

    if (inputNode.mesh > -1) {
        if (inputNode.mesh >= model.meshes.size()) {
            std::cerr << "Invalid mesh index: " << inputNode.mesh << std::endl;
            return;
        }
        const tinygltf::Mesh& mesh = model.meshes[inputNode.mesh];

        for (const auto& primitive : mesh.primitives) {
            uint32_t firstIndex = static_cast<uint32_t>(indices.size());
            uint32_t vertexStart = static_cast<uint32_t>(vertices.size());


            if (primitive.attributes.find("POSITION") != primitive.attributes.end()) {
                const tinygltf::Accessor& posAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
                const tinygltf::BufferView& posView = model.bufferViews[posAccessor.bufferView];
                const float* posBuffer = reinterpret_cast<const float*>(
                    &model.buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]
                );

                size_t vertexCount = posAccessor.count;

                const float* normalBuffer = nullptr;
                const float* uvBuffer = nullptr;

                if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
                    const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.find("NORMAL")->second];
                    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
                    normalBuffer = reinterpret_cast<const float*>(&model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]);
                }

                if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
                    const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
                    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
                    uvBuffer = reinterpret_cast<const float*>(&model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]);
                }

                for (size_t v = 0; v < vertexCount; v++) {
                    Vertex vertex{};

                    vertex.position = glm::vec3(
                        posBuffer[v * 3 + 0],
                        posBuffer[v * 3 + 1],
                        posBuffer[v * 3 + 2]
                    );

                    if (normalBuffer) {
                        glm::vec3 normal(
                            normalBuffer[v * 3 + 0],
                            normalBuffer[v * 3 + 1],
                            normalBuffer[v * 3 + 2]
                        );
                        vertex.normal = glm::normalize(normal);
                    } else {
                        vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                    }

                    if (uvBuffer) {
                        vertex.uv.x = uvBuffer[v * 2 + 0];
                        vertex.uv.y= uvBuffer[v * 2 + 1];
                    } else {
                        vertex.uv.x = 0.0f;
                        vertex.uv.y = 0.0f;
                    }

                    vertex.color = glm::vec4(1.0f);

                    vertices.push_back(vertex);
                }
            }

            if (primitive.indices >= 0) {
                const tinygltf::Accessor& accessor = model.accessors[primitive.indices];
                const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

                const void* dataPtr = &buffer.data[accessor.byteOffset + bufferView.byteOffset];

                switch (accessor.componentType) {
                    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
                        const uint32_t* buf = static_cast<const uint32_t*>(dataPtr);
                        for (size_t index = 0; index < accessor.count; index++) {
                            indices.push_back(buf[index] + vertexStart);
                        }
                        break;
                    }
                    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
                        const uint16_t* buf = static_cast<const uint16_t*>(dataPtr);
                        for (size_t index = 0; index < accessor.count; index++) {
                            indices.push_back(buf[index] + vertexStart);
                        }
                        break;
                    }
                    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
                        const uint8_t* buf = static_cast<const uint8_t*>(dataPtr);
                        for (size_t index = 0; index < accessor.count; index++) {
                            indices.push_back(buf[index] + vertexStart);
                        }
                        break;
                    }
                }

                Primitive prim;
                prim.firstIndex = firstIndex;
                prim.indexCount = static_cast<uint32_t>(accessor.count);
                prim.materialIndex = primitive.material;
                node->mesh.primitives.push_back(prim);
            }
        }
    }

    Node* nodePtr = node.get();
    if (parent) {
        parent->children.push_back(std::move(node));
    } else {
        nodes.push_back(std::move(node));
    }

    for (size_t i = 0; i < inputNode.children.size(); i++) {
        loadNode(model.nodes[inputNode.children[i]], model, nodePtr);
    }
}

void GLTFLoader::loadImages(tinygltf::Model &model) {
    images.clear();
    images.resize(model.images.size());

    for (size_t i = 0; i < model.images.size(); i++) {
        tinygltf::Image& gltfImage = model.images[i];
        Image& image = images[i];

        image.image = VK_NULL_HANDLE;
        image.imageView = VK_NULL_HANDLE;
        image.sampler = VK_NULL_HANDLE;
        image.allocation = VK_NULL_HANDLE;
        image.data = gltfImage.image;
        image.width = gltfImage.width;
        image.height = gltfImage.height;
        image.component = gltfImage.component;
        image.bits = gltfImage.bits;
        image.pixel_type = gltfImage.pixel_type;
        image.name = gltfImage.name;
        image.mimeType = gltfImage.mimeType;
        image.size = image.data.size();
    }
}

void GLTFLoader::loadTextures(tinygltf::Model &model) {
    textures.resize(model.textures.size());

    for (size_t i = 0; i < model.textures.size(); i++) {
        const tinygltf::Texture& gltfTexture = model.textures[i];
        Texture& texture = textures[i];

        texture.source = gltfTexture.source;
        texture.sampler = gltfTexture.sampler;
        texture.name = gltfTexture.name;
    }
}

void GLTFLoader::loadMaterials(tinygltf::Model &model) {materials.resize(model.materials.size());

    for (size_t i = 0; i < model.materials.size(); i++) {
        const tinygltf::Material& gltfMaterial = model.materials[i];
        Material& material = materials[i];

        material.name = gltfMaterial.name;

        const auto& pbr = gltfMaterial.pbrMetallicRoughness;

        if (pbr.baseColorFactor.size() == 4) {
            material.baseColorFactor = glm::vec4(
                static_cast<float>(pbr.baseColorFactor[0]),
                static_cast<float>(pbr.baseColorFactor[1]),
                static_cast<float>(pbr.baseColorFactor[2]),
                static_cast<float>(pbr.baseColorFactor[3])
            );
        }

        if (pbr.baseColorTexture.index >= 0) {
            material.baseColorTexture = pbr.baseColorTexture.index;
        }

        material.metallicFactor = static_cast<float>(pbr.metallicFactor);
        material.roughnessFactor = static_cast<float>(pbr.roughnessFactor);

        if (pbr.metallicRoughnessTexture.index >= 0) {
            material.metallicRoughnessTexture = pbr.metallicRoughnessTexture.index;
        }

        if (gltfMaterial.normalTexture.index >= 0) {
            material.normalTexture = gltfMaterial.normalTexture.index;
        }

        if (gltfMaterial.occlusionTexture.index >= 0) {
            material.occlusionTexture = gltfMaterial.occlusionTexture.index;
        }

        if (gltfMaterial.emissiveTexture.index >= 0) {
            material.emissiveTexture = gltfMaterial.emissiveTexture.index;
        }

        if (gltfMaterial.emissiveFactor.size() == 3) {
            material.emissiveFactor = glm::vec3(
                static_cast<float>(gltfMaterial.emissiveFactor[0]),
                static_cast<float>(gltfMaterial.emissiveFactor[1]),
                static_cast<float>(gltfMaterial.emissiveFactor[2])
            );
        }

        material.alphaMode = gltfMaterial.alphaMode;
        material.alphaCutoff = static_cast<float>(gltfMaterial.alphaCutoff);

        material.doubleSided = gltfMaterial.doubleSided;
    }
}

void GLTFLoader::createVulkanResources(Mesh *mesh, VkDevice device, VkPhysicalDevice physicalDevice,
    VkCommandPool commandPool, VkQueue queue) {
        for (size_t i = 0; i < images.size(); i++) {
            createVulkanImage(mesh, device, commandPool, queue, i);
        }
}

void GLTFLoader::cleanup(Mesh *mesh, VkDevice device) {
    for (auto& img : images) {
        if (img.sampler) {
            vkDestroySampler(device, img.sampler, nullptr);
            img.sampler = VK_NULL_HANDLE;
        }
        if (img.imageView) {
            vkDestroyImageView(device, img.imageView, nullptr);
            img.imageView = VK_NULL_HANDLE;
        }
        if (img.image && img.allocation) {
            mesh->vmaDestroyImageWrapper(img.image, img.allocation);
            img.image = VK_NULL_HANDLE;
            img.allocation = VK_NULL_HANDLE;
        }
    }
}

void GLTFLoader::createVulkanImage(Mesh *mesh, VkDevice device, VkCommandPool commandPool, VkQueue queue,
    size_t imageIndex) {
    if (imageIndex >= images.size()) return;

    Image& img = images[imageIndex];

    if (img.image != VK_NULL_HANDLE) return;

    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    if (img.component == 1) {
        format = VK_FORMAT_R8_UNORM;
    } else if (img.component == 2) {
        format = VK_FORMAT_R8G8_UNORM;
    } else if (img.component == 3) {
        std::vector<unsigned char> rgbaData;
        rgbaData.resize(img.width * img.height * 4);
        for (int i = 0; i < img.width * img.height; i++) {
            rgbaData[i * 4 + 0] = img.data[i * 3 + 0];
            rgbaData[i * 4 + 1] = img.data[i * 3 + 1];
            rgbaData[i * 4 + 2] = img.data[i * 3 + 2];
            rgbaData[i * 4 + 3] = 255;
        }
        img.data = std::move(rgbaData);
        img.component = 4;
        img.size = img.data.size();
    }

    uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(img.width, img.height)))) + 1;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    createGLTFBuffer(mesh, device, img.size,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VMA_MEMORY_USAGE_CPU_ONLY,
                     stagingBuffer, stagingAllocation);

    void* mappedData;
    mesh->vmaMapMemoryWrapper(stagingAllocation, &mappedData);
    memcpy(mappedData, img.data.data(), img.size);
    mesh->vmaUnmapMemoryWrapper(stagingAllocation);

    createGLTFImage(mesh, device, img.width, img.height, format,
                           VK_IMAGE_TILING_OPTIMAL,
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                           mipLevels, img.image, img.allocation);


    mesh->immediateSubmit([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = img.image;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {static_cast<uint32_t>(img.width),
                             static_cast<uint32_t>(img.height), 1};

        vkCmdCopyBufferToImage(cmd, stagingBuffer, img.image,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.subresourceRange.levelCount = 1;

        int32_t mipWidth = img.width;
        int32_t mipHeight = img.height;

        for (uint32_t i = 1; i < mipLevels; i++) {
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                0, nullptr,
                0, nullptr,
                1, &barrier);

            VkImageBlit blit{};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1,
                                  mipHeight > 1 ? mipHeight / 2 : 1, 1};
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;

            vkCmdBlitImage(cmd,
                img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit,
                VK_FILTER_LINEAR);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                0, nullptr,
                0, nullptr,
                1, &barrier);

            if (mipWidth > 1) mipWidth /= 2;
            if (mipHeight > 1) mipHeight /= 2;
        }

        barrier.subresourceRange.baseMipLevel = mipLevels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);
    });

    mesh->vmaDestroyBufferWrapper(stagingBuffer, stagingAllocation);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = img.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkResult result = vkCreateImageView(device, &viewInfo, nullptr, &img.imageView);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture image view!");
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels);

    result = vkCreateSampler(device, &samplerInfo, nullptr, &img.sampler);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture sampler!");
    }

}

void GLTFLoader::createGLTFImage(Mesh *mesh, VkDevice device, uint32_t width, uint32_t height, VkFormat format,
    VkImageTiling tiling, VkImageUsageFlags usage, uint32_t mipLevels, VkImage&image, VmaAllocation&allocation) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (mesh->vmaCreateImageWrapper(&imageInfo, &allocInfo, &image, &allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image!");
    }
}

void GLTFLoader::createGLTFBuffer(Mesh *mesh, VkDevice device, VkDeviceSize size, VkBufferUsageFlags usage,
    VmaMemoryUsage memoryUsage, VkBuffer&buffer, VmaAllocation&allocation) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;

    if (mesh->vmaCreateBufferWrapper(&bufferInfo, &allocInfo, &buffer, &allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer!");
    }
}


void GLTFLoader::createMaterialDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool,
    VkDescriptorSetLayout descriptorSetLayout) {
    // to do
}