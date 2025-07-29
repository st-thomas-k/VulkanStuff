#include "mesh.h"

#include "GLFW/glfw3.h"
#include "../tools/gltfLoader.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <iostream>
#include <tiny_obj_loader.h>

Mesh::Mesh(uint32_t _width, uint32_t _height, const char* _windowName)
    : Base(_width, _height, _windowName) {

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    initCamera(0.0f, 50.0f, 100.0f);
    initDepthImage();


    loadObj("../assets/grass/Grass_Block.obj");
    loadTextureImage("../assets/grass/Grass_Block_TEX.png");

    createInstances();
    createCullBuffers();
    createIndirectBuffer();

    initDescriptorSets();

    initMeshPipeline();
    initCullPipeline();
}

void Mesh::initDescriptorSets() {
    // texture descriptor set
    {
        DescriptorLayout builder;
        builder.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        meshDescriptorLayout = builder.build(device, VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    VkSamplerCreateInfo sampler = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.minLod = 0.0f;
    sampler.maxLod = VK_LOD_CLAMP_NONE;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.mipLodBias = -0.25f;
    sampler.anisotropyEnable = VK_TRUE;
    sampler.maxAnisotropy = 16.0f;
    sampler.compareEnable = VK_FALSE;
    sampler.compareOp = VK_COMPARE_OP_ALWAYS;

    VK_CHECK(vkCreateSampler(device, &sampler, nullptr, &texSampler));

    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1.0f },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1.0f },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1.0f }
    };


    // tex descriptor set is the same for every instance.
    // just allocate them to each frame ig.
    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        frames[i]._frameDescriptors.init(device, 10, sizes);

        imageDescriptorSets[i] = frames[i]._frameDescriptors.allocate(device, meshDescriptorLayout);
        DescriptorWriter writer;
        writer.writeImage(0, texImage.imageView, texSampler,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        writer.updateSet(device, imageDescriptorSets[i]);
    }

    // cull descriptor set
    {
        DescriptorLayout builder;
        builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        builder.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        builder.addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        builder.addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        builder.addBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        cullDescriptorLayout = builder.build(device, VK_SHADER_STAGE_COMPUTE_BIT);
    }

    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        cullDescriptorSets[i] = frames[i]._frameDescriptors.allocate(device, cullDescriptorLayout);
        DescriptorWriter writer;
        writer.writeBuffer(0, cullDataBuffers[i].buffer, sizeof(CullData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        writer.writeBuffer(1, instanceBuffer.buffer, trueInstanceCount * sizeof(InstanceData), 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(2, indirectBuffer.buffer, sizeof(DrawIndexedIndirectCommand), 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(3, cullStatsBuffers[i].buffer, sizeof(CullStats), 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(4, visibilityBuffer.buffer, trueInstanceCount * sizeof(uint32_t), 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.updateSet(device, cullDescriptorSets[i]);
    }
}

void Mesh::createInstances() {
    instances.clear();
    instances.reserve(100000);

    const int gridX = 50;
    const int gridY = 40;
    const int gridZ = 50;
    const float spacing = 0.6f;
    const float cubeScale = 0.3f;

    for (int x = 0; x < gridX; x++) {
        for (int y = 0; y < gridY; y++) {
            for (int z = 0; z < gridZ; z++) {
                InstanceData instance{};

                instance.position = glm::vec3(
                    (x - gridX/2) * spacing,
                    (y - gridY/2) * spacing,
                    (z - gridZ/2) * spacing
                );

                instance.scale = cubeScale;
                instances.push_back(instance);
            }
        }
    }

    trueInstanceCount = static_cast<uint32_t>(instances.size());

    std::cout << "Created " << instances.size() << " instances" << std::endl;
    std::cout << "Instance buffer size: " << (instances.size() * sizeof(InstanceData) / (1024.0 * 1024.0)) << " MB" << std::endl;

    size_t bufferSize = instances.size() * sizeof(InstanceData);

    instanceBuffer = createAllocatedBuffer(
        bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );

    AllocatedBuffer staging = createAllocatedBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU
    );

    void* data;
    vmaMapMemory(allocator, staging.allocation, &data);
    memcpy(data, instances.data(), bufferSize);
    vmaUnmapMemory(allocator, staging.allocation);

    immediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy copy{};
        copy.size = bufferSize;
        vkCmdCopyBuffer(cmd, staging.buffer, instanceBuffer.buffer, 1, &copy);
    });

    vmaDestroyBuffer(allocator, staging.buffer, staging.allocation);

    // done with this
    instances.clear();
    instances.shrink_to_fit();
}

void Mesh::createCullBuffers() {
    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        cullDataBuffers[i] = createAllocatedBuffer(sizeof(CullData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        cullStatsBuffers[i] = createAllocatedBuffer(sizeof(CullStats),
   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
   VMA_MEMORY_USAGE_GPU_TO_CPU);

        CullStats initialStats = {0, trueInstanceCount};
        void* data;
        vmaMapMemory(allocator, cullStatsBuffers[i].allocation, &data);
        memcpy(data, &initialStats, sizeof(CullStats));
        vmaUnmapMemory(allocator, cullStatsBuffers[i].allocation);
    }

    size_t visibilityBufferSize = trueInstanceCount * sizeof(uint32_t);
    visibilityBuffer = createAllocatedBuffer(visibilityBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

}

void Mesh::initMeshPipeline() {
    VkShaderModule vertShader { VK_NULL_HANDLE };
    vertShader = loadShader(device, "../shaders/mesh.vert.spv");  // Use instanced shader
    assert(vertShader);

    VkShaderModule fragShader { VK_NULL_HANDLE };
    fragShader = loadShader(device, "../shaders/mesh.frag.spv");
    assert(fragShader);

    // only need PCs for vertex stage
    VkPushConstantRange range;
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    range.offset = 0;
    range.size = sizeof(MeshPushConstants);

    VkPipelineLayoutCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = 0;
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &range;
    info.pSetLayouts = &meshDescriptorLayout;
    info.setLayoutCount = 1;

    VK_CHECK(vkCreatePipelineLayout(device, &info, nullptr, &meshPipelineLayout));

    vertexBindings = {
        {0, sizeof(InstanceData), VK_VERTEX_INPUT_RATE_INSTANCE}
    };

    vertexAttributes = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(InstanceData, position)},
        {1, 0, VK_FORMAT_R32_SFLOAT, offsetof(InstanceData, scale)},
    };

    PipelineBuilder pipelineBuilder;
    pipelineBuilder.pipelineLayout = meshPipelineLayout;
    pipelineBuilder.setShaders(vertShader, fragShader);
    pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    pipelineBuilder.setMultisamplingSampleRate(0.20f);
    pipelineBuilder.disableBlending();
    pipelineBuilder.enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL);
    pipelineBuilder.setColorAttachmentFormat(VK_FORMAT_B8G8R8A8_SRGB);
    pipelineBuilder.setDepthFormat(VK_FORMAT_D32_SFLOAT);

    pipelineBuilder.vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    pipelineBuilder.vertexInputInfo.pNext = nullptr;
    pipelineBuilder.vertexInputInfo.flags = 0;
    pipelineBuilder.vertexInputInfo.vertexBindingDescriptionCount = vertexBindings.size();
    pipelineBuilder.vertexInputInfo.pVertexBindingDescriptions = vertexBindings.data();
    pipelineBuilder.vertexInputInfo.vertexAttributeDescriptionCount = vertexAttributes.size();
    pipelineBuilder.vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes.data();


    meshPipeline = pipelineBuilder.buildPipeline(device);

    vkDestroyShaderModule(device, fragShader, nullptr);
    vkDestroyShaderModule(device, vertShader, nullptr);
}

