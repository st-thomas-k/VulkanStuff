#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "../tools/types.h"

#define API_VERSION VK_API_VERSION_1_4

VkInstance createInstance();
VkPhysicalDevice choosePhysicalDevice(VkInstance instance);
QueueFamilyIndices findQueueFamilies(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);
VkDevice createLogicalDevice(VkPhysicalDevice physicalDevice, QueueFamilyIndices indices);