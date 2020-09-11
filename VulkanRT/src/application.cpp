#define VOLK_IMPLEMENTATION
#include "application.h"

#include "commandPools.h"
#include "swapchain.h"

#pragma warning(push, 0)
#define GLFW_INCLUDE_VULKAN
#include "glfw3.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_XYZW_ONLY
#include "glm/fwd.hpp"
#include "glm/gtx/rotate_vector.hpp"
#include "glm/mat4x4.hpp"
#pragma warning(pop)

#include <array>
#include <chrono>
#include <cstdio>
#include <stdexcept>

#define API_DUMP 0
#define VERBOSE  0
#define INFO     0

#define PI 3.1415926535897932384f

#define WIDTH  1280
#define HEIGHT 720
#define FOV    glm::radians(100.0f)
#define NEAR   0.001f

#define ACCELERATION_FACTOR 300.0f

#define STAGING_BUFFER_SIZE 67'108'864 // 64MB

#define MAX_FRAMES_IN_FLIGHT 2
#define UI_UPDATE_PERIOD     500'000 // 0.5 seconds

#define INDEX_RAYGEN      0
#define INDEX_CLOSEST_HIT 1
#define INDEX_MISS        2

Application::~Application() {

    vkDeviceWaitIdle(m_device);

    for (VkSemaphore& semaphore : m_imageAvailableSemaphores) {
        vkDestroySemaphore(m_device, semaphore, nullptr);
    }

    for (VkSemaphore& semaphore : m_renderFinishedSemaphores) {
        vkDestroySemaphore(m_device, semaphore, nullptr);
    }

    for (VkFence& fence : m_inFlightFences) {
        vkDestroyFence(m_device, fence, nullptr);
    }

    for (size_t i = 0; i < m_swapchainImageCount; ++i) {
        vkFreeCommandBuffers(m_device, m_commandPools[i], 1, &m_commandBuffers[i]);
        vkDestroyCommandPool(m_device, m_commandPools[i], nullptr);
    }

    vkDestroyCommandPool(m_device, m_transferCommandPool, nullptr);

    vkDestroyBuffer(m_device, m_shaderBindingTableBuffer.buffer, nullptr);
    vkFreeMemory(m_device, m_shaderBindingTableBuffer.memory, nullptr);

    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);

    vkDestroyPipeline(m_device, m_rayTracingPipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_rayTracingPipelineLayout, nullptr);

    vkDestroyBuffer(m_device, m_topLevelAccelerationStructure.instanceBuffer.buffer, nullptr);
    vkFreeMemory(m_device, m_topLevelAccelerationStructure.instanceBuffer.memory, nullptr);

    vkDestroyAccelerationStructureKHR(m_device, m_topLevelAccelerationStructure.accelerationStructure, nullptr);
    vkFreeMemory(m_device, m_topLevelAccelerationStructure.memory, nullptr);

    vkDestroyAccelerationStructureKHR(m_device, m_bottomLevelAccelerationStructure.accelerationStructure, nullptr);
    vkFreeMemory(m_device, m_bottomLevelAccelerationStructure.memory, nullptr);

    vkDestroyPipeline(m_device, m_rasterPipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_rasterPipelineLayout, nullptr);
    vkDestroyPipelineCache(m_device, m_pipelineCache, nullptr);

    vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);

    vkDestroyBuffer(m_device, m_indexBuffer.buffer, nullptr);
    vkFreeMemory(m_device, m_indexBuffer.memory, nullptr);

    vkDestroyBuffer(m_device, m_vertexBuffer.buffer, nullptr);
    vkFreeMemory(m_device, m_vertexBuffer.memory, nullptr);

    for (VkFramebuffer& framebuffer : m_framebuffers) {
        vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    }

    vkDestroyImageView(m_device, m_depthImageView, nullptr);
    vkDestroyImage(m_device, m_depthImage, nullptr);
    vkFreeMemory(m_device, m_depthImageMemory, nullptr);

    vkDestroyRenderPass(m_device, m_renderPass, nullptr);

    for (VkImageView& imageView : m_swapchainImageViews) {
        vkDestroyImageView(m_device, imageView, nullptr);
    }
    vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);

    vkDestroyDevice(m_device, nullptr);

    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);

#ifdef _DEBUG
    vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugUtilsMessenger, nullptr);
#endif

    vkDestroyInstance(m_instance, nullptr);

    glfwTerminate();
}

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    Application* application = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
    KeyState&        keyState  = application->m_keyStates[key];

    if (action == GLFW_PRESS) {
        keyState.pressed = true;
        ++keyState.transitions;
    } else if (action == GLFW_RELEASE) {
        keyState.pressed = false;
        ++keyState.transitions;
    }
}