void Mesh::initCullPipeline() {
    VkShaderModule cullShader;
    cullShader = loadShader(device, "../shaders/cull.comp.glsl.spv");
    assert(cullShader);

    VkPipelineLayoutCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = 0;
    info.pSetLayouts = &cullDescriptorLayout;
    info.setLayoutCount = 1;

    VK_CHECK(vkCreatePipelineLayout(device, &info, nullptr, &cullPipelineLayout));

    VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    stageInfo.pNext = nullptr;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = cullShader;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    pipelineInfo.pNext = nullptr;
    pipelineInfo.layout = cullPipelineLayout;
    pipelineInfo.stage = stageInfo;

    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &cullPipeline));

    vkDestroyShaderModule(device, cullShader, nullptr);
}

void Mesh::loadObj(const char *filePath) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    std::vector<Vertex>           vertices;
    std::vector<uint32_t>         vertexIndices;

    if (!LoadObj(&attrib, &shapes, &materials, &warn, &err, filePath)) {
        throw std::runtime_error(warn + err);
    }

    std::unordered_map<Vertex, uint32_t> uniqueVertices{};

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};

            vertex.position = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };

            if (index.texcoord_index >= 0) {
                vertex.uv_x = attrib.texcoords[2 * index.texcoord_index + 0];
                vertex.uv_y = 1.0f - attrib.texcoords[2 * index.texcoord_index + 1];

            } else {
                vertex.uv_x = 0.0f;
                vertex.uv_y = 0.0f;
            }

            if (index.normal_index >= 0) {
                vertex.normal = {
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]
                };
            } else {
                vertex.normal = {0.0f, 1.0f, 0.0f};
            }

            vertex.color = {1.0f, 1.0f, 1.0f};

            if (!uniqueVertices.contains(vertex)) {
                uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vertex);
            }

            vertexIndices.push_back(uniqueVertices[vertex]);
        }
    }


    size_t vertexBuffersize = vertices.size() * sizeof(Vertex);
    indexCount = static_cast<uint32_t>(vertexIndices.size());
    size_t indexBufferSize = vertexIndices.size() * sizeof(uint32_t);

    vertexBuffer = createAllocatedBuffer(vertexBuffersize,  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    VkBufferDeviceAddressInfo deviceAddressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer = vertexBuffer.buffer };
    vertexBuffer.bufferAddress = vkGetBufferDeviceAddress(device, &deviceAddressInfo);

    indexBuffer = createAllocatedBuffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    AllocatedBuffer vertexStaging = createAllocatedBuffer(vertexBuffersize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    void* vertexData;
    vmaMapMemory(allocator, vertexStaging.allocation, &vertexData);
    memcpy(vertexData, vertices.data(), vertexBuffersize);
    vmaUnmapMemory(allocator, vertexStaging.allocation);

    AllocatedBuffer indexStaging = createAllocatedBuffer(indexBufferSize,
       VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    void* indexData;
    vmaMapMemory(allocator, indexStaging.allocation, &indexData);
    memcpy(indexData, vertexIndices.data(), indexBufferSize);
    vmaUnmapMemory(allocator, indexStaging.allocation);

    immediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy vertexCopy{};
        vertexCopy.size = vertexBuffersize;
        vkCmdCopyBuffer(cmd, vertexStaging.buffer, vertexBuffer.buffer, 1, &vertexCopy);

        VkBufferCopy indexCopy{};
        indexCopy.size = indexBufferSize;
        vkCmdCopyBuffer(cmd, indexStaging.buffer, indexBuffer.buffer, 1, &indexCopy);
    });

    vmaDestroyBuffer(allocator, vertexStaging.buffer, vertexStaging.allocation);
    vmaDestroyBuffer(allocator, indexStaging.buffer, indexStaging.allocation);
}

