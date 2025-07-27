#include "base.h"
#include "device.h"
#include "../tools/debug.h"
#include "../tools/utils.h"
#include <fstream>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>




Base::Base(uint32_t _width, uint32_t _height, const char *_windowName)
    : windowExtent({_width, _height}),
      windowName(_windowName)
{
   prepare();
}

void Base::prepare() {
    initWindow();
    initInstance();
    initVulkan();
    initAllocator();
    createCommandPool();
    initFrameData();
    initImmStructures();
}

void Base::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window = glfwCreateWindow(windowExtent.width, windowExtent.height, windowName, nullptr, nullptr);
    assert(window);

}

void Base::initInstance() {
    instance = createInstance();

    debugMessenger = registerDebugCallback(instance);
}

void Base::initVulkan() {
    physicalDevice = choosePhysicalDevice(instance);

    VK_CHECK(glfwCreateWindowSurface(instance, window, nullptr, &surface));

    indices = findQueueFamilies(physicalDevice, surface);

    device = createLogicalDevice(physicalDevice, indices);

    vkGetDeviceQueue(device, indices.graphicsFamily, 0, &graphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily, 0, &presentQueue);

    swapchain.setContext(instance, physicalDevice, device, surface, window);
    swapchain.create(windowExtent.width, windowExtent.height, indices);

    initialized = true;
}

void Base::initAllocator() {
    VmaAllocatorCreateInfo allocInfo = {};
    allocInfo.physicalDevice = physicalDevice;
    allocInfo.device = device;
    allocInfo.instance = instance;
    allocInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT | VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;

#ifndef NDEBUG
    allocInfo.flags |= VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;
#endif

    VK_CHECK(vmaCreateAllocator(&allocInfo, &allocator));
}

void Base::initDepthImage() {
    VkExtent3D depthImageExtent = {
        swapchain.swapchainExtent.width,
        swapchain.swapchainExtent.height,
        1
    };

    depthImage = createAllocatedImage(depthImageExtent, VK_FORMAT_D32_SFLOAT,
                                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, false);

    immediateSubmit([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = depthImage.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier);
    });
}

void Base::initFrameData() {
    VkCommandBufferAllocateInfo cmdBufferAllocInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdBufferAllocInfo.pNext = nullptr;
    cmdBufferAllocInfo.commandPool = commandPool;
    cmdBufferAllocInfo.commandBufferCount = 1;
    cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frameSizes = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1.0f },
    };

    for (auto& frame : frames) {
        vkAllocateCommandBuffers(device, &cmdBufferAllocInfo, &frame.commandBuffer);
        frame.renderFence = createFence(device, VK_FENCE_CREATE_SIGNALED_BIT);
        frame.imgAvailable = createSemaphore(device);
        frame.renderComplete = createSemaphore(device);

        frame._frameDescriptors.init(device, 10, frameSizes);
    }
}

void Base::initImmStructures() {
    immFence = createFence(device);

    VkCommandPoolCreateInfo cmdPoolInfo = {};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.queueFamilyIndex = indices.graphicsFamily;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VK_CHECK(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &immCommandPool));

    VkCommandBufferAllocateInfo cmdBufferAllocInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdBufferAllocInfo.pNext = nullptr;
    cmdBufferAllocInfo.commandPool = immCommandPool;
    cmdBufferAllocInfo.commandBufferCount = 1;
    cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VK_CHECK(vkAllocateCommandBuffers(device, &cmdBufferAllocInfo, &immCommandBuffer));

}

void Base::initCamera(float x, float y, float z) {
    camera.position = glm::vec3(x, y, z);
    camera.initialPosition = glm::vec3(x, y, z);
    camera.velocity = glm::vec3(0.0f);
    camera.pitch = 0.0f;
    camera.yaw = 0.0f;
}

void Base::createCommandPool() {
    VkCommandPoolCreateInfo cmdPoolInfo = {};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.queueFamilyIndex = indices.graphicsFamily;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VK_CHECK(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &commandPool));
}

void Base::createCommandBuffers() {
    drawCommandBuffers.resize(swapchain.imageCount);

    VkCommandBufferAllocateInfo cmdBufferAllocInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdBufferAllocInfo.pNext = nullptr;
    cmdBufferAllocInfo.commandPool = commandPool;
    cmdBufferAllocInfo.commandBufferCount = 1;
    cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    vkAllocateCommandBuffers(device, &cmdBufferAllocInfo, drawCommandBuffers.data());
}

MeshBuffers Base::loadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices) {
    const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
    const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

    MeshBuffers newMeshBuffer;
    newMeshBuffer.vertexBuffer = createAllocatedBuffer(vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    assert(newMeshBuffer.vertexBuffer.allocation);

    VkBufferDeviceAddressInfo deviceAddressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer = newMeshBuffer.vertexBuffer.buffer };
    newMeshBuffer.vertexBufferAddress = vkGetBufferDeviceAddress(device, &deviceAddressInfo);

    newMeshBuffer.indexBuffer = createAllocatedBuffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    AllocatedBuffer staging  = createAllocatedBuffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    void* data = staging.allocation->GetMappedData();
    memcpy(data, vertices.data(), vertexBufferSize);
    memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

    immediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy vertexCopy { 0 };
        vertexCopy.dstOffset = 0;
        vertexCopy.srcOffset = 0;
        vertexCopy.size = vertexBufferSize;

        vkCmdCopyBuffer(cmd, staging.buffer, newMeshBuffer.vertexBuffer.buffer, 1, &vertexCopy);

        VkBufferCopy indexCopy{ 0 };
        indexCopy.dstOffset = 0;
        indexCopy.srcOffset = vertexBufferSize;
        indexCopy.size = indexBufferSize;

        vkCmdCopyBuffer(cmd, staging.buffer, newMeshBuffer.indexBuffer.buffer, 1, &indexCopy);
    });

    destroyAllocatedBuffer(staging.buffer, staging.allocation);

    return newMeshBuffer;
}

