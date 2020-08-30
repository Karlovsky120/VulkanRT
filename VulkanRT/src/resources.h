#pragma once

#include "common.h"

#pragma warning(push, 0)
#define VK_ENABLE_BETA_EXTENSIONS
#include "volk.h"

#include <cassert>
#include <vector>
#pragma warning(pop)

VkImage createImage(const VkDevice device, const VkExtent2D imageSize, const VkImageUsageFlags imageUsageFlags, const VkFormat imageFormat);

VkImageView createImageView(const VkDevice device, const VkImage image, const VkFormat format, const VkImageAspectFlags aspectMask);

VkBuffer createBuffer(const VkDevice device, const VkDeviceSize bufferSize, const VkBufferUsageFlags bufferUsageFlags);

template <typename T>
void uploadToDeviceLocalBuffer(const VkDevice device, const std::vector<T>& data, const VkBuffer stagingBuffer, const VkDeviceMemory stagingBufferMemory,
                               const VkBuffer deviceBuffer, const VkCommandPool transferCommandPool, const VkQueue queue) {
    uint32_t bufferSize = sizeof(T) * static_cast<uint32_t>(data.size());

    void* stagingBufferPointer;
    VK_CHECK(vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &stagingBufferPointer));
    memcpy(stagingBufferPointer, data.data(), bufferSize);
    vkUnmapMemory(device, stagingBufferMemory);

    VkCommandBufferAllocateInfo transferCommandBufferAllocateInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    transferCommandBufferAllocateInfo.commandPool                 = transferCommandPool;
    transferCommandBufferAllocateInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    transferCommandBufferAllocateInfo.commandBufferCount          = 1;

    VkCommandBuffer transferCommandBuffer = 0;
    VK_CHECK(vkAllocateCommandBuffers(device, &transferCommandBufferAllocateInfo, &transferCommandBuffer));

    VkCommandBufferBeginInfo transferCommandBufferBeginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(transferCommandBuffer, &transferCommandBufferBeginInfo));

    VkBufferCopy bufferCopy = {};
    bufferCopy.srcOffset    = 0;
    bufferCopy.dstOffset    = 0;
    bufferCopy.size         = bufferSize;
    vkCmdCopyBuffer(transferCommandBuffer, stagingBuffer, deviceBuffer, 1, &bufferCopy);
    VK_CHECK(vkEndCommandBuffer(transferCommandBuffer));

    VkSubmitInfo transferSubmitInfo       = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    transferSubmitInfo.commandBufferCount = 1;
    transferSubmitInfo.pCommandBuffers    = &transferCommandBuffer;

    VK_CHECK(vkQueueSubmit(queue, 1, &transferSubmitInfo, VK_NULL_HANDLE));
    vkDeviceWaitIdle(device);

    vkFreeCommandBuffers(device, transferCommandPool, 1, &transferCommandBuffer);
}

VkDeviceMemory allocateVulkanObjectMemory(const VkDevice device, const VkMemoryRequirements memoryRequirements, const VkMemoryPropertyFlags memoryPropertyFlags,
                                          const VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties);