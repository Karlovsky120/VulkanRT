#include "rayTracing.h"

#include "commandPools.h"
#include "resources.h"

#pragma warning(push, 0)
#include <cassert>
#pragma warning(pop)

AccelerationStructure createBottomAccelerationStructure(const VkDevice device, const uint32_t vertexCount, const uint32_t primitiveCount,
                                                        const VkDeviceAddress vertexBufferAddress, const VkDeviceAddress indexBufferAddress,
                                                        const VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties, const VkQueue queue,
                                                        const uint32_t queueFamilyIndex) {

    VkAccelerationStructureCreateGeometryTypeInfoKHR createGeometryTypeInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_GEOMETRY_TYPE_INFO_KHR};
    createGeometryTypeInfo.geometryType                                     = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    createGeometryTypeInfo.maxPrimitiveCount                                = primitiveCount;
    createGeometryTypeInfo.indexType                                        = VK_INDEX_TYPE_UINT16;
    createGeometryTypeInfo.maxVertexCount                                   = vertexCount;
    createGeometryTypeInfo.vertexFormat                                     = VK_FORMAT_R32G32B32_SFLOAT;
    createGeometryTypeInfo.allowsTransforms                                 = VK_FALSE;

    VkAccelerationStructureCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    createInfo.type                                 = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    createInfo.flags                                = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    createInfo.maxGeometryCount                     = 1;
    createInfo.pGeometryInfos                       = &createGeometryTypeInfo;

    AccelerationStructure accelerationStructure = {};
    VK_CHECK(vkCreateAccelerationStructureKHR(device, &createInfo, nullptr, &accelerationStructure.accelerationStructure));

    VkAccelerationStructureMemoryRequirementsInfoKHR objectMemoryRequirementsInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_KHR};
    objectMemoryRequirementsInfo.type                                             = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_KHR;
    objectMemoryRequirementsInfo.buildType                                        = VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR;
    objectMemoryRequirementsInfo.accelerationStructure                            = accelerationStructure.accelerationStructure;

    VkMemoryRequirements2 objectMemoryRequirements2 = {VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    vkGetAccelerationStructureMemoryRequirementsKHR(device, &objectMemoryRequirementsInfo, &objectMemoryRequirements2);

    uint32_t memoryType =
        findMemoryType(physicalDeviceMemoryProperties, objectMemoryRequirements2.memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    memoryAllocateFlagsInfo.flags                     = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo memoryAllocateInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memoryAllocateInfo.allocationSize       = objectMemoryRequirements2.memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex      = memoryType;
    memoryAllocateInfo.pNext                = &memoryAllocateFlagsInfo;

    VK_CHECK(vkAllocateMemory(device, &memoryAllocateInfo, nullptr, &accelerationStructure.memory));

    VkBindAccelerationStructureMemoryInfoKHR bindMemoryInfo = {VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_KHR};
    bindMemoryInfo.accelerationStructure                    = accelerationStructure.accelerationStructure;
    bindMemoryInfo.memory                                   = accelerationStructure.memory;
    bindMemoryInfo.memoryOffset                             = 0;

    VK_CHECK(vkBindAccelerationStructureMemoryKHR(device, 1, &bindMemoryInfo));

    VkAccelerationStructureGeometryTrianglesDataKHR geometryTrianglesData = {};
    geometryTrianglesData.vertexFormat                                    = VK_FORMAT_R32G32B32_SFLOAT;
    geometryTrianglesData.vertexData.deviceAddress                        = vertexBufferAddress;
    geometryTrianglesData.vertexStride                                    = sizeof(uint32_t);
    geometryTrianglesData.indexType                                       = VK_INDEX_TYPE_UINT16;
    geometryTrianglesData.indexData.deviceAddress                         = indexBufferAddress;

    VkAccelerationStructureGeometryKHR geometry   = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.flags                                = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.geometry.triangles                   = geometryTrianglesData;
    VkAccelerationStructureGeometryKHR* pGeometry = &geometry;

    VkAccelerationStructureMemoryRequirementsInfoKHR scratchMemoryRequirementsInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_KHR};
    scratchMemoryRequirementsInfo.type                                             = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_KHR;
    scratchMemoryRequirementsInfo.buildType                                        = VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR;
    scratchMemoryRequirementsInfo.accelerationStructure                            = accelerationStructure.accelerationStructure;

    VkMemoryRequirements2 scracthMemoryRequirements2 = {VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    vkGetAccelerationStructureMemoryRequirementsKHR(device, &scratchMemoryRequirementsInfo, &scracthMemoryRequirements2);

    Buffer scratchBuffer = createBuffer(device, scracthMemoryRequirements2.memoryRequirements.size,
                                        VK_BUFFER_USAGE_RAY_TRACING_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, physicalDeviceMemoryProperties,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);

    VkBufferDeviceAddressInfo scratchBufferDeviceAddressInfo = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    scratchBufferDeviceAddressInfo.buffer                    = scratchBuffer.buffer;

    VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    buildGeometryInfo.type                                        = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildGeometryInfo.flags                                       = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildGeometryInfo.update                                      = VK_FALSE;
    buildGeometryInfo.srcAccelerationStructure                    = nullptr;
    buildGeometryInfo.dstAccelerationStructure                    = accelerationStructure.accelerationStructure;
    buildGeometryInfo.geometryArrayOfPointers                     = VK_FALSE;
    buildGeometryInfo.geometryCount                               = 1;
    buildGeometryInfo.ppGeometries                                = &pGeometry;
    buildGeometryInfo.scratchData.deviceAddress                   = vkGetBufferDeviceAddress(device, &scratchBufferDeviceAddressInfo);

    VkAccelerationStructureBuildOffsetInfoKHR buildOffsetInfo   = {};
    buildOffsetInfo.primitiveCount                              = primitiveCount;
    buildOffsetInfo.primitiveOffset                             = 0;
    buildOffsetInfo.firstVertex                                 = 0;
    buildOffsetInfo.transformOffset                             = 0;
    VkAccelerationStructureBuildOffsetInfoKHR* pBuildOffsetInfo = &buildOffsetInfo;

    VkCommandPool commandPool = createCommandPool(device, queueFamilyIndex);

    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandBufferAllocateInfo.commandPool                 = commandPool;
    commandBufferAllocateInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandBufferCount          = 1;

    VkCommandBuffer commandBuffer = 0;
    VK_CHECK(vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &commandBuffer));

    VkCommandBufferBeginInfo commandBufferBeginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));
    vkCmdBuildAccelerationStructureKHR(commandBuffer, 1, &buildGeometryInfo, &pBuildOffsetInfo);
    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkSubmitInfo submitInfo       = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &commandBuffer;
    VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, nullptr));

    VkAccelerationStructureDeviceAddressInfoKHR deviceAddressInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    deviceAddressInfo.accelerationStructure                       = accelerationStructure.accelerationStructure;
    accelerationStructure.deviceAddress                           = vkGetAccelerationStructureDeviceAddressKHR(device, &deviceAddressInfo);

    vkDeviceWaitIdle(device);

    vkFreeMemory(device, scratchBuffer.memory, nullptr);
    vkDestroyBuffer(device, scratchBuffer.buffer, nullptr);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    vkDestroyCommandPool(device, commandPool, nullptr);

    return accelerationStructure;
}

