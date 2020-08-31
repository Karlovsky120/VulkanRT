#include "resources.h"

#pragma warning(push, 0)
#include <stdexcept>
#pragma warning(pop)

VkImage createImage(const VkDevice device, const VkExtent2D imageSize, const VkImageUsageFlags imageUsageFlags, const VkFormat imageFormat) {
    VkImageCreateInfo imageCreateInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageCreateInfo.imageType         = VK_IMAGE_TYPE_2D;
    imageCreateInfo.usage             = imageUsageFlags;
    imageCreateInfo.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.format            = imageFormat;
    imageCreateInfo.extent            = {imageSize.width, imageSize.height, 1};
    imageCreateInfo.samples           = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling            = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.mipLevels         = 1;
    imageCreateInfo.arrayLayers       = 1;

    VkImage image = 0;
    VK_CHECK(vkCreateImage(device, &imageCreateInfo, nullptr, &image));

    return image;
}

VkImageView createImageView(const VkDevice device, const VkImage image, const VkFormat format, const VkImageAspectFlags aspectMask) {
    VkImageViewCreateInfo imageViewCreateInfo           = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    imageViewCreateInfo.image                           = image;
    imageViewCreateInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format                          = format;
    imageViewCreateInfo.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCreateInfo.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCreateInfo.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCreateInfo.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCreateInfo.subresourceRange.aspectMask     = aspectMask;
    imageViewCreateInfo.subresourceRange.baseMipLevel   = 0;
    imageViewCreateInfo.subresourceRange.levelCount     = 1;
    imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    imageViewCreateInfo.subresourceRange.layerCount     = 1;

    VkImageView imageView = 0;
    VK_CHECK(vkCreateImageView(device, &imageViewCreateInfo, nullptr, &imageView));

    return imageView;
}

VkBuffer createBuffer(const VkDevice device, const VkDeviceSize bufferSize, const VkBufferUsageFlags bufferUsageFlags) {
    VkBufferCreateInfo bufferCreateInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferCreateInfo.size               = bufferSize;
    bufferCreateInfo.usage              = bufferUsageFlags;
    bufferCreateInfo.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer = 0;
    VK_CHECK(vkCreateBuffer(device, &bufferCreateInfo, nullptr, &buffer));

    return buffer;
}

Buffer createBuffer(const VkDevice device, const VkDeviceSize bufferSize, const VkBufferUsageFlags bufferUsageFlags,
                    const VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties, const VkMemoryPropertyFlags memoryPropertyFlags,
                    const VkMemoryAllocateFlags memoryAllocateFlags) {

    Buffer buffer;
    buffer.buffer = createBuffer(device, bufferSize, bufferUsageFlags);

    VkMemoryRequirements memoryRequirements = {};
    vkGetBufferMemoryRequirements(device, buffer.buffer, &memoryRequirements);

    buffer.memory = allocateVulkanObjectMemory(device, memoryRequirements, physicalDeviceMemoryProperties, memoryPropertyFlags, memoryAllocateFlags);
    vkBindBufferMemory(device, buffer.buffer, buffer.memory, 0);

    if (memoryPropertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT && memoryAllocateFlags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfo deviceAddressInfo = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        deviceAddressInfo.buffer = buffer.buffer;

        buffer.deviceAddress = vkGetBufferDeviceAddress(device, &deviceAddressInfo);
    }

    return buffer;
}

uint32_t findMemoryType(const VkPhysicalDeviceMemoryProperties& physicalDeviceMemoryProperties, const uint32_t memoryTypeBits,
                        const VkMemoryPropertyFlags memoryPropertyFlags) {
    uint32_t memoryType = UINT32_MAX;
    for (uint32_t i = 0; i < physicalDeviceMemoryProperties.memoryTypeCount; ++i) {
        bool memoryIsOfRequiredType        = memoryTypeBits & (1 << i);
        bool memoryHasDesiredPropertyFlags = (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & memoryPropertyFlags) == memoryPropertyFlags;

        if (memoryIsOfRequiredType && memoryHasDesiredPropertyFlags) {
            memoryType = i;
            break;
        }
    }

    if (memoryType == UINT32_MAX) {
        throw std::runtime_error("Couldn't find memory type for depth image!");
    }

    return memoryType;
}

VkDeviceMemory allocateVulkanObjectMemory(const VkDevice device, const VkMemoryRequirements& memoryRequirements,
                                          const VkPhysicalDeviceMemoryProperties& physicalDeviceMemoryProperties,
                                          const VkMemoryPropertyFlags memoryPropertyFlags, const VkMemoryAllocateFlags memoryAllocateFlags) {

    uint32_t memoryType = findMemoryType(physicalDeviceMemoryProperties, memoryRequirements.memoryTypeBits, memoryPropertyFlags);

    VkMemoryAllocateInfo memoryAllocateInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memoryAllocateInfo.allocationSize       = memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex      = memoryType;

    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    if (memoryAllocateFlags != 0) {
        memoryAllocateFlagsInfo.flags = memoryAllocateFlags;
        memoryAllocateInfo.pNext      = &memoryAllocateFlagsInfo;
    }

    VkDeviceMemory memory = 0;
    VK_CHECK(vkAllocateMemory(device, &memoryAllocateInfo, nullptr, &memory));

    return memory;
}
