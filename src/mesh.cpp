#include "mesh.h"

#include "GLFW/glfw3.h"
#include "../tools/gltfLoader.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

Mesh::Mesh(uint32_t _width, uint32_t _height, const char* _windowName)
    : Base(_width, _height, _windowName) {

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    initCamera(0.0f, 3.0f, 5.0f);
    initDepthImage();
    createDescriptors();
    initMeshPipeline();

    loadObj("../assets/grass/Grass_Block.obj");
    loadTextureImage("../assets/grass/Grass_Block_TEX.png");
    initDescriptorSets();
    createIndirectBuffer();
}

void Mesh::createDescriptors() {
    {
        DescriptorLayout builder;
        builder.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        meshDescriptorLayout = builder.build(device, VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    VkSamplerCreateInfo sampler = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.mipLodBias = 0.0f;
    sampler.anisotropyEnable = VK_TRUE;
    sampler.maxAnisotropy = 16.0f;
    sampler.compareEnable = VK_FALSE;
    sampler.compareOp = VK_COMPARE_OP_ALWAYS;

    vkCreateSampler(device, &sampler, nullptr, &texSampler);
}

void Mesh::initDescriptorSets() {
    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        imageDescriptorSets[i] = frames[i]._frameDescriptors.allocate(device, meshDescriptorLayout);
        DescriptorWriter writer;
        writer.writeImage(0, texImage.imageView, texSampler,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        writer.updateSet(device, imageDescriptorSets[i]);
    }
}

void Mesh::initMeshPipeline() {
    VkShaderModule vertShader { VK_NULL_HANDLE };
    vertShader = loadShader(device, "../shaders/mesh.vert.spv");
    assert(vertShader);

    VkShaderModule fragShader { VK_NULL_HANDLE };
    fragShader = loadShader(device, "../shaders/mesh.frag.spv");
    assert(fragShader);

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

    PipelineBuilder pipelineBuilder;
    pipelineBuilder.pipelineLayout = meshPipelineLayout;
    pipelineBuilder.setShaders(vertShader, fragShader);
    pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.setMultisamplingNone();
    pipelineBuilder.disableBlending();
    pipelineBuilder.enableDepthTest(true, VK_COMPARE_OP_LESS);
    pipelineBuilder.setColorAttachmentFormat(VK_FORMAT_B8G8R8A8_SRGB);
    pipelineBuilder.setDepthFormat(VK_FORMAT_D32_SFLOAT);

    meshPipeline = pipelineBuilder.buildPipeline(device);

    vkDestroyShaderModule(device, fragShader, nullptr);
    vkDestroyShaderModule(device, vertShader, nullptr);
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
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false);

    immediateSubmit([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texImage.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
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
        region.imageExtent = {
            static_cast<uint32_t>(texWidth),
            static_cast<uint32_t>(texHeight),
            1 };

        vkCmdCopyBufferToImage(cmd, stagingBuffer, texImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier);
    });

    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
}

void Mesh::createIndirectBuffer() {
    indirectCommand.indexCount = indexCount;
    indirectCommand.instanceCount = 1;
    indirectCommand.firstIndex = 0;
    indirectCommand.vertexOffset = 0;
    indirectCommand.firstInstance = 0;

    size_t bufferSize = sizeof(DrawIndexedIndirectCommand);

    indirectBuffer = createAllocatedBuffer(
        bufferSize,
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
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

    vkCmdBindIndexBuffer(cmd, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    pushConstants.worldMatrix = transform;
    pushConstants.vertexBuffer = vertexBuffer.bufferAddress;

    vkCmdPushConstants(cmd, meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
        0, sizeof(MeshPushConstants), &pushConstants);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipelineLayout,
                                   0, 1, &imageDescriptorSets[frameIndex], 0, nullptr);

    vkCmdDrawIndexedIndirect(cmd, indirectBuffer.buffer, 0, 1, sizeof(DrawIndexedIndirectCommand));

    endCommands(cmd);
}

void Mesh::drawFrame() {
    auto frameStart = std::chrono::high_resolution_clock::now();

    uint32_t frameIndex = currentFrame % MAX_FRAMES;
    FrameData& frame = frames[frameIndex];

    auto currentTime = steady_clock::now();
    float deltaTime = duration<float>(currentTime - lastFrameTime).count();
    lastFrameTime = currentTime;

    camera.processEvent(window);
    camera.velocity *= 0.001f;
    camera.update();

    VK_CHECK(vkWaitForFences(device, 1, &frame.renderFence, VK_TRUE, UINT64_MAX));

    uint32_t swapchainImageIndex;
    VkResult result = swapchain.acquireNextImage(frame.imgAvailable, swapchainImageIndex);

    VK_CHECK(vkResetFences(device, 1, &frame.renderFence));
    VK_CHECK(vkResetCommandBuffer(frame.commandBuffer, 0));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo));

    transitionImage(frame.commandBuffer, swapchain.images[swapchainImageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    updateTransformMatrix();

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

void Mesh::updateTransformMatrix() {
    glm::mat4 model = glm::mat4(1.0f);

    glm::mat4 view = camera.getViewMatrix();

    glm::mat4 proj = glm::perspective(
        glm::radians(70.0f),
        (float)swapchain.swapchainExtent.width / (float)swapchain.swapchainExtent.height,
        0.1f, 10000.0f
    );

    proj[1][1] *= -1;

    transform = proj * view * model;
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

    vmaDestroyBuffer(allocator, vertexBuffer.buffer, vertexBuffer.allocation);
    vmaDestroyBuffer(allocator, indexBuffer.buffer, indexBuffer.allocation);
    vmaDestroyBuffer(allocator, indirectBuffer.buffer, indirectBuffer.allocation);

    vkDestroyDescriptorSetLayout(device, meshDescriptorLayout, nullptr);
    vkDestroyPipelineLayout(device, meshPipelineLayout, nullptr);
    vkDestroyPipeline(device, meshPipeline, nullptr);
}
