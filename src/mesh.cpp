#include "mesh.h"

#include "GLFW/glfw3.h"
#include "../tools/gltfLoader.h"
#include "../tools/inits.h"


#include <iostream>

Mesh::Mesh(uint32_t _width, uint32_t _height, const char* _windowName)
    : Base(_width, _height, _windowName) {

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    initCamera(0.0f, 20.0f, 50.0f);
    initDepthImage();

    loadObj("../assets/barrel/Barrel.obj");
    textureImage = loadTextureImage("../assets/barrel/Barrel_Base_Color.png");

    createInstances();
    createCullBuffers();
    createIndirectCmdBuffer();

    initDescriptorSets();

    initInstancePipeline();
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
        writer.writeImage(0, textureImage.imageView, texSampler,
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
        cullDescriptorLayout = builder.build(device, VK_SHADER_STAGE_COMPUTE_BIT);
    }

    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        cullDescriptorSets[i] = frames[i]._frameDescriptors.allocate(device, cullDescriptorLayout);
        DescriptorWriter writer;
        writer.writeBuffer(0, cullDataBuffers[i].buffer, sizeof(CullData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        writer.writeBuffer(1, instanceBuffer.buffer, sizeof(InstanceData) * trueInstanceCount, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(2, drawCmdBuffer.buffer, sizeof(DrawIndexedIndirectCommand) * trueInstanceCount, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(3, cullStatsBuffers[i].buffer, sizeof(CullStats), 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.updateSet(device, cullDescriptorSets[i]);
    }
}

void Mesh::createInstances() {
    instances.clear();
    instances.reserve(8000);

    const int gridDim = 20;
    const float spacing = 5.0f;
    const float cubeScale = 0.01f;

    for (int x = 0; x < gridDim; x++) {
        for (int y = 0; y < gridDim; y++) {
            for (int z = 0; z < gridDim; z++) {
                InstanceData instance{};

                instance.position = glm::vec3(
                    (x - gridDim/2) * spacing,
                    (y - gridDim/2) * spacing,
                    (z - gridDim/2) * spacing
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

    // useful if number of instances is really high -- save space
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

        CullStats stats {0, 0, trueInstanceCount};
        void* data;
        vmaMapMemory(allocator, cullStatsBuffers[i].allocation, &data);
        memcpy(data, &stats, sizeof(CullStats));
        vmaUnmapMemory(allocator, cullStatsBuffers[i].allocation);
    }
}

void Mesh::initInstancePipeline() {
    VkShaderModule vertShader { VK_NULL_HANDLE };
    vertShader = loadShader(device, "../shaders/mesh.vert.spv");
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
    pipelineBuilder.setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
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

void Mesh::createIndirectCmdBuffer() {
    drawIndirectCmds.resize(trueInstanceCount);
    size_t bufferSize = sizeof(DrawIndexedIndirectCommand) * trueInstanceCount;

    for (uint32_t i = 0; i < trueInstanceCount; i++) {
        drawIndirectCmds[i].indexCount = indexCount;
        drawIndirectCmds[i].instanceCount = 0;
        drawIndirectCmds[i].firstIndex = 0;
        drawIndirectCmds[i].vertexOffset = 0;
        drawIndirectCmds[i].firstInstance = i;
    }

    drawCmdBuffer = createAllocatedBuffer(
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
    memcpy(data, drawIndirectCmds.data(), bufferSize);
    vmaUnmapMemory(allocator, staging.allocation);

    immediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy copy{};
        copy.size = bufferSize;
        vkCmdCopyBuffer(cmd, staging.buffer, drawCmdBuffer.buffer, 1, &copy);

        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
    });

    vmaDestroyBuffer(allocator, staging.buffer, staging.allocation);
}

void Mesh::recordCommands(VkCommandBuffer cmd, uint32_t frameNumber, VkImageView swapchainImageView) {
    uint32_t frameIndex = frameNumber % MAX_FRAMES;

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
    vkCmdDrawIndexedIndirect(cmd, drawCmdBuffer.buffer, 0, trueInstanceCount, sizeof(DrawIndexedIndirectCommand));

    endCommands(cmd);
}

void Mesh::drawFrame() {
    uint32_t frameIndex = currentFrame % MAX_FRAMES;
    FrameData& frame = frames[frameIndex];

    camera.processEvent(window);
    camera.velocity *= 0.01f;

    updatePerFrameData(frameIndex);

    VK_CHECK(vkWaitForFences(device, 1, &frame.renderFence, VK_TRUE, UINT64_MAX));

    uint32_t swapchainImageIndex;
    VkResult result = swapchain.acquireNextImage(frame.imgAvailable, swapchainImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        // think it's out of date or suboptimal . . . anyways . . . accommodate resizes
    }

    VK_CHECK(vkResetFences(device, 1, &frame.renderFence));
    VK_CHECK(vkResetCommandBuffer(frame.commandBuffer, 0));

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo));

    vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipeline);
    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                           cullPipelineLayout, 0, 1,
                           &cullDescriptorSets[frameIndex], 0, nullptr);

    // can change # depending on capabilities . . . 64, 128, 256
    uint32_t optimalLocalSize = 128;
    uint32_t workgroupCount = (trueInstanceCount + optimalLocalSize - 1) / optimalLocalSize;
    vkCmdDispatch(frame.commandBuffer, workgroupCount, 1, 1);

    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

    vkCmdPipelineBarrier(frame.commandBuffer,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                        0, 1, &barrier,
                        0, nullptr,
                        0, nullptr);

    transitionImage(frame.commandBuffer, swapchain.images[swapchainImageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    recordCommands(frame.commandBuffer, frameIndex, swapchain.imageViews[swapchainImageIndex]);

    transitionImage(frame.commandBuffer, swapchain.images[swapchainImageIndex],
         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VK_CHECK(vkEndCommandBuffer(frame.commandBuffer));

    VkCommandBufferSubmitInfo cmdInfo = {};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = frame.commandBuffer;

    VkSemaphoreSubmitInfo waitSemaphoreInfo = {};
    waitSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitSemaphoreInfo.semaphore = frame.imgAvailable;
    waitSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signalSemaphoreInfo = {};
    signalSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalSemaphoreInfo.semaphore = frame.renderComplete;
    signalSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

    VkSubmitInfo2 submitInfo = createSubmitInfo(&cmdInfo, &signalSemaphoreInfo, &waitSemaphoreInfo);

    VK_CHECK(vkQueueSubmit2(graphicsQueue, 1, &submitInfo, frame.renderFence));

    if (currentFrame % 1000 == 0) {
        readCullStats(frameIndex);
    }

    VkPresentInfoKHR presentInfo = getPresentInfoKHR(&frame.renderComplete, &swapchain.swapchain, swapchainImageIndex);

    VkResult presentResult = vkQueuePresentKHR(presentQueue, &presentInfo);

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR) {
        // need to handle swapchain resizes
    }

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
    vkDestroyImageView(device, textureImage.imageView, nullptr);
    vmaDestroyImage(allocator, textureImage.image, textureImage.allocation);
    vkDestroyImageView(device, depthImage.imageView, nullptr);
    vmaDestroyImage(allocator, depthImage.image, depthImage.allocation);

    vkDestroySampler(device, texSampler, nullptr);

    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        frames[i]._frameDescriptors.destroyPools(device);
        vmaDestroyBuffer(allocator, cullDataBuffers[i].buffer, cullDataBuffers[i].allocation);
        vmaDestroyBuffer(allocator, cullStatsBuffers[i].buffer, cullStatsBuffers[i].allocation);
    }

    vmaDestroyBuffer(allocator, vertexBuffer.buffer, vertexBuffer.allocation);
    vmaDestroyBuffer(allocator, indexBuffer.buffer, indexBuffer.allocation);
    vmaDestroyBuffer(allocator, drawCmdBuffer.buffer, drawCmdBuffer.allocation);
    vmaDestroyBuffer(allocator, instanceBuffer.buffer, instanceBuffer.allocation);

    vkDestroyDescriptorSetLayout(device, meshDescriptorLayout, nullptr);
    vkDestroyPipelineLayout(device, meshPipelineLayout, nullptr);
    vkDestroyPipeline(device, meshPipeline, nullptr);

    vkDestroyDescriptorSetLayout(device, cullDescriptorLayout, nullptr);
    vkDestroyPipelineLayout(device, cullPipelineLayout, nullptr);
    vkDestroyPipeline(device, cullPipeline, nullptr);
}