void Application::run() {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW!");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window = glfwCreateWindow(WIDTH, HEIGHT, "VulkanRT", NULL, NULL);
    if (!window) {
        throw std::runtime_error("Failed to create GLFW window!");
    }

    glfwSetWindowUserPointer(window, this);

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (!glfwRawMouseMotionSupported()) {
        throw std::runtime_error("Raw mouse motion not supported!");
    }

    glfwSetKeyCallback(window, key_callback);
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    glfwSetCursorPos(window, 0, 0);

    VK_CHECK(volkInitialize());

    assert(volkGetInstanceVersion() >= VK_API_VERSION_1_2);

    m_instance = createInstance();
    volkLoadInstance(m_instance);

    m_keyStates.emplace(std::pair<int, KeyState>(GLFW_KEY_W, {}));
    m_keyStates.emplace(std::pair<int, KeyState>(GLFW_KEY_A, {}));
    m_keyStates.emplace(std::pair<int, KeyState>(GLFW_KEY_S, {}));
    m_keyStates.emplace(std::pair<int, KeyState>(GLFW_KEY_D, {}));
    m_keyStates.emplace(std::pair<int, KeyState>(GLFW_KEY_SPACE, {}));
    m_keyStates.emplace(std::pair<int, KeyState>(GLFW_KEY_RIGHT_CONTROL, {}));
    m_keyStates.emplace(std::pair<int, KeyState>(GLFW_KEY_P, {}));

#ifdef _DEBUG
    VkDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfo = {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debugUtilsMessengerCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
#if INFO
    debugUtilsMessengerCreateInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
#endif
#if VERBOSE
    debugUtilsMessengerCreateInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
#endif
    debugUtilsMessengerCreateInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugUtilsMessengerCreateInfo.pfnUserCallback = debugUtilsCallback;

    VK_CHECK(vkCreateDebugUtilsMessengerEXT(m_instance, &debugUtilsMessengerCreateInfo, nullptr, &m_debugUtilsMessenger));
#endif

    if (glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface!");
    }

    m_physicalDevice = pickPhysicalDevice();

    m_surfaceFormat.format     = VK_FORMAT_B8G8R8A8_UNORM;
    m_surfaceFormat.colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;

    if (!surfaceFormatSupported(m_physicalDevice, m_surface, m_surfaceFormat)) {
        throw std::runtime_error("Requested surface format not supported!");
    }

    VkPhysicalDeviceRayTracingPropertiesKHR physicalDeviceRayTracingProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2             physicalDeviceProperties2          = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    physicalDeviceProperties2.pNext                                            = &physicalDeviceRayTracingProperties;
    vkGetPhysicalDeviceProperties2(m_physicalDevice, &physicalDeviceProperties2);

    VkPhysicalDeviceProperties physicalDeviceProperties = physicalDeviceProperties2.properties;

    printf("Selected GPU: %s\n", physicalDeviceProperties.deviceName);

    m_queueFamilyIndex = getGraphicsQueueFamilyIndex(m_physicalDevice);

    const float             queuePriorities       = 1.0f;
    VkDeviceQueueCreateInfo deviceQueueCreateInfo = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    deviceQueueCreateInfo.queueCount              = 1;
    deviceQueueCreateInfo.queueFamilyIndex        = m_queueFamilyIndex;
    deviceQueueCreateInfo.pQueuePriorities        = &queuePriorities;

    std::array<const char*, 4> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_RAY_TRACING_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, // Required for VK_KHR_ray_tracing
        VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME          // Required for VK_KHR_ray_tracing
    };

    VkDeviceCreateInfo deviceCreateInfo      = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceCreateInfo.queueCreateInfoCount    = 1;
    deviceCreateInfo.pQueueCreateInfos       = &deviceQueueCreateInfo;
    deviceCreateInfo.enabledExtensionCount   = static_cast<uint32_t>(deviceExtensions.size());
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

    VkPhysicalDeviceRayTracingFeaturesKHR physicalDeviceRayTracingFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_FEATURES_KHR};
    physicalDeviceRayTracingFeatures.rayTracing                            = VK_TRUE;

    VkPhysicalDeviceVulkan12Features physicalDeviceVulkan12Features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    physicalDeviceVulkan12Features.scalarBlockLayout                = VK_TRUE;
    physicalDeviceVulkan12Features.bufferDeviceAddress              = VK_TRUE;
    physicalDeviceVulkan12Features.pNext                            = &physicalDeviceRayTracingFeatures;

    VkPhysicalDeviceFeatures2 physicalDeviceFeatures2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    physicalDeviceFeatures2.pNext                     = &physicalDeviceVulkan12Features;

    deviceCreateInfo.pNext = &physicalDeviceFeatures2;

    VK_CHECK(vkCreateDevice(m_physicalDevice, &deviceCreateInfo, nullptr, &m_device));

    volkLoadDevice(m_device);

    VkQueue queue = 0;
    vkGetDeviceQueue(m_device, m_queueFamilyIndex, 0, &queue);

    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &m_surfaceCapabilities));

    m_swapchainImageCount = getSwapchainImageCount(m_surfaceCapabilities);

    m_surfaceExtent = getSurfaceExtent(window, m_surfaceCapabilities);

    m_presentMode = getPresentMode(m_physicalDevice, m_surface);
    m_swapchain =
        createSwapchain(m_device, m_surface, m_surfaceFormat, m_presentMode, m_swapchainImageCount, m_queueFamilyIndex, m_surfaceExtent, VK_NULL_HANDLE);

    m_swapchainImages = std::vector<VkImage>(m_swapchainImageCount);
    VK_CHECK(vkGetSwapchainImagesKHR(m_device, m_swapchain, &m_swapchainImageCount, m_swapchainImages.data()));

    m_swapchainImageViews = getSwapchainImageViews(m_device, m_swapchainImages, m_surfaceFormat.format);

    m_physicalDeviceMemoryProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_physicalDeviceMemoryProperties);

    m_depthImage = createImage(m_device, m_surfaceExtent, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_FORMAT_D32_SFLOAT_S8_UINT);
    VkMemoryRequirements depthImageMemoryRequirements;
    vkGetImageMemoryRequirements(m_device, m_depthImage, &depthImageMemoryRequirements);
    m_depthImageMemory =
        allocateVulkanObjectMemory(m_device, depthImageMemoryRequirements, m_physicalDeviceMemoryProperties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkBindImageMemory(m_device, m_depthImage, m_depthImageMemory, 0);

    m_depthImageView = createImageView(m_device, m_depthImage, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_IMAGE_ASPECT_DEPTH_BIT);

    m_renderPass   = createRenderPass();
    m_framebuffers = createFramebuffers();

    Buffer stagingBuffer =
        createBuffer(m_device, STAGING_BUFFER_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, m_physicalDeviceMemoryProperties, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

    m_transferCommandPool = createCommandPool(m_device, m_queueFamilyIndex);

    // clang-format off
    std::vector<float> cubeVertices = {
            0.5, -0.5, -0.5,
            0.5, -0.5, 0.5,
            -0.5, -0.5, 0.5,
            -0.5, -0.5, -0.5,
            0.5, 0.5, -0.5,
            0.5, 0.5, 0.5,
            -0.5, 0.5, 0.5,
            -0.5, 0.5, -0.5
    };
    // clang-format on

    VkBufferUsageFlags bufferUsageFlags =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_KHR;

    uint32_t vertexBufferSize = sizeof(float) * static_cast<uint32_t>(cubeVertices.size());
    m_vertexBuffer = createBuffer(m_device, vertexBufferSize, bufferUsageFlags, m_physicalDeviceMemoryProperties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                  VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);

    uploadToDeviceLocalBuffer(m_device, cubeVertices, stagingBuffer.buffer, stagingBuffer.memory, m_vertexBuffer.buffer, m_transferCommandPool, queue);

    // clang-format off
    std::vector<uint16_t> cubeIndices = {
            0, 1, 3, 3, 1, 2,
            1, 5, 2, 2, 5, 6,
            5, 4, 6, 6, 4, 7,
            4, 0, 7, 7, 0, 3,
            3, 2, 7, 7, 2, 6,
            4, 5, 0, 0, 5, 1
    };
    // clang-format on

    uint32_t indexBufferSize = sizeof(uint16_t) * static_cast<uint32_t>(cubeIndices.size());
    m_indexBuffer            = createBuffer(m_device, indexBufferSize, bufferUsageFlags, m_physicalDeviceMemoryProperties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                 VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);

    uploadToDeviceLocalBuffer(m_device, cubeIndices, stagingBuffer.buffer, stagingBuffer.memory, m_indexBuffer.buffer, m_transferCommandPool, queue);

    VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    pipelineCacheCreateInfo.initialDataSize           = 0;
    VK_CHECK(vkCreatePipelineCache(m_device, &pipelineCacheCreateInfo, nullptr, &m_pipelineCache));

    std::array<VkDescriptorSetLayoutBinding, 4> descriptorSetLayoutBindings;
    descriptorSetLayoutBindings.fill({});

    // Vertex buffer
    descriptorSetLayoutBindings[0].binding         = 0;
    descriptorSetLayoutBindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorSetLayoutBindings[0].descriptorCount = 1;
    descriptorSetLayoutBindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    // Index buffer
    descriptorSetLayoutBindings[1].binding         = 1;
    descriptorSetLayoutBindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorSetLayoutBindings[1].descriptorCount = 1;
    descriptorSetLayoutBindings[1].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    // Acceleration structure
    descriptorSetLayoutBindings[2].binding         = 2;
    descriptorSetLayoutBindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    descriptorSetLayoutBindings[2].descriptorCount = 1;
    descriptorSetLayoutBindings[2].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    // Ray tracing image
    descriptorSetLayoutBindings[3].binding         = 3;
    descriptorSetLayoutBindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorSetLayoutBindings[3].descriptorCount = 1;
    descriptorSetLayoutBindings[3].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptorSetLayoutCreateInfo.bindingCount                    = static_cast<uint32_t>(descriptorSetLayoutBindings.size());
    descriptorSetLayoutCreateInfo.pBindings                       = descriptorSetLayoutBindings.data();
    VK_CHECK(vkCreateDescriptorSetLayout(m_device, &descriptorSetLayoutCreateInfo, nullptr, &m_descriptorSetLayout));

    VkPushConstantRange rasterPushConstantRange = {};
    rasterPushConstantRange.offset              = 0;
    rasterPushConstantRange.size                = sizeof(RasterPushData);
    rasterPushConstantRange.stageFlags          = VK_SHADER_STAGE_VERTEX_BIT;

    VkPipelineLayoutCreateInfo rasterPipelineLayoutCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    rasterPipelineLayoutCreateInfo.pushConstantRangeCount     = 1;
    rasterPipelineLayoutCreateInfo.pPushConstantRanges        = &rasterPushConstantRange;
    rasterPipelineLayoutCreateInfo.setLayoutCount             = 1;
    rasterPipelineLayoutCreateInfo.pSetLayouts                = &m_descriptorSetLayout;
    VK_CHECK(vkCreatePipelineLayout(m_device, &rasterPipelineLayoutCreateInfo, nullptr, &m_rasterPipelineLayout));

    VkShaderModule vertexShader   = loadShader("src/shaders/spirv/vertexShader.spv");
    VkShaderModule fragmentShader = loadShader("src/shaders/spirv/fragmentShader.spv");

    m_rasterPipeline = createRasterPipeline(vertexShader, fragmentShader);

    vkDestroyShaderModule(m_device, fragmentShader, nullptr);
    vkDestroyShaderModule(m_device, vertexShader, nullptr);

    m_bottomLevelAccelerationStructure = createBottomAccelerationStructure(
        m_device, static_cast<uint32_t>(cubeVertices.size() / 3), static_cast<uint32_t>(cubeIndices.size() / 3), m_vertexBuffer.deviceAddress,
        m_indexBuffer.deviceAddress, m_physicalDeviceMemoryProperties, queue, m_queueFamilyIndex);

    m_topLevelAccelerationStructure =
        createTopAccelerationStructure(m_device, m_bottomLevelAccelerationStructure, m_physicalDeviceMemoryProperties, queue, m_queueFamilyIndex);

    VkPushConstantRange rayTracePushConstantRange = {};
    rayTracePushConstantRange.offset              = 0;
    rayTracePushConstantRange.size                = sizeof(RayTracingPushData);
    rayTracePushConstantRange.stageFlags          = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkPipelineLayoutCreateInfo rayTracePipelineLayoutCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    rayTracePipelineLayoutCreateInfo.pushConstantRangeCount     = 1;
    rayTracePipelineLayoutCreateInfo.pPushConstantRanges        = &rayTracePushConstantRange;
    rayTracePipelineLayoutCreateInfo.setLayoutCount             = 1;
    rayTracePipelineLayoutCreateInfo.pSetLayouts                = &m_descriptorSetLayout;
    VK_CHECK(vkCreatePipelineLayout(m_device, &rayTracePipelineLayoutCreateInfo, nullptr, &m_rayTracingPipelineLayout));

    VkShaderModule raygenShader     = loadShader("src/shaders/spirv/raygenShader.spv");
    VkShaderModule closestHitShader = loadShader("src/shaders/spirv/closestHitShader.spv");
    VkShaderModule missShader       = loadShader("src/shaders/spirv/missShader.spv");

    m_rayTracingPipeline = createRayTracingPipeline(raygenShader, closestHitShader, missShader);

    vkDestroyShaderModule(m_device, missShader, nullptr);
    vkDestroyShaderModule(m_device, closestHitShader, nullptr);
    vkDestroyShaderModule(m_device, raygenShader, nullptr);

    // clang-format off
    std::array<VkDescriptorPoolSize, 3> descriptorPoolSizes = {{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2},
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}
    }};
    // clang-format on

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptorPoolCreateInfo.poolSizeCount              = static_cast<uint32_t>(descriptorPoolSizes.size());
    descriptorPoolCreateInfo.pPoolSizes                 = descriptorPoolSizes.data();
    descriptorPoolCreateInfo.maxSets                    = m_swapchainImageCount;
    VK_CHECK(vkCreateDescriptorPool(m_device, &descriptorPoolCreateInfo, nullptr, &m_descriptorPool));

    // Allocating one descriptor set for each swapchain image, all with the same layout
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts(m_swapchainImageCount, m_descriptorSetLayout);

    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    descriptorSetAllocateInfo.descriptorPool              = m_descriptorPool;
    descriptorSetAllocateInfo.descriptorSetCount          = m_swapchainImageCount;
    descriptorSetAllocateInfo.pSetLayouts                 = descriptorSetLayouts.data();

    m_descriptorSets = std::vector<VkDescriptorSet>(m_swapchainImageCount);
    vkAllocateDescriptorSets(m_device, &descriptorSetAllocateInfo, m_descriptorSets.data());

    std::array<VkDescriptorBufferInfo, 2> descriptorBufferInfos;
    descriptorBufferInfos[0].buffer = m_vertexBuffer.buffer;
    descriptorBufferInfos[0].offset = 0;
    descriptorBufferInfos[0].range  = vertexBufferSize;

    descriptorBufferInfos[1].buffer = m_indexBuffer.buffer;
    descriptorBufferInfos[1].offset = 0;
    descriptorBufferInfos[1].range  = indexBufferSize;

    VkWriteDescriptorSetAccelerationStructureKHR writeDescriptorSetAccelerationStructure = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    writeDescriptorSetAccelerationStructure.accelerationStructureCount                   = 1;
    writeDescriptorSetAccelerationStructure.pAccelerationStructures                      = &m_topLevelAccelerationStructure.accelerationStructure;

    VkDescriptorImageInfo descriptorSwapchainImageInfo = {};
    descriptorSwapchainImageInfo.imageLayout           = VK_IMAGE_LAYOUT_GENERAL;

    std::array<VkWriteDescriptorSet, 3> writeDescriptorSets;
    writeDescriptorSets.fill({VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET});

    writeDescriptorSets[0].dstBinding      = 0; // 0 for vertex and 1 for index buffer
    writeDescriptorSets[0].dstArrayElement = 0;
    writeDescriptorSets[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writeDescriptorSets[0].descriptorCount = static_cast<uint32_t>(descriptorBufferInfos.size());
    writeDescriptorSets[0].pBufferInfo     = descriptorBufferInfos.data();

    writeDescriptorSets[1].dstBinding      = 2;
    writeDescriptorSets[1].dstArrayElement = 0;
    writeDescriptorSets[1].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    writeDescriptorSets[1].descriptorCount = 1;
    writeDescriptorSets[1].pNext           = &writeDescriptorSetAccelerationStructure;

    writeDescriptorSets[2].dstBinding      = 3;
    writeDescriptorSets[2].dstArrayElement = 0;
    writeDescriptorSets[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writeDescriptorSets[2].descriptorCount = 1;
    writeDescriptorSets[2].pImageInfo      = &descriptorSwapchainImageInfo;

    for (size_t i = 0; i < m_swapchainImageCount; ++i) {
        descriptorSwapchainImageInfo.imageView = m_swapchainImageViews[i];

        writeDescriptorSets[0].dstSet = m_descriptorSets[i];
        writeDescriptorSets[1].dstSet = m_descriptorSets[i];
        writeDescriptorSets[2].dstSet = m_descriptorSets[i];

        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
    }

    const uint32_t shaderGroupCount = 3;

    const VkDeviceSize baseGroupAlignment    = physicalDeviceRayTracingProperties.shaderGroupBaseAlignment;
    const VkDeviceSize shaderGroupHandleSize = physicalDeviceRayTracingProperties.shaderGroupHandleSize;

    const VkDeviceSize   shaderHandleStorageSize = shaderGroupHandleSize * shaderGroupCount;
    std::vector<uint8_t> shaderHandleStorage(shaderHandleStorageSize);
    vkGetRayTracingShaderGroupHandlesKHR(m_device, m_rayTracingPipeline, 0, shaderGroupCount, shaderHandleStorageSize, shaderHandleStorage.data());
    uint8_t* shaderHandlesStoragePtr = shaderHandleStorage.data();

    const VkDeviceSize   alignedShaderHandlesSize = baseGroupAlignment * shaderGroupCount;
    std::vector<uint8_t> alignedShaderHandles(alignedShaderHandlesSize);
    uint8_t*             alignedShaderHandlesPtr = alignedShaderHandles.data();

    for (size_t i = 0; i < 3; ++i) {
        memcpy(alignedShaderHandlesPtr, shaderHandlesStoragePtr, shaderGroupHandleSize);
        shaderHandlesStoragePtr += shaderGroupHandleSize;
        alignedShaderHandlesPtr += baseGroupAlignment;
    }

    m_shaderBindingTableBuffer = createBuffer(m_device, alignedShaderHandlesSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_KHR,
                                              m_physicalDeviceMemoryProperties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    uploadToDeviceLocalBuffer(m_device, alignedShaderHandles, stagingBuffer.buffer, stagingBuffer.memory, m_shaderBindingTableBuffer.buffer,
                              m_transferCommandPool, queue);

    vkFreeMemory(m_device, stagingBuffer.memory, nullptr);
    vkDestroyBuffer(m_device, stagingBuffer.buffer, nullptr);

    VkStridedBufferRegionKHR raygenStridedBufferRegion = {};
    raygenStridedBufferRegion.buffer                   = m_shaderBindingTableBuffer.buffer;
    raygenStridedBufferRegion.offset                   = static_cast<VkDeviceSize>(baseGroupAlignment * INDEX_RAYGEN);
    raygenStridedBufferRegion.size                     = shaderGroupHandleSize;
    raygenStridedBufferRegion.stride                   = shaderGroupHandleSize;

    VkStridedBufferRegionKHR closestHitStridedBufferRegion = {};
    closestHitStridedBufferRegion.buffer                   = m_shaderBindingTableBuffer.buffer;
    closestHitStridedBufferRegion.offset                   = static_cast<VkDeviceSize>(baseGroupAlignment * INDEX_CLOSEST_HIT);
    closestHitStridedBufferRegion.size                     = shaderGroupHandleSize;
    closestHitStridedBufferRegion.stride                   = shaderGroupHandleSize;

    VkStridedBufferRegionKHR missStridedBufferRegion = {};
    missStridedBufferRegion.buffer                   = m_shaderBindingTableBuffer.buffer;
    missStridedBufferRegion.offset                   = static_cast<VkDeviceSize>(baseGroupAlignment * INDEX_MISS);
    missStridedBufferRegion.size                     = shaderGroupHandleSize;
    missStridedBufferRegion.stride                   = shaderGroupHandleSize;

    VkStridedBufferRegionKHR callableStridedBufferRegion = {};

    VkCommandPoolCreateInfo commandPoolCreateInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandPoolCreateInfo.flags                   = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    commandPoolCreateInfo.queueFamilyIndex        = m_queueFamilyIndex;

    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandBufferAllocateInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandBufferCount          = 1;

    m_commandPools   = std::vector<VkCommandPool>(m_swapchainImageCount);
    m_commandBuffers = std::vector<VkCommandBuffer>(m_swapchainImageCount);
    for (size_t i = 0; i < m_swapchainImageCount; ++i) {
        VK_CHECK(vkCreateCommandPool(m_device, &commandPoolCreateInfo, nullptr, &m_commandPools[i]));
        commandBufferAllocateInfo.commandPool = m_commandPools[i];
        VK_CHECK(vkAllocateCommandBuffers(m_device, &commandBufferAllocateInfo, &m_commandBuffers[i]));
    }

    m_imageAvailableSemaphores = std::vector<VkSemaphore>(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores = std::vector<VkSemaphore>(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences           = std::vector<VkFence>(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkFence> imagesInFlight(m_swapchainImageCount, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreCreateInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo     fenceCreateInfo     = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceCreateInfo.flags                     = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK(vkCreateSemaphore(m_device, &semaphoreCreateInfo, nullptr, &m_imageAvailableSemaphores[i]));
        VK_CHECK(vkCreateSemaphore(m_device, &semaphoreCreateInfo, nullptr, &m_renderFinishedSemaphores[i]));
        VK_CHECK(vkCreateFence(m_device, &fenceCreateInfo, nullptr, &m_inFlightFences[i]));
    }

    m_camera.orientation = glm::vec2(0.0f, 0.0f);
    m_camera.position    = glm::vec3(0.0f, 0.0f, 2.5f);

    m_rasterPushData.oneOverTanOfHalfFov = 1.0f / tan(0.5f * FOV);
    m_rasterPushData.oneOverAspectRatio  = static_cast<float>(m_surfaceExtent.height) / static_cast<float>(m_surfaceExtent.width);
    m_rasterPushData.near                = NEAR;

    m_rayTracingPushData.oneOverTanOfHalfFov = 1.0f / tan(0.5f * FOV);

    uint32_t currentFrame = 0;
    bool     rayTracing   = true;

    std::chrono::high_resolution_clock::time_point oldTime = std::chrono::high_resolution_clock::now();
    uint32_t                                       time    = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        vkWaitForFences(m_device, 1, &m_inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

        uint32_t imageIndex;
        VkResult acquireResult =
            vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, m_imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            updateSurfaceDependantStructures(m_queueFamilyIndex);
            continue;
        } else if (acquireResult != VK_SUBOPTIMAL_KHR) {
            VK_CHECK(acquireResult);
        }

        if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
            vkWaitForFences(m_device, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
        }
        imagesInFlight[imageIndex] = m_inFlightFences[currentFrame];

        vkResetCommandPool(m_device, m_commandPools[imageIndex], VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

        std::chrono::high_resolution_clock::time_point newTime = std::chrono::high_resolution_clock::now();
        uint32_t frameTime = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(newTime - oldTime).count());
        oldTime            = newTime;
        time += frameTime;

        if (time > UI_UPDATE_PERIOD) {
            char title[256];
            sprintf_s(title, "Frametime: %.2fms", frameTime / 1'000.0f);
            glfwSetWindowTitle(window, title);
            time = 0;
        }

        if (m_keyStates[GLFW_KEY_P].pressed && m_keyStates[GLFW_KEY_P].transitions % 2 == 1) {
            rayTracing = !rayTracing;
        }

        m_keyStates[GLFW_KEY_P].transitions = 0;

        updateCameraAndPushData(frameTime);

        VkPipelineStageFlags waitStage;

        if (rayTracing) {
            recordRayTracingCommandBuffer(imageIndex, raygenStridedBufferRegion, closestHitStridedBufferRegion, missStridedBufferRegion,
                                          callableStridedBufferRegion);
            waitStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        } else {
            recordRasterCommandBuffer(imageIndex, static_cast<uint32_t>(cubeIndices.size()));
            waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }

        VkSubmitInfo submitInfo         = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.waitSemaphoreCount   = 1;
        submitInfo.pWaitSemaphores      = &m_imageAvailableSemaphores[currentFrame];
        submitInfo.pWaitDstStageMask    = &waitStage;
        submitInfo.commandBufferCount   = 1;
        submitInfo.pCommandBuffers      = &m_commandBuffers[imageIndex];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores    = &m_renderFinishedSemaphores[currentFrame];

        vkResetFences(m_device, 1, &m_inFlightFences[currentFrame]);

        VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, m_inFlightFences[currentFrame]));

        VkPresentInfoKHR presentInfo   = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores    = &m_renderFinishedSemaphores[currentFrame];
        presentInfo.swapchainCount     = 1;
        presentInfo.pSwapchains        = &m_swapchain;
        presentInfo.pImageIndices      = &imageIndex;

        VkResult presentResult = vkQueuePresentKHR(queue, &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
            updateSurfaceDependantStructures(m_queueFamilyIndex);
            continue;
        } else {
            VK_CHECK(presentResult);
        }

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }
}

VkInstance Application::createInstance() const {
    VkApplicationInfo applicationInfo  = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    applicationInfo.apiVersion         = VK_API_VERSION_1_2;
    applicationInfo.applicationVersion = 0;
    applicationInfo.pApplicationName   = NULL;
    applicationInfo.pEngineName        = NULL;
    applicationInfo.engineVersion      = 0;

    VkInstanceCreateInfo createInfo  = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo      = &applicationInfo;
    createInfo.enabledLayerCount     = 0;
    createInfo.enabledExtensionCount = 0;

#ifdef _DEBUG
    std::vector<const char*> layers = {
#if API_DUMP
        "VK_LAYER_LUNARG_api_dump",
#endif
        "VK_LAYER_KHRONOS_validation"
    };

    createInfo.enabledLayerCount   = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();
#endif

    uint32_t     glfwExtensionCount;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

#ifdef _DEBUG
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkInstance instance = 0;
    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &instance));

    return instance;
}

uint32_t Application::getGraphicsQueueFamilyIndex(const VkPhysicalDevice& physicalDevice) const {
    uint32_t queueFamilyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, 0);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    uint32_t     queueFamilyIndex = UINT32_MAX;
    VkQueueFlags queueFlags       = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    for (size_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & queueFlags) {
            queueFamilyIndex = static_cast<uint32_t>(i);
            break;
        }
    }

    return queueFamilyIndex;
}