void Mesh::loadTextureImage(const char *filePath) {
     int texWidth, texHeight, texChannels;

    stbi_uc* pixels = stbi_load(filePath, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    uint64_t imageSize = texWidth * texHeight * 4;
    uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, nullptr);

    void* mappedData;
    vmaMapMemory(allocator, stagingAllocation, &mappedData);
    memcpy(mappedData, pixels, imageSize);
    vmaUnmapMemory(allocator, stagingAllocation);

    stbi_image_free(pixels);

    VkExtent3D imageExtent = {
        static_cast<uint32_t>(texWidth),
        static_cast<uint32_t>(texHeight),
        1
    };

    texImage = createAllocatedImage(imageExtent, VK_FORMAT_R8G8B8A8_SRGB,
           VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
           true);

    immediateSubmit([&](VkCommandBuffer cmd) {
        // Transition entire image (all mip levels) to transfer destination
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texImage.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {
            static_cast<uint32_t>(texWidth),
            static_cast<uint32_t>(texHeight),
            1 };

        vkCmdCopyBufferToImage(cmd, stagingBuffer, texImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        createMipmaps(cmd, texImage.image, VK_FORMAT_R8G8B8A8_SRGB, texWidth, texHeight, mipLevels);
    });

    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
}

void Mesh::createMipmaps(VkCommandBuffer cmd, VkImage image, VkFormat imageFormat,
                        int32_t texWidth, int32_t texHeight, uint32_t mipLevels) {
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, imageFormat, &formatProperties);

    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        throw std::runtime_error("Texture image format does not support linear blitting!");
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i = 1; i < mipLevels; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &barrier);

        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;

        int32_t nextMipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
        int32_t nextMipHeight = mipHeight > 1 ? mipHeight / 2 : 1;

        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {nextMipWidth, nextMipHeight, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(cmd,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &barrier);

        mipWidth = nextMipWidth;
        mipHeight = nextMipHeight;
    }

    // Transition the last mip level
    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, nullptr, 0, nullptr, 1, &barrier);
}