AllocatedImage Base::createAllocatedImage(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) {
    AllocatedImage newImage;
    newImage.imageFormat = format;
    newImage.imageExtent = size;

    VkImageCreateInfo img_info = imageCreateInfo(format, usage, size);
    if (mipmapped) {
        img_info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
    } else {
        img_info.mipLevels = 1;
    }

    VmaAllocationCreateInfo allocinfo = {};
    allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vmaCreateImage(allocator, &img_info, &allocinfo, &newImage.image, &newImage.allocation, nullptr));

    VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
    if (format == VK_FORMAT_D32_SFLOAT) {
        aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    VkImageViewCreateInfo view_info = imageviewCreateInfo(format, newImage.image, aspectFlag);
    view_info.subresourceRange.levelCount = img_info.mipLevels;

    VK_CHECK(vkCreateImageView(device, &view_info, nullptr, &newImage.imageView));

    return newImage;
}

AllocatedBuffer Base::createAllocatedBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) {
    VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.pNext = nullptr;
    bufferInfo.size = allocSize;
    bufferInfo.usage = usage;

    VmaAllocationCreateInfo vmaAllocInfo = {};
    vmaAllocInfo.usage = memoryUsage;
    vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    AllocatedBuffer newBuffer;

    VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &vmaAllocInfo, &newBuffer.buffer, &newBuffer.allocation,
        &newBuffer.info));

    return newBuffer;
}

void Base::destroyAllocatedImage(VkImage image, VmaAllocation allocation) {
    vmaDestroyImage(allocator, image, allocation);
}

void Base::destroyAllocatedBuffer(VkBuffer buffer, VmaAllocation allocation) {
    vmaDestroyBuffer(allocator, buffer, allocation);
}

void Base::immediateSubmit(std::function<void(VkCommandBuffer cmd)> &&function) {
    VK_CHECK(vkResetFences(device, 1, &immFence));
    VK_CHECK(vkResetCommandBuffer(immCommandBuffer, 0));

    VkCommandBufferBeginInfo cmdBeginInfo = commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(immCommandBuffer, &cmdBeginInfo));

    function(immCommandBuffer);

    VK_CHECK(vkEndCommandBuffer(immCommandBuffer));

    VkCommandBufferSubmitInfo cmdInfo = submitCommandBufferInfo(immCommandBuffer);
    VkSubmitInfo2 submit = createSubmitInfo(&cmdInfo, nullptr, nullptr);

    VK_CHECK(vkQueueSubmit2(graphicsQueue, 1, &submit, immFence));
    VK_CHECK(vkWaitForFences(device, 1, &immFence, true, 9999999999));
}

void Base::beginCommands(VkCommandBuffer cmd, VkImageView swapchainImageView) {
    VkClearValue colorClear;
    colorClear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkClearValue depthClear;
    depthClear.depthStencil = {1.0f, 0};

    VkRenderingAttachmentInfo colorAttachment = getColorAttachment(swapchainImageView, &colorClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo depthAttachment = getDepthAttachment(depthImage.imageView, &depthClear, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingInfo renderInfo = getRenderingInfo(swapchain.swapchainExtent, &colorAttachment, &depthAttachment);

    vkCmdBeginRendering(cmd, &renderInfo);
}

void Base::endCommands(VkCommandBuffer cmd) {
    vkCmdEndRendering(cmd);
}

VkShaderModule Base::loadShader(VkDevice device, const char *filePath) {
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        std::cerr << "Could not open file" << std::endl;
        exit(1);
    }

    size_t fileSize = (size_t)file.tellg();

    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

    file.seekg(0);
    file.read((char*)buffer.data(), fileSize);
    file.close();

    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.pNext = nullptr;
    createInfo.codeSize = buffer.size() * sizeof(uint32_t);
    createInfo.pCode = buffer.data();

    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule));

    return shaderModule;
}

Base::~Base() {
    vkDeviceWaitIdle(device);


    for (auto& frame : frames) {
        vkDestroyFence(device, frame.renderFence, nullptr);
        vkDestroySemaphore(device, frame.imgAvailable, nullptr);
        vkDestroySemaphore(device, frame.renderComplete, nullptr);
        frame._frameDescriptors.destroy_pools(device);
    }

    vmaDestroyAllocator(allocator);

    vkDestroyFence(device, immFence, nullptr);
    vkDestroyCommandPool(device, immCommandPool, nullptr);

    vkDestroyCommandPool(device, commandPool, nullptr);

    vkDestroyDevice(device, nullptr);
    destroyDebugMessenger(instance, debugMessenger);
    vkDestroyInstance(instance, nullptr);

    glfwDestroyWindow(window);
    glfwTerminate();
}