VkPhysicalDevice Application::pickPhysicalDevice() const {
    uint32_t physicalDeviceCount;
    VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &physicalDeviceCount, 0));

    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &physicalDeviceCount, physicalDevices.data()));

    uint32_t graphicsQueueIndex = UINT32_MAX;
    for (size_t i = 0; i < physicalDeviceCount; ++i) {
        VkPhysicalDevice physicalDevice = physicalDevices[i];

        VkPhysicalDeviceProperties physicalDeviceProperties = {};
        vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);

        printf("GPU%d: %s\n", static_cast<uint32_t>(i), physicalDeviceProperties.deviceName);

        if (physicalDeviceProperties.apiVersion < VK_API_VERSION_1_2) {
            continue;
        }

        if (physicalDeviceProperties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            continue;
        }

        graphicsQueueIndex = getGraphicsQueueFamilyIndex(physicalDevice);

        if (graphicsQueueIndex == UINT32_MAX) {
            continue;
        }

        VkBool32 presentSupported;
        VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, graphicsQueueIndex, m_surface, &presentSupported));

        if (presentSupported == VK_FALSE) {
            continue;
        }

        uint32_t extensionPropertyCount = 0;
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionPropertyCount, nullptr);

        std::vector<VkExtensionProperties> extensionPropertiess(extensionPropertyCount);
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionPropertyCount, extensionPropertiess.data());

        bool rayTracingSupported             = false;
        bool defferedHostOperationsSupported = false; // Required for VK_KHR_ray_tracing
        bool pipelineLibrarySupported        = false; // Required for VK_KHR_ray_tracing
        for (VkExtensionProperties extensionProperties : extensionPropertiess) {
            if (strcmp(extensionProperties.extensionName, VK_KHR_RAY_TRACING_EXTENSION_NAME) == 0) {
                rayTracingSupported = true;
            } else if (strcmp(extensionProperties.extensionName, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) == 0) {
                defferedHostOperationsSupported = true;
            } else if (strcmp(extensionProperties.extensionName, VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME) == 0) {
                pipelineLibrarySupported = true;
            }

            if (rayTracingSupported && defferedHostOperationsSupported && pipelineLibrarySupported) {
                break;
            }
        }

        if (!rayTracingSupported || !defferedHostOperationsSupported || !pipelineLibrarySupported) {
            continue;
        }

        return physicalDevice;
    }

    throw std::runtime_error("No suitable GPU found!");
}

