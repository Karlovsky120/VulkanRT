#pragma once

#include "common.h"

#include "resources.h"

#pragma warning(push, 0)
#define VK_ENABLE_BETA_EXTENSIONS
#include "volk.h"
#pragma warning(pop)

struct AccelerationStructure {
    VkAccelerationStructureKHR accelerationStructure = VK_NULL_HANDLE;
    VkDeviceMemory             memory                = VK_NULL_HANDLE;
    VkDeviceAddress            deviceAddress         = VK_NULL_HANDLE;
    Buffer                     instanceBuffer        = {};
};

AccelerationStructure createBottomAccelerationStructure(const VkDevice device, const uint32_t vertexCount, const uint32_t primitiveCount,
                                                        const VkDeviceAddress vertexBufferAddress, const VkDeviceAddress indexBufferAddress,
                                                        const VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties, const VkQueue queue,
                                                        const uint32_t queueFamilyIndex);

AccelerationStructure createTopAccelerationStructure(const VkDevice device, const AccelerationStructure bottomLevelAccelerationStructure,
                                                     const VkPhysicalDeviceMemoryProperties& physicalDeviceMemoryProperties, const VkQueue queue,
                                                     const uint32_t queueFamilyIndex);