void Mesh::createIndirectBuffer() {
    indirectCommand.indexCount = indexCount;
    indirectCommand.instanceCount = trueInstanceCount;
    indirectCommand.firstIndex = 0;
    indirectCommand.vertexOffset = 0;
    indirectCommand.firstInstance = 0;

    size_t bufferSize = sizeof(DrawIndexedIndirectCommand);

    indirectBuffer = createAllocatedBuffer(
        bufferSize,
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU
    );

    AllocatedBuffer staging = createAllocatedBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU
    );

    void* data;
    vmaMapMemory(allocator, staging.allocation, &data);
    memcpy(data, &indirectCommand, bufferSize);
    vmaUnmapMemory(allocator, staging.allocation);

    immediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy copy{};
        copy.size = bufferSize;
        vkCmdCopyBuffer(cmd, staging.buffer, indirectBuffer.buffer, 1, &copy);
    });

    vmaDestroyBuffer(allocator, staging.buffer, staging.allocation);

}

void Mesh::recordCommands(VkCommandBuffer cmd, uint32_t frameNumber, VkImageView swapchainImageView) {
    uint32_t frameIndex = frameNumber % MAX_FRAMES;
    FrameData& frame = frames[frameIndex];

    beginCommands(cmd, swapchainImageView);

    VkViewport viewport = initViewport(swapchain.swapchainExtent);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor = initScissor(viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline);

    VkDeviceSize offset = 0;

    // send draw params to GPU
    vkCmdBindVertexBuffers(cmd, 0, 1, &instanceBuffer.buffer, &offset);

    vkCmdBindIndexBuffer(cmd, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    pushConstants.worldMatrix = transform;
    pushConstants.vertexBuffer = vertexBuffer.bufferAddress;

    vkCmdPushConstants(cmd, meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
        0, sizeof(MeshPushConstants), &pushConstants);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipelineLayout,
                                   0, 1, &imageDescriptorSets[frameIndex], 0, nullptr);

    // use draw params on GPU to render all blocks.
    // no need to iterate 0 -> object count. one call very nice.
    vkCmdDrawIndexedIndirect(cmd, indirectBuffer.buffer, 0, 1, sizeof(DrawIndexedIndirectCommand));

    endCommands(cmd);
}