VkRenderPass Application::createRenderPass() const {
    std::array<VkAttachmentDescription, 2> attachments;
    attachments.fill({});

    attachments[0].format        = m_surfaceFormat.format;
    attachments[0].samples       = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[1].format         = VK_FORMAT_D32_SFLOAT_S8_UINT;
    attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass    = {};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency subpassDependency = {};
    subpassDependency.srcSubpass          = VK_SUBPASS_EXTERNAL;
    subpassDependency.dstSubpass          = 0;
    subpassDependency.srcStageMask        = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependency.dstStageMask        = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependency.srcAccessMask       = 0;
    subpassDependency.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassCreateInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    renderPassCreateInfo.attachmentCount        = static_cast<uint32_t>(attachments.size());
    renderPassCreateInfo.pAttachments           = attachments.data();
    renderPassCreateInfo.subpassCount           = 1;
    renderPassCreateInfo.pSubpasses             = &subpass;
    renderPassCreateInfo.dependencyCount        = 1;
    renderPassCreateInfo.pDependencies          = &subpassDependency;

    VkRenderPass renderPass = 0;
    VK_CHECK(vkCreateRenderPass(m_device, &renderPassCreateInfo, nullptr, &renderPass));

    return renderPass;
}

std::vector<VkFramebuffer> Application::createFramebuffers() const {
    VkFramebufferCreateInfo framebufferCreateInfo = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebufferCreateInfo.renderPass              = m_renderPass;
    framebufferCreateInfo.attachmentCount         = 2;
    framebufferCreateInfo.width                   = m_surfaceExtent.width;
    framebufferCreateInfo.height                  = m_surfaceExtent.height;
    framebufferCreateInfo.layers                  = 1;

    std::vector<VkFramebuffer> framebuffers(m_swapchainImageCount);
    std::array<VkImageView, 2> attachments({});
    attachments[1] = m_depthImageView;

    for (size_t i = 0; i < m_swapchainImageCount; ++i) {
        attachments[0]                     = m_swapchainImageViews[i];
        framebufferCreateInfo.pAttachments = attachments.data();
        VK_CHECK(vkCreateFramebuffer(m_device, &framebufferCreateInfo, nullptr, &framebuffers[i]));
    }

    return framebuffers;
}

