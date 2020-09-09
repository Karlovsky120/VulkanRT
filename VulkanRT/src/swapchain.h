#pragma once

#include "common.h"

#pragma warning(push, 0)
#define VK_ENABLE_BETA_EXTENSIONS
#include "volk.h"
#pragma warning(pop)

#include <vector>

struct GLFWwindow;

bool surfaceFormatSupported(const VkPhysicalDevice physicalDevice, const VkSurfaceKHR surface, const VkSurfaceFormatKHR& desiredSurfaceFormat);

VkExtent2D getSurfaceExtent(GLFWwindow* window, const VkSurfaceCapabilitiesKHR surfaceCapabilities);

VkPresentModeKHR getPresentMode(const VkPhysicalDevice physicalDevice, const VkSurfaceKHR surface);

uint32_t getSwapchainImageCount(const VkSurfaceCapabilitiesKHR surfaceCapabilities);

VkSwapchainKHR createSwapchain(const VkDevice device, const VkSurfaceKHR surface, const VkSurfaceFormatKHR surfaceFormat, const VkPresentModeKHR presentMode,
                               const uint32_t imageCount, const uint32_t graphicsQueueFamilyIndex, const VkExtent2D imageExtent,
                               const VkSwapchainKHR oldSwapchain);

std::vector<VkImageView> getSwapchainImageViews(const VkDevice device, std::vector<VkImage>& swapchainImages, const VkFormat surfaceFormat);
