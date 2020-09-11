#include "swapchain.h"

#pragma warning(push, 0)
#define GLFW_INCLUDE_VULKAN
#include "glfw3.h"
#pragma warning(pop)

#include <stdexcept>

bool surfaceFormatSupported(const VkPhysicalDevice physicalDevice, const VkSurfaceKHR surface, const VkSurfaceFormatKHR& desiredSurfaceFormat) {
    uint32_t surfaceFormatsCount;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatsCount, 0));

    std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatsCount);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatsCount, surfaceFormats.data()));

    for (VkSurfaceFormatKHR surfaceFormat : surfaceFormats) {
        if (surfaceFormat.format == desiredSurfaceFormat.format && surfaceFormat.colorSpace == desiredSurfaceFormat.colorSpace) {
            return true;
        }
    }

    return false;
}

VkExtent2D getSurfaceExtent(GLFWwindow* window, const VkSurfaceCapabilitiesKHR surfaceCapabilities) {
    if (surfaceCapabilities.currentExtent.width != UINT32_MAX) {
        return surfaceCapabilities.currentExtent;
    }

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
}

VkPresentModeKHR getPresentMode(const VkPhysicalDevice physicalDevice, const VkSurfaceKHR surface) {
    uint32_t presentModesCount;
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModesCount, 0));

    std::vector<VkPresentModeKHR> presentModes(presentModesCount);
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModesCount, presentModes.data()));

    bool immediatePresentModeSupported = false;
    for (VkPresentModeKHR presentMode : presentModes) {
        immediatePresentModeSupported = immediatePresentModeSupported || presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR;
    }

    return immediatePresentModeSupported ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR;
}

uint32_t getSwapchainImageCount(const VkSurfaceCapabilitiesKHR surfaceCapabilities) {
    if (surfaceCapabilities.maxImageCount < 2 && surfaceCapabilities.maxImageCount != 0) {
        throw std::runtime_error("Couldn't get enough swapchain images!");
    }

    uint32_t imageCount = surfaceCapabilities.minImageCount + 1;
    if (surfaceCapabilities.maxImageCount > 0 && imageCount > surfaceCapabilities.maxImageCount) {
        imageCount = surfaceCapabilities.minImageCount;
    }

    return imageCount;
}

VkSwapchainKHR createSwapchain(const VkDevice device, const VkSurfaceKHR surface, const VkSurfaceFormatKHR surfaceFormat, const VkPresentModeKHR presentMode,
                               const uint32_t imageCount, const uint32_t graphicsQueueFamilyIndex, const VkExtent2D imageExtent,
                               const VkSwapchainKHR oldSwapchain) {
    VkSwapchainCreateInfoKHR swapchainCreateInfo = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchainCreateInfo.surface                  = surface;
    swapchainCreateInfo.minImageCount            = imageCount;
    swapchainCreateInfo.queueFamilyIndexCount    = 1;
    swapchainCreateInfo.pQueueFamilyIndices      = &graphicsQueueFamilyIndex;
    swapchainCreateInfo.preTransform             = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchainCreateInfo.presentMode              = presentMode;
    swapchainCreateInfo.imageUsage               = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    swapchainCreateInfo.imageFormat              = surfaceFormat.format;
    swapchainCreateInfo.imageColorSpace          = surfaceFormat.colorSpace;
    swapchainCreateInfo.imageExtent              = imageExtent;
    swapchainCreateInfo.imageArrayLayers         = 1;
    swapchainCreateInfo.imageSharingMode         = VK_SHARING_MODE_EXCLUSIVE;
    swapchainCreateInfo.compositeAlpha           = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainCreateInfo.oldSwapchain             = oldSwapchain;

    VkSwapchainKHR swapchain = 0;
    VK_CHECK(vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain));

    return swapchain;
}

std::vector<VkImageView> getSwapchainImageViews(const VkDevice device, std::vector<VkImage>& swapchainImages, const VkFormat surfaceFormat) {
    std::vector<VkImageView> swapchainImageViews(swapchainImages.size());

    VkImageViewCreateInfo imageViewCreateInfo           = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    imageViewCreateInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format                          = surfaceFormat;
    imageViewCreateInfo.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCreateInfo.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCreateInfo.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCreateInfo.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCreateInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCreateInfo.subresourceRange.baseMipLevel   = 0;
    imageViewCreateInfo.subresourceRange.levelCount     = 1;
    imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    imageViewCreateInfo.subresourceRange.layerCount     = 1;

    for (size_t i = 0; i < swapchainImages.size(); ++i) {
        imageViewCreateInfo.image = swapchainImages[i];
        VK_CHECK(vkCreateImageView(device, &imageViewCreateInfo, nullptr, &swapchainImageViews[i]));
    }

    return swapchainImageViews;
}