VkShaderModule Application::loadShader(const char* pathToSource) const {
    FILE* source;
    fopen_s(&source, pathToSource, "rb");
    assert(source);

    fseek(source, 0, SEEK_END);
    size_t length = static_cast<size_t>(ftell(source));
    assert(length > 0);
    fseek(source, 0, SEEK_SET);

    char* buffer = new char[length];
    if (fread(buffer, 1, length, source) != length) {
        assert(false);
    }

    VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize                 = length;
    createInfo.pCode                    = reinterpret_cast<uint32_t*>(buffer);

    VkShaderModule shaderModule = 0;
    VK_CHECK(vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule));

    delete[] buffer;

    return shaderModule;
}

VkPipeline Application::createRasterPipeline(const VkShaderModule& vertexShader, const VkShaderModule& fragmentShader) const {
    VkGraphicsPipelineCreateInfo createInfo = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
    shaderStages.fill({VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO});

    shaderStages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertexShader;
    shaderStages[0].pName  = "main";

    shaderStages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragmentShader;
    shaderStages[1].pName  = "main";

    createInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    createInfo.pStages    = shaderStages.data();

    VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    createInfo.pVertexInputState                                    = &vertexInputStateCreateInfo;

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssemblyStateCreateInfo.topology                               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    createInfo.pInputAssemblyState                                      = &inputAssemblyStateCreateInfo;

    VkPipelineViewportStateCreateInfo viewportStateCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportStateCreateInfo.viewportCount                     = 1;
    viewportStateCreateInfo.scissorCount                      = 1;
    createInfo.pViewportState                                 = &viewportStateCreateInfo;

    VkPipelineRasterizationStateCreateInfo rasterizationStateCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizationStateCreateInfo.lineWidth                              = 1.0f;
    rasterizationStateCreateInfo.frontFace                              = VK_FRONT_FACE_CLOCKWISE;
    rasterizationStateCreateInfo.cullMode                               = VK_CULL_MODE_BACK_BIT;
    createInfo.pRasterizationState                                      = &rasterizationStateCreateInfo;

    VkPipelineMultisampleStateCreateInfo multisampleStateCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampleStateCreateInfo.rasterizationSamples                 = VK_SAMPLE_COUNT_1_BIT;
    createInfo.pMultisampleState                                    = &multisampleStateCreateInfo;

    VkPipelineDepthStencilStateCreateInfo depthStencilStateCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencilStateCreateInfo.depthTestEnable                       = true;
    depthStencilStateCreateInfo.depthWriteEnable                      = true;
    depthStencilStateCreateInfo.depthCompareOp                        = VK_COMPARE_OP_GREATER;
    createInfo.pDepthStencilState                                     = &depthStencilStateCreateInfo;

    VkPipelineColorBlendAttachmentState colorBlendAttachmentState = {};
    colorBlendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlendStateCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlendStateCreateInfo.attachmentCount                     = 1;
    colorBlendStateCreateInfo.pAttachments                        = &colorBlendAttachmentState;
    createInfo.pColorBlendState                                   = &colorBlendStateCreateInfo;

    std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicStateCreateInfo.dynamicStateCount                = static_cast<uint32_t>(dynamicStates.size());
    dynamicStateCreateInfo.pDynamicStates                   = dynamicStates.data();
    createInfo.pDynamicState                                = &dynamicStateCreateInfo;

    createInfo.layout     = m_rasterPipelineLayout;
    createInfo.renderPass = m_renderPass;

    VkPipeline pipeline = 0;
    VK_CHECK(vkCreateGraphicsPipelines(m_device, m_pipelineCache, 1, &createInfo, nullptr, &pipeline));

    return pipeline;
}