void Mesh::drawFrame() {
    uint32_t frameIndex = currentFrame % MAX_FRAMES;
    FrameData& frame = frames[frameIndex];

    camera.processEvent(window);
    camera.velocity *= 0.1f;

    updatePerFrameData(frameIndex);

    VK_CHECK(vkWaitForFences(device, 1, &frame.renderFence, VK_TRUE, UINT64_MAX));

    uint32_t swapchainImageIndex;
    VkResult result = swapchain.acquireNextImage(frame.imgAvailable, swapchainImageIndex);

    VK_CHECK(vkResetFences(device, 1, &frame.renderFence));
    VK_CHECK(vkResetCommandBuffer(frame.commandBuffer, 0));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo));

    vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipeline);
    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                           cullPipelineLayout, 0, 1,
                           &cullDescriptorSets[frameIndex], 0, nullptr);


    uint32_t workgroupCount = (trueInstanceCount + 63) / 64;
    vkCmdDispatch(frame.commandBuffer, workgroupCount, 1, 1);

    VkMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    vkCmdPipelineBarrier(frame.commandBuffer,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                        0, 1, &barrier, 0, nullptr, 0, nullptr);



    transitionImage(frame.commandBuffer, swapchain.images[swapchainImageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    recordCommands(frame.commandBuffer, frameIndex, swapchain.imageViews[swapchainImageIndex]);

    transitionImage(frame.commandBuffer, swapchain.images[swapchainImageIndex],
         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VK_CHECK(vkEndCommandBuffer(frame.commandBuffer));

    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = frame.commandBuffer;

    VkSemaphoreSubmitInfo waitSemaphoreInfo{};
    waitSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitSemaphoreInfo.semaphore = frame.imgAvailable;
    waitSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signalSemaphoreInfo{};
    signalSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalSemaphoreInfo.semaphore = frame.renderComplete;
    signalSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitSemaphoreInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalSemaphoreInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdInfo;

    VK_CHECK(vkQueueSubmit2(graphicsQueue, 1, &submitInfo, frame.renderFence));

    if (currentFrame % 60 == 0) {  // Every 60 frames
        readCullStats(frameIndex);
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &frame.renderComplete;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain.swapchain;
    presentInfo.pImageIndices = &swapchainImageIndex;

    VkResult presentResult = vkQueuePresentKHR(presentQueue, &presentInfo);

    currentFrame++;
}

void Mesh::updateCullData(uint32_t frameIndex) {
    auto data = camera.getFrustumData();

    void* mapped;
    vmaMapMemory(allocator, cullDataBuffers[frameIndex].allocation, &mapped);
    memcpy(mapped, &data, sizeof(CullData));
    vmaUnmapMemory(allocator, cullDataBuffers[frameIndex].allocation);
}

void Mesh::updatePerFrameData(uint32_t frameIndex) {
    camera.update();

    glm::mat4 model = glm::mat4(1.0f);

    glm::mat4 proj = glm::perspective(
        glm::radians(70.0f),
        (float)swapchain.swapchainExtent.width / (float)swapchain.swapchainExtent.height,
        0.1f, 10000.0f
    );

    camera.updateFrustum(proj);
    updateCullData(frameIndex);

    transform = camera.getFrustumData().viewProj * model;
}

void Mesh::readCullStats(uint32_t frameIndex) {
    CullStats stats;
    void* data;
    vmaMapMemory(allocator, cullStatsBuffers[frameIndex].allocation, &data);
    memcpy(&stats, data, sizeof(CullStats));
    vmaUnmapMemory(allocator, cullStatsBuffers[frameIndex].allocation);

    std::cout << "Inside Frustum: " << stats.visibleCount
              << " / " << stats.totalCount
              << " (" << (100.0f * stats.visibleCount / stats.totalCount) << "%)" << std::endl;
}

void Mesh::run() {
    while (!glfwWindowShouldClose(window)) {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            break;
        }

        glfwPollEvents();
        drawFrame();
    }

    vkDeviceWaitIdle(device);
}

Mesh::~Mesh() {
    vkDeviceWaitIdle(device);

    swapchain.cleanup();
    vkDestroyImageView(device, texImage.imageView, nullptr);
    vmaDestroyImage(allocator, texImage.image, texImage.allocation);
    vkDestroyImageView(device, depthImage.imageView, nullptr);
    vmaDestroyImage(allocator, depthImage.image, depthImage.allocation);

    vkDestroySampler(device, texSampler, nullptr);

    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        frames[i]._frameDescriptors.destroy_pools(device);
        vmaDestroyBuffer(allocator, cullDataBuffers[i].buffer, cullDataBuffers[i].allocation);
        vmaDestroyBuffer(allocator, cullStatsBuffers[i].buffer, cullStatsBuffers[i].allocation);

    }
    vmaDestroyBuffer(allocator, vertexBuffer.buffer, vertexBuffer.allocation);
    vmaDestroyBuffer(allocator, indexBuffer.buffer, indexBuffer.allocation);
    vmaDestroyBuffer(allocator, indirectBuffer.buffer, indirectBuffer.allocation);
    vmaDestroyBuffer(allocator, instanceBuffer.buffer, instanceBuffer.allocation);
    vmaDestroyBuffer(allocator, visibilityBuffer.buffer, visibilityBuffer.allocation);

    vkDestroyDescriptorSetLayout(device, meshDescriptorLayout, nullptr);
    vkDestroyPipelineLayout(device, meshPipelineLayout, nullptr);
    vkDestroyPipeline(device, meshPipeline, nullptr);

    vkDestroyDescriptorSetLayout(device, cullDescriptorLayout, nullptr);
    vkDestroyPipelineLayout(device, cullPipelineLayout, nullptr);
    vkDestroyPipeline(device, cullPipeline, nullptr);
}
