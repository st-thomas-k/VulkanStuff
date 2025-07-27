#pragma once
#include "../tools/utils.h"
#include "GLFW/glfw3.h"

class Swapchain {
private:
        VkInstance               instance       { VK_NULL_HANDLE };
        VkDevice                 device         { VK_NULL_HANDLE };
        VkPhysicalDevice         physicalDevice { VK_NULL_HANDLE };
        VkSurfaceKHR             surface        { VK_NULL_HANDLE };
        GLFWwindow*              window;

        bool                     initialized    { false };
public:
        VkSwapchainKHR           swapchain  { VK_NULL_HANDLE };
        VkExtent2D               swapchainExtent {};
        std::vector<VkImage>     images {};
        std::vector<VkImageView> imageViews {};
        uint32_t                 imageCount { 0 };

        void setContext(VkInstance _instance, VkPhysicalDevice _pDevice, VkDevice _device, VkSurfaceKHR _surface, GLFWwindow* _window);

        void create(uint32_t& width, uint32_t& height, QueueFamilyIndices indices, VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);

        void cleanup();
        VkResult acquireNextImage(VkSemaphore presentSemaphore, uint32_t& imageIndex);
        VkResult queuePresent(VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore = VK_NULL_HANDLE);
};