VkPipeline Application::createRayTracingPipeline(const VkShaderModule& raygenShaderModule, const VkShaderModule& closestHitShaderModule,
                                                 const VkShaderModule& missShaderModule) const {
    std::array<VkPipelineShaderStageCreateInfo, 3> shaderStagesCreateInfos;
    shaderStagesCreateInfos.fill({VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO});
    shaderStagesCreateInfos[INDEX_RAYGEN].stage  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    shaderStagesCreateInfos[INDEX_RAYGEN].module = raygenShaderModule;
    shaderStagesCreateInfos[INDEX_RAYGEN].pName  = "main";

    shaderStagesCreateInfos[INDEX_CLOSEST_HIT].stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    shaderStagesCreateInfos[INDEX_CLOSEST_HIT].module = closestHitShaderModule;
    shaderStagesCreateInfos[INDEX_CLOSEST_HIT].pName  = "main";

    shaderStagesCreateInfos[INDEX_MISS].stage  = VK_SHADER_STAGE_MISS_BIT_KHR;
    shaderStagesCreateInfos[INDEX_MISS].module = missShaderModule;
    shaderStagesCreateInfos[INDEX_MISS].pName  = "main";

    std::array<VkRayTracingShaderGroupCreateInfoKHR, 3> rayTracingShaderGroupCreateInfos;
    rayTracingShaderGroupCreateInfos.fill({VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR});

    for (VkRayTracingShaderGroupCreateInfoKHR& rayTracingShaderGroupCreateInfo : rayTracingShaderGroupCreateInfos) {
        rayTracingShaderGroupCreateInfo.generalShader      = VK_SHADER_UNUSED_KHR;
        rayTracingShaderGroupCreateInfo.closestHitShader   = VK_SHADER_UNUSED_KHR;
        rayTracingShaderGroupCreateInfo.anyHitShader       = VK_SHADER_UNUSED_KHR;
        rayTracingShaderGroupCreateInfo.intersectionShader = VK_SHADER_UNUSED_KHR;
    }

    rayTracingShaderGroupCreateInfos[INDEX_RAYGEN].type                  = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    rayTracingShaderGroupCreateInfos[INDEX_RAYGEN].generalShader         = INDEX_RAYGEN;
    rayTracingShaderGroupCreateInfos[INDEX_CLOSEST_HIT].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    rayTracingShaderGroupCreateInfos[INDEX_CLOSEST_HIT].closestHitShader = INDEX_CLOSEST_HIT;
    rayTracingShaderGroupCreateInfos[INDEX_MISS].type                    = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    rayTracingShaderGroupCreateInfos[INDEX_MISS].generalShader           = INDEX_MISS;

    VkRayTracingPipelineCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    createInfo.stageCount                        = static_cast<uint32_t>(shaderStagesCreateInfos.size());
    createInfo.pStages                           = shaderStagesCreateInfos.data();
    createInfo.groupCount                        = static_cast<uint32_t>(rayTracingShaderGroupCreateInfos.size());
    createInfo.pGroups                           = rayTracingShaderGroupCreateInfos.data();
    createInfo.maxRecursionDepth                 = 1;
    createInfo.layout                            = m_rayTracingPipelineLayout;
    createInfo.libraries                         = {VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR};

    VkPipeline pipeline = 0;
    VK_CHECK(vkCreateRayTracingPipelinesKHR(m_device, m_pipelineCache, 1, &createInfo, nullptr, &pipeline));

    return pipeline;
}