AccelerationStructure createTopAccelerationStructure(const VkDevice device, const AccelerationStructure bottomLevelAccelerationStructure,
                                                     const VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties, const VkQueue queue,
                                                     const uint32_t queueFamilyIndex) {
    VkAccelerationStructureCreateGeometryTypeInfoKHR createGeometryTypeInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_GEOMETRY_TYPE_INFO_KHR};
    createGeometryTypeInfo.geometryType                                     = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    createGeometryTypeInfo.maxPrimitiveCount                                = 1;
    createGeometryTypeInfo.allowsTransforms                                 = VK_FALSE;

    VkAccelerationStructureCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    createInfo.type                                 = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    createInfo.flags                                = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    createInfo.maxGeometryCount                     = 1;
    createInfo.pGeometryInfos                       = &createGeometryTypeInfo;

    AccelerationStructure accelerationStructure;
    VK_CHECK(vkCreateAccelerationStructureKHR(device, &createInfo, nullptr, &accelerationStructure.accelerationStructure));

    VkAccelerationStructureMemoryRequirementsInfoKHR objectMemoryRequirementsInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_KHR};
    objectMemoryRequirementsInfo.type                                             = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_KHR;
    objectMemoryRequirementsInfo.buildType                                        = VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR;
    objectMemoryRequirementsInfo.accelerationStructure                            = accelerationStructure.accelerationStructure;

    VkMemoryRequirements2 objectMemoryRequirements2 = {VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    vkGetAccelerationStructureMemoryRequirementsKHR(device, &objectMemoryRequirementsInfo, &objectMemoryRequirements2);

    uint32_t memoryType =
        findMemoryType(physicalDeviceMemoryProperties, objectMemoryRequirements2.memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    memoryAllocateFlagsInfo.flags                     = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo memoryAllocateInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memoryAllocateInfo.allocationSize       = objectMemoryRequirements2.memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex      = memoryType;
    memoryAllocateInfo.pNext                = &memoryAllocateFlagsInfo;

    VK_CHECK(vkAllocateMemory(device, &memoryAllocateInfo, nullptr, &accelerationStructure.memory));

    VkBindAccelerationStructureMemoryInfoKHR bindMemoryInfo = {VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_KHR};
    bindMemoryInfo.accelerationStructure                    = accelerationStructure.accelerationStructure;
    bindMemoryInfo.memory                                   = accelerationStructure.memory;
    bindMemoryInfo.memoryOffset                             = 0;

    VK_CHECK(vkBindAccelerationStructureMemoryKHR(device, 1, &bindMemoryInfo));

    // clang-format off
    VkTransformMatrixKHR transformationMatrix = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f
    };
    // clang-format on

    VkAccelerationStructureInstanceKHR instance;
    instance.transform                              = transformationMatrix;
    instance.instanceCustomIndex                    = 1;
    instance.mask                                   = 0xFF;
    instance.instanceShaderBindingTableRecordOffset = 0;
    instance.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference         = bottomLevelAccelerationStructure.deviceAddress;

    Buffer instanceBuffer = createBuffer(device, sizeof(VkAccelerationStructureInstanceKHR),
                                         VK_BUFFER_USAGE_RAY_TRACING_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                         physicalDeviceMemoryProperties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);

    std::vector<VkAccelerationStructureInstanceKHR> instances = {instance};

    VkCommandPool commandPool = createCommandPool(device, queueFamilyIndex);

    Buffer stagingBuffer = createBuffer(device, sizeof(VkAccelerationStructureInstanceKHR), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, physicalDeviceMemoryProperties,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

    uploadToDeviceLocalBuffer(device, instances, stagingBuffer.buffer, stagingBuffer.memory, instanceBuffer.buffer, commandPool, queue);

    vkFreeMemory(device, stagingBuffer.memory, nullptr);
    vkDestroyBuffer(device, stagingBuffer.buffer, nullptr);

    VkBufferDeviceAddressInfo instanceBufferDeviceAddressInfo = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    instanceBufferDeviceAddressInfo.buffer = instanceBuffer.buffer;

    VkAccelerationStructureGeometryInstancesDataKHR geometryInstanceData = {};
    geometryInstanceData.arrayOfPointers                                 = VK_FALSE;
    geometryInstanceData.data.deviceAddress                              = vkGetBufferDeviceAddress(device, &instanceBufferDeviceAddressInfo);

    VkAccelerationStructureGeometryKHR geometry   = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.flags                                = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances                   = geometryInstanceData;
    VkAccelerationStructureGeometryKHR* pGeometry = &geometry;

    VkAccelerationStructureMemoryRequirementsInfoKHR scratchMemoryRequirementsInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_KHR};
    scratchMemoryRequirementsInfo.type                                             = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_KHR;
    scratchMemoryRequirementsInfo.buildType                                        = VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR;
    scratchMemoryRequirementsInfo.accelerationStructure                            = accelerationStructure.accelerationStructure;

    VkMemoryRequirements2 scracthMemoryRequirements2 = {VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    vkGetAccelerationStructureMemoryRequirementsKHR(device, &scratchMemoryRequirementsInfo, &scracthMemoryRequirements2);

    Buffer scratchBuffer = createBuffer(device, scracthMemoryRequirements2.memoryRequirements.size,
                                        VK_BUFFER_USAGE_RAY_TRACING_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, physicalDeviceMemoryProperties,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);

    VkBufferDeviceAddressInfo scratchBufferDeviceAddressInfo = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    scratchBufferDeviceAddressInfo.buffer                    = scratchBuffer.buffer;

    VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    buildGeometryInfo.type                                        = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildGeometryInfo.flags                                       = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildGeometryInfo.update                                      = VK_FALSE;
    buildGeometryInfo.srcAccelerationStructure                    = nullptr;
    buildGeometryInfo.dstAccelerationStructure                    = accelerationStructure.accelerationStructure;
    buildGeometryInfo.geometryArrayOfPointers                     = VK_FALSE;
    buildGeometryInfo.geometryCount                               = 1;
    buildGeometryInfo.ppGeometries                                = &pGeometry;
    buildGeometryInfo.scratchData.deviceAddress                   = vkGetBufferDeviceAddress(device, &scratchBufferDeviceAddressInfo);

    VkAccelerationStructureBuildOffsetInfoKHR buildOffsetInfo   = {};
    buildOffsetInfo.primitiveCount                              = 1;
    buildOffsetInfo.primitiveOffset                             = 0;
    buildOffsetInfo.firstVertex                                 = 0;
    buildOffsetInfo.transformOffset                             = 0;
    VkAccelerationStructureBuildOffsetInfoKHR* pBuildOffsetInfo = &buildOffsetInfo;

    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandBufferAllocateInfo.commandPool                 = commandPool;
    commandBufferAllocateInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandBufferCount          = 1;

    VkCommandBuffer commandBuffer = 0;
    VK_CHECK(vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &commandBuffer));

    VkCommandBufferBeginInfo commandBufferBeginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));
    vkCmdBuildAccelerationStructureKHR(commandBuffer, 1, &buildGeometryInfo, &pBuildOffsetInfo);
    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkSubmitInfo submitInfo       = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &commandBuffer;
    VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, nullptr));

    VkAccelerationStructureDeviceAddressInfoKHR deviceAddressInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    deviceAddressInfo.accelerationStructure                       = accelerationStructure.accelerationStructure;
    accelerationStructure.deviceAddress                           = vkGetAccelerationStructureDeviceAddressKHR(device, &deviceAddressInfo);

    vkDeviceWaitIdle(device);

    vkFreeMemory(device, scratchBuffer.memory, nullptr);
    vkDestroyBuffer(device, scratchBuffer.buffer, nullptr);

    vkFreeMemory(device, instanceBuffer.memory, nullptr);
    vkDestroyBuffer(device, instanceBuffer.buffer, nullptr);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    vkDestroyCommandPool(device, commandPool, nullptr);

    return accelerationStructure;
}