void Application::recordRasterCommandBuffer(const uint32_t& frameIndex, const uint32_t& indexCount) const {
    VkCommandBufferBeginInfo commandBufferBeginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

    VkViewport viewport = {};
    viewport.width      = static_cast<float>(m_surfaceExtent.width);
    viewport.height     = static_cast<float>(m_surfaceExtent.height);
    viewport.x          = 0;
    viewport.y          = 0;
    viewport.minDepth   = 1.0f;
    viewport.maxDepth   = 0.0f;

    VkRect2D scissor = {};
    scissor.offset   = {0, 0};
    scissor.extent   = m_surfaceExtent;

    VK_CHECK(vkBeginCommandBuffer(m_commandBuffers[frameIndex], &commandBufferBeginInfo));

    vkCmdSetViewport(m_commandBuffers[frameIndex], 0, 1, &viewport);
    vkCmdSetScissor(m_commandBuffers[frameIndex], 0, 1, &scissor);

    VkRenderPassBeginInfo renderPassBeginInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassBeginInfo.renderPass            = m_renderPass;
    renderPassBeginInfo.renderArea.offset     = {0, 0};
    renderPassBeginInfo.renderArea.extent     = m_surfaceExtent;

    VkClearValue                colorImageClearColor = {0.0f, 0.0f, 0.2f, 1.0f};
    VkClearValue                depthImageClearColor = {0.0f, 0.0f, 0.0f, 0.0f};
    std::array<VkClearValue, 2> imageClearColors     = {colorImageClearColor, depthImageClearColor};
    renderPassBeginInfo.clearValueCount              = static_cast<uint32_t>(imageClearColors.size());
    renderPassBeginInfo.pClearValues                 = imageClearColors.data();
    renderPassBeginInfo.framebuffer                  = m_framebuffers[frameIndex];
    vkCmdBeginRenderPass(m_commandBuffers[frameIndex], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdPushConstants(m_commandBuffers[frameIndex], m_rasterPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(RasterPushData), &m_rasterPushData);

    vkCmdBindPipeline(m_commandBuffers[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, m_rasterPipeline);
    vkCmdBindDescriptorSets(m_commandBuffers[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, m_rasterPipelineLayout, 0, 1, &m_descriptorSets[frameIndex], 0,
                            nullptr);

    vkCmdDraw(m_commandBuffers[frameIndex], indexCount, 1, 0, 0);

    vkCmdEndRenderPass(m_commandBuffers[frameIndex]);

    VK_CHECK(vkEndCommandBuffer(m_commandBuffers[frameIndex]));
}

void Application::recordRayTracingCommandBuffer(const uint32_t& frameIndex, const VkStridedBufferRegionKHR& raygenStridedBufferRegion,
                                                const VkStridedBufferRegionKHR& closestHitStridedBufferRegion,
                                                const VkStridedBufferRegionKHR& missStridedBufferRegion,
                                                const VkStridedBufferRegionKHR& callableBufferRegion) const {
    VkCommandBufferBeginInfo commandBufferBeginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

    VK_CHECK(vkBeginCommandBuffer(m_commandBuffers[frameIndex], &commandBufferBeginInfo));

    VkImageMemoryBarrier undefinedToGeneral = createImageMemoryBarrier(m_swapchainImages[frameIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    vkCmdPipelineBarrier(m_commandBuffers[frameIndex], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &undefinedToGeneral);

    vkCmdPushConstants(m_commandBuffers[frameIndex], m_rayTracingPipelineLayout, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(RayTracingPushData),
                       &m_rayTracingPushData);

    vkCmdBindPipeline(m_commandBuffers[frameIndex], VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rayTracingPipeline);
    vkCmdBindDescriptorSets(m_commandBuffers[frameIndex], VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rayTracingPipelineLayout, 0, 1,
                            &m_descriptorSets[frameIndex], 0, nullptr);

    vkCmdTraceRaysKHR(m_commandBuffers[frameIndex], &raygenStridedBufferRegion, &missStridedBufferRegion, &closestHitStridedBufferRegion, &callableBufferRegion,
                      m_surfaceExtent.width, m_surfaceExtent.height, 1);

    VkImageMemoryBarrier generalToPresentSrc =
        createImageMemoryBarrier(m_swapchainImages[frameIndex], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    vkCmdPipelineBarrier(m_commandBuffers[frameIndex], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &generalToPresentSrc);

    VK_CHECK(vkEndCommandBuffer(m_commandBuffers[frameIndex]));
}

void Application::updateCameraAndPushData(const uint32_t& frameTime) {
    double mouseXInput;
    double mouseYInput;

    glfwGetCursorPos(window, &mouseXInput, &mouseYInput);
    glfwSetCursorPos(window, 0, 0);

    float mouseX = static_cast<float>(mouseXInput);
    float mouseY = static_cast<float>(mouseYInput);

    float rotateSpeedModifier = 0.001f;

    m_camera.orientation.x += rotateSpeedModifier * mouseX;
    if (m_camera.orientation.x > 2.0f * PI) {
        m_camera.orientation.x -= 2.0f * PI;
    } else if (m_camera.orientation.x < 0.0f) {
        m_camera.orientation.x += 2.0f * PI;
    }

    float epsilon = 0.00001f;

    m_camera.orientation.y += rotateSpeedModifier * mouseY;
    if (m_camera.orientation.y > PI * 0.5f) {
        m_camera.orientation.y = PI * 0.5f - epsilon;
    } else if (m_camera.orientation.y < -PI * 0.5f) {
        m_camera.orientation.y = -PI * 0.5f + epsilon;
    }

    glm::vec3 globalUp      = glm::vec3(0.0f, -1.0f, 0.0f);
    glm::vec3 globalRight   = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 globalForward = glm::vec3(0.0f, 0.0f, -1.0f);

    glm::vec3 forward = glm::rotate(globalForward, m_camera.orientation.x, globalUp);
    glm::vec3 right   = glm::rotate(globalRight, m_camera.orientation.x, globalUp);

    glm::vec3 deltaPosition = glm::vec3();

    if (m_keyStates[GLFW_KEY_W].pressed) {
        deltaPosition += forward;
    }

    if (m_keyStates[GLFW_KEY_S].pressed) {
        deltaPosition -= forward;
    }

    if (m_keyStates[GLFW_KEY_A].pressed) {
        deltaPosition -= right;
    }

    if (m_keyStates[GLFW_KEY_D].pressed) {
        deltaPosition += right;
    }

    if (m_keyStates[GLFW_KEY_SPACE].pressed) {
        deltaPosition += globalUp;
    }

    if (m_keyStates[GLFW_KEY_LEFT_CONTROL].pressed) {
        deltaPosition -= globalUp;
    }

    float dt = frameTime * 0.000001f; // Time in seconds

    glm::vec3 acceleration = glm::vec3();
    if (deltaPosition != glm::vec3()) {
        acceleration = glm::normalize(deltaPosition) * ACCELERATION_FACTOR;
    }
    glm::vec3 offset = (0.5f * acceleration * dt + m_camera.velocity) * dt;

    m_camera.velocity += acceleration * dt;
    m_camera.velocity *= 0.99f;

    m_camera.position += offset;

    m_rasterPushData.cameraTransformation = glm::transpose(glm::translate(glm::identity<glm::mat4>(), -m_camera.position));
    m_rasterPushData.cameraTransformation = glm::rotate(m_rasterPushData.cameraTransformation, static_cast<float>(m_camera.orientation.x), globalUp);
    m_rasterPushData.cameraTransformation = glm::rotate(m_rasterPushData.cameraTransformation, static_cast<float>(m_camera.orientation.y), globalRight);

    m_rayTracingPushData.cameraTransformationInverse = glm::inverse(m_rasterPushData.cameraTransformation);
}

void Application::updateSurfaceDependantStructures(const uint32_t& graphicsQueueFamilyIndex) {

    int width  = 0;
    int height = 0;
    do {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    } while (width == 0 || height == 0);

    vkDeviceWaitIdle(m_device);

    for (VkFramebuffer& framebuffer : m_framebuffers) {
        vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    }

    vkDestroyRenderPass(m_device, m_renderPass, nullptr);

    vkDestroyImageView(m_device, m_depthImageView, nullptr);
    vkFreeMemory(m_device, m_depthImageMemory, nullptr);
    vkDestroyImage(m_device, m_depthImage, nullptr);

    for (VkImageView& imageView : m_swapchainImageViews) {
        vkDestroyImageView(m_device, imageView, nullptr);
    }

    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &m_surfaceCapabilities));
    m_surfaceExtent = getSurfaceExtent(window, m_surfaceCapabilities);

    m_rasterPushData.oneOverAspectRatio = static_cast<float>(m_surfaceExtent.height) / static_cast<float>(m_surfaceExtent.width);

    VkSwapchainKHR newSwapchain =
        createSwapchain(m_device, m_surface, m_surfaceFormat, m_presentMode, m_swapchainImageCount, graphicsQueueFamilyIndex, m_surfaceExtent, m_swapchain);
    vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    m_swapchain = newSwapchain;

    vkGetSwapchainImagesKHR(m_device, m_swapchain, &m_swapchainImageCount, m_swapchainImages.data());
    m_swapchainImageViews = getSwapchainImageViews(m_device, m_swapchainImages, m_surfaceFormat.format);

    VkDescriptorImageInfo descriptorSwapchainImageInfo = {};
    descriptorSwapchainImageInfo.imageLayout           = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writeDescriptorSet = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writeDescriptorSet.dstBinding           = 3;
    writeDescriptorSet.dstArrayElement      = 0;
    writeDescriptorSet.descriptorType       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writeDescriptorSet.descriptorCount      = 1;
    writeDescriptorSet.pImageInfo           = &descriptorSwapchainImageInfo;

    for (size_t i = 0; i < m_swapchainImageCount; ++i) {
        descriptorSwapchainImageInfo.imageView = m_swapchainImageViews[i];
        writeDescriptorSet.dstSet              = m_descriptorSets[i];

        vkUpdateDescriptorSets(m_device, 1, &writeDescriptorSet, 0, nullptr);
    }

    m_depthImage = createImage(m_device, m_surfaceExtent, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_FORMAT_D32_SFLOAT_S8_UINT);
    VkMemoryRequirements depthImageMemoryRequirements;
    vkGetImageMemoryRequirements(m_device, m_depthImage, &depthImageMemoryRequirements);
    m_depthImageMemory =
        allocateVulkanObjectMemory(m_device, depthImageMemoryRequirements, m_physicalDeviceMemoryProperties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkBindImageMemory(m_device, m_depthImage, m_depthImageMemory, 0);
    m_depthImageView = createImageView(m_device, m_depthImage, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_IMAGE_ASPECT_DEPTH_BIT);

    m_renderPass   = createRenderPass();
    m_framebuffers = createFramebuffers();
}

#ifdef _DEBUG
VkBool32 VKAPI_CALL Application::debugUtilsCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                                    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* /*pUserData*/) {
    const char* severity = (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                               ? "ERROR"
                               : (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
                                     ? "WARNING"
                                     : (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) ? "INFO" : "VERBOSE";

    const char* type = (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
                           ? "GENERAL"
                           : (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) ? "VALIDATION" : "PERFORMANCE";

    printf("%s-%s: %s\n\n", severity, type, pCallbackData->pMessage);

    return VK_FALSE;
}
#endif