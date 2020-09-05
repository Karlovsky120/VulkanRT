#include "common.h"

#include "commandPools.h"
#include "rayTracing.h"
#include "resources.h"
#include "sharedStructures.h"
#include "swapchain.h"

#pragma warning(push, 0)
#define VK_ENABLE_BETA_EXTENSIONS
#define VOLK_IMPLEMENTATION
#include "volk.h"

#define GLFW_INCLUDE_VULKAN
#include "glfw3.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_XYZW_ONLY
#include "glm/fwd.hpp"
#include "glm/gtx/rotate_vector.hpp"
#include "glm/mat4x4.hpp"
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <vector>
#pragma warning(pop)

#define API_DUMP 0
#define VERBOSE  0
#define INFO     0

#define PI 3.1415926535897932384f

#define WIDTH  1280
#define HEIGHT 720
#define FOV    glm::radians(100.0f)
#define NEAR   0.001f

#define ACCELERATION_FACTOR 300.0f

#define STAGING_BUFFER_SIZE 67'108'864 /// 64MB

#define MAX_FRAMES_IN_FLIGHT 2
#define UI_UPDATE_PERIOD     500'000

#define INDEX_RAYGEN      0
#define INDEX_CLOSEST_HIT 1
#define INDEX_MISS        2

struct Camera {
    glm::vec2 orientation = glm::vec2();
    glm::vec3 position    = glm::vec3();
    glm::vec3 velocity    = glm::vec3();
};

#ifdef _DEBUG
static VkBool32 VKAPI_CALL debugUtilsCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes,
#pragma warning(suppress : 4100) // Unreferenced formal parameter (pUserData)
                                              const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
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

VkInstance createInstance() {
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
    const char* layers[] = {
#if API_DUMP
        "VK_LAYER_LUNARG_api_dump",
#endif
        "VK_LAYER_KHRONOS_validation"
    };

    createInfo.enabledLayerCount   = ARRAYSIZE(layers);
    createInfo.ppEnabledLayerNames = layers;
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

uint32_t getGraphicsQueueFamilyIndex(const VkPhysicalDevice physicalDevice) {
    uint32_t queueFamilyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, 0);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    uint32_t graphicsQueueFamilyIndex = UINT32_MAX;
    for (size_t j = 0; j < queueFamilyCount; ++j) {
        if (queueFamilies[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsQueueFamilyIndex = static_cast<uint32_t>(j);
            break;
        }
    }

    return graphicsQueueFamilyIndex;
}

VkPhysicalDevice pickPhysicalDevice(const VkInstance instance, const VkSurfaceKHR surface) {
    uint32_t physicalDeviceCount;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, 0));

    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data()));

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
        VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, graphicsQueueIndex, surface, &presentSupported));

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

VkRenderPass createRenderPass(const VkDevice device, const VkFormat surfaceFormat) {
    VkAttachmentDescription attachments[2] = {};
    attachments[0].format                  = surfaceFormat;
    attachments[0].samples                 = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp                  = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp                 = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].initialLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout             = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

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
    renderPassCreateInfo.attachmentCount        = ARRAYSIZE(attachments);
    renderPassCreateInfo.pAttachments           = attachments;
    renderPassCreateInfo.subpassCount           = 1;
    renderPassCreateInfo.pSubpasses             = &subpass;
    renderPassCreateInfo.dependencyCount        = 1;
    renderPassCreateInfo.pDependencies          = &subpassDependency;

    VkRenderPass renderPass = 0;
    VK_CHECK(vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &renderPass));

    return renderPass;
}

std::vector<VkFramebuffer> createFramebuffers(const VkDevice device, const VkRenderPass renderPass, const uint32_t swapchainImageCount,
                                              const std::vector<VkImageView>& swapchainImageViews, const VkImageView depthImageView,
                                              const VkExtent2D framebufferArea) {
    VkFramebufferCreateInfo framebufferCreateInfo = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebufferCreateInfo.renderPass              = renderPass;
    framebufferCreateInfo.attachmentCount         = 2;
    framebufferCreateInfo.width                   = framebufferArea.width;
    framebufferCreateInfo.height                  = framebufferArea.height;
    framebufferCreateInfo.layers                  = 1;

    std::vector<VkFramebuffer> framebuffers(swapchainImageCount);
    for (size_t i = 0; i < swapchainImageCount; ++i) {
        VkImageView attachments[2]         = {swapchainImageViews[i], depthImageView};
        framebufferCreateInfo.pAttachments = attachments;
        VK_CHECK(vkCreateFramebuffer(device, &framebufferCreateInfo, nullptr, &framebuffers[i]));
    }

    return framebuffers;
}

VkShaderModule loadShader(const VkDevice device, const char* pathToSource) {
    FILE* source;
    fopen_s(&source, pathToSource, "rb");
    assert(source);

    fseek(source, 0, SEEK_END);
    size_t length = static_cast<size_t>(ftell(source));
    assert(length > 0);
    fseek(source, 0, SEEK_SET);

    char*  buffer    = new char[length];
    size_t readChars = fread(buffer, 1, length, source);
    assert(length == readChars);

    VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize                 = length;
    createInfo.pCode                    = reinterpret_cast<uint32_t*>(buffer);

    VkShaderModule shaderModule = 0;
    VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule));

    delete[] buffer;

    return shaderModule;
}

VkPipeline createRasterPipeline(const VkDevice device, const VkPipelineLayout pipelineLayout, const VkShaderModule vertexShader,
                                const VkShaderModule fragmentShader, const VkRenderPass renderPass, const VkPipelineCache pipelineCache) {
    VkGraphicsPipelineCreateInfo createInfo = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages(2, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO});
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

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicStateCreateInfo.dynamicStateCount                = ARRAYSIZE(dynamicStates);
    dynamicStateCreateInfo.pDynamicStates                   = dynamicStates;
    createInfo.pDynamicState                                = &dynamicStateCreateInfo;

    createInfo.layout     = pipelineLayout;
    createInfo.renderPass = renderPass;

    VkPipeline pipeline = 0;
    VK_CHECK(vkCreateGraphicsPipelines(device, pipelineCache, 1, &createInfo, nullptr, &pipeline));

    return pipeline;
}

VkPipeline createRayTracePipeline(const VkDevice device, const VkPipelineLayout pipelineLayout, const VkShaderModule raygenShaderModule,
                                  const VkShaderModule closestHitShaderModule, const VkShaderModule missShaderModule, const VkPipelineCache pipelineCache) {
    std::vector<VkPipelineShaderStageCreateInfo> shaderStagesCreateInfos(3, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO});
    shaderStagesCreateInfos[INDEX_RAYGEN].stage  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    shaderStagesCreateInfos[INDEX_RAYGEN].module = raygenShaderModule;
    shaderStagesCreateInfos[INDEX_RAYGEN].pName  = "main";

    shaderStagesCreateInfos[INDEX_CLOSEST_HIT].stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    shaderStagesCreateInfos[INDEX_CLOSEST_HIT].module = closestHitShaderModule;
    shaderStagesCreateInfos[INDEX_CLOSEST_HIT].pName  = "main";

    shaderStagesCreateInfos[INDEX_MISS].stage  = VK_SHADER_STAGE_MISS_BIT_KHR;
    shaderStagesCreateInfos[INDEX_MISS].module = missShaderModule;
    shaderStagesCreateInfos[INDEX_MISS].pName  = "main";

    std::vector<VkRayTracingShaderGroupCreateInfoKHR> rayTracingShaderGroupCreateInfos(3, {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR});
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

    VkPipelineLibraryCreateInfoKHR libraryCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR};

    VkRayTracingPipelineCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    createInfo.stageCount                        = static_cast<uint32_t>(shaderStagesCreateInfos.size());
    createInfo.pStages                           = shaderStagesCreateInfos.data();
    createInfo.groupCount                        = static_cast<uint32_t>(rayTracingShaderGroupCreateInfos.size());
    createInfo.pGroups                           = rayTracingShaderGroupCreateInfos.data();
    createInfo.maxRecursionDepth                 = 1;
    createInfo.layout                            = pipelineLayout;
    createInfo.libraries                         = libraryCreateInfo;

    VkPipeline pipeline = 0;
    VK_CHECK(vkCreateRayTracingPipelinesKHR(device, pipelineCache, 1, &createInfo, nullptr, &pipeline));

    return pipeline;
}

void recordCommandBuffer(const VkCommandBuffer commandBuffer, const VkRenderPass renderPass, const VkFramebuffer& framebuffer, const VkExtent2D renderArea,
                         const VkPipeline pipeline, const VkPipelineLayout pipelineLayout, const VkDescriptorSet descriptorSet, const RasterPushData& pushData,
                         uint32_t indexCount) {
    VkCommandBufferBeginInfo commandBufferBeginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

    VkViewport viewport = {};
    viewport.width      = static_cast<float>(renderArea.width);
    viewport.height     = static_cast<float>(renderArea.height);
    viewport.x          = 0;
    viewport.y          = 0;
    viewport.minDepth   = 1.0f;
    viewport.maxDepth   = 0.0f;

    VkRect2D scissor = {};
    scissor.offset   = {0, 0};
    scissor.extent   = renderArea;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    VkRenderPassBeginInfo renderPassBeginInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassBeginInfo.renderPass            = renderPass;
    renderPassBeginInfo.renderArea.offset     = {0, 0};
    renderPassBeginInfo.renderArea.extent     = renderArea;

    VkClearValue colorImageClearColor   = {0.0f, 0.0f, 0.2f, 1.0f};
    VkClearValue depthImageClearColor   = {0.0f, 0.0f, 0.0f, 0.0f};
    VkClearValue imageClearColors[2]    = {colorImageClearColor, depthImageClearColor};
    renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(ARRAYSIZE(imageClearColors));
    renderPassBeginInfo.pClearValues    = imageClearColors;
    renderPassBeginInfo.framebuffer     = framebuffer;
    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(RasterPushData), &pushData);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    vkCmdDraw(commandBuffer, indexCount, 1, 0, 0);

    vkCmdEndRenderPass(commandBuffer);

    VK_CHECK(vkEndCommandBuffer(commandBuffer));
}

void updateCameraAndPushData(GLFWwindow* window, Camera& camera, RasterPushData& rasterPushData,
                             RayTracePushData rayTracePushData, const uint32_t frameTime) {
    double mouseXInput;
    double mouseYInput;

    glfwGetCursorPos(window, &mouseXInput, &mouseYInput);
    glfwSetCursorPos(window, 0, 0);

    float mouseX = static_cast<float>(mouseXInput);
    float mouseY = static_cast<float>(mouseYInput);

    float rotateSpeedModifier = 0.001f;

    camera.orientation.x += rotateSpeedModifier * mouseX;
    if (camera.orientation.x > 2.0f * PI) {
        camera.orientation.x -= 2.0f * PI;
    } else if (camera.orientation.x < 0.0f) {
        camera.orientation.x += 2.0f * PI;
    }

    float epsilon = 0.00001f;

    camera.orientation.y += rotateSpeedModifier * mouseY;
    if (camera.orientation.y > PI * 0.5f) {
        camera.orientation.y = PI * 0.5f - epsilon;
    } else if (camera.orientation.y < -PI * 0.5f) {
        camera.orientation.y = -PI * 0.5f + epsilon;
    }

    glm::vec3 globalUp      = glm::vec3(0.0f, -1.0f, 0.0f);
    glm::vec3 globalRight   = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 globalForward = glm::vec3(0.0f, 0.0f, -1.0f);

    glm::vec3 forward = glm::rotate(globalForward, camera.orientation.x, globalUp);
    glm::vec3 right   = glm::rotate(globalRight, camera.orientation.x, globalUp);

    glm::vec3 deltaPosition = glm::vec3();

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        deltaPosition += forward;
    }

    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        deltaPosition -= forward;
    }

    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        deltaPosition -= right;
    }

    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        deltaPosition += right;
    }

    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        deltaPosition += globalUp;
    }

    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) {
        deltaPosition -= globalUp;
    }

    float dt = frameTime * 0.000001f; // Time in seconds

    glm::vec3 acceleration = glm::vec3();
    if (deltaPosition != glm::vec3()) {
        acceleration = glm::normalize(deltaPosition) * ACCELERATION_FACTOR;
    }
    glm::vec3 offset = (0.5f * acceleration * dt + camera.velocity) * dt;

    camera.velocity += acceleration * dt;
    camera.velocity *= 0.99f;

    camera.position += offset;

    rasterPushData.cameraTransformation = glm::transpose(glm::translate(glm::identity<glm::mat4>(), -camera.position));
    rasterPushData.cameraTransformation = glm::rotate(rasterPushData.cameraTransformation, static_cast<float>(camera.orientation.x), globalUp);
    rasterPushData.cameraTransformation = glm::rotate(rasterPushData.cameraTransformation, static_cast<float>(camera.orientation.y), globalRight);

    rayTracePushData.cameraTransformationInverse = glm::inverse(rasterPushData.cameraTransformation);
}

void updateSurfaceDependantStructures(const VkDevice device, const VkPhysicalDevice physicalDevice, GLFWwindow* window, const VkSurfaceKHR surface,
                                      VkSwapchainKHR& swapchain, std::vector<VkImageView>& swapchainImageViews, VkImageView& depthImageView,
                                      VkImage& depthImage, VkDeviceMemory& depthImageMemory, VkRenderPass& renderPass, std::vector<VkFramebuffer>& framebuffers,
                                      VkImageView& rayTracingImageView, VkImage& rayTracingImage, VkDeviceMemory& rayTracingImageMemory,
                                      VkSurfaceCapabilitiesKHR& surfaceCapabilities, VkExtent2D& surfaceExtent,
                                      const VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties, const VkSurfaceFormatKHR surfaceFormat,
                                      const VkPresentModeKHR presentMode, const uint32_t swapchainImageCount, const uint32_t graphicsQueueFamilyIndex,
                                      float& oneOverAspectRatio, float& aspectRatio) {

    int width  = 0;
    int height = 0;
    do {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    } while (width == 0 || height == 0);

    vkDeviceWaitIdle(device);

    for (VkFramebuffer& framebuffer : framebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }

    vkDestroyRenderPass(device, renderPass, nullptr);

    vkDestroyImageView(device, depthImageView, nullptr);
    vkFreeMemory(device, depthImageMemory, nullptr);
    vkDestroyImage(device, depthImage, nullptr);

    for (VkImageView& imageView : swapchainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }

    vkDestroyImageView(device, rayTracingImageView, nullptr);
    vkFreeMemory(device, rayTracingImageMemory, nullptr);
    vkDestroyImage(device, rayTracingImage, nullptr);

    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities));
    surfaceExtent = getSurfaceExtent(window, surfaceCapabilities);

    oneOverAspectRatio = static_cast<float>(surfaceExtent.height) / static_cast<float>(surfaceExtent.width);
    aspectRatio        = static_cast<float>(surfaceExtent.width) / static_cast<float>(surfaceExtent.height);

    VkSwapchainKHR newSwapchain =
        createSwapchain(device, surface, surfaceFormat, presentMode, swapchainImageCount, graphicsQueueFamilyIndex, surfaceExtent, swapchain);
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = newSwapchain;

    swapchainImageViews = getSwapchainImageViews(device, swapchain, surfaceFormat.format);

    depthImage = createImage(device, surfaceExtent, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_FORMAT_D32_SFLOAT_S8_UINT);
    VkMemoryRequirements depthImageMemoryRequirements;
    vkGetImageMemoryRequirements(device, depthImage, &depthImageMemoryRequirements);
    depthImageMemory = allocateVulkanObjectMemory(device, depthImageMemoryRequirements, physicalDeviceMemoryProperties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkBindImageMemory(device, depthImage, depthImageMemory, 0);
    depthImageView = createImageView(device, depthImage, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_IMAGE_ASPECT_DEPTH_BIT);

    renderPass   = createRenderPass(device, surfaceFormat.format);
    framebuffers = createFramebuffers(device, renderPass, swapchainImageCount, swapchainImageViews, depthImageView, surfaceExtent);

    rayTracingImage = createImage(device, surfaceExtent, VK_IMAGE_USAGE_STORAGE_BIT, VK_FORMAT_B8G8R8A8_UNORM);
    VkMemoryRequirements rayTracingImageMemoryRequirements;
    vkGetImageMemoryRequirements(device, rayTracingImage, &rayTracingImageMemoryRequirements);
    rayTracingImageMemory =
        allocateVulkanObjectMemory(device, rayTracingImageMemoryRequirements, physicalDeviceMemoryProperties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkBindImageMemory(device, rayTracingImage, rayTracingImageMemory, 0);
    rayTracingImageView = createImageView(device, rayTracingImage, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
}

int main(int, char*[]) {

    if (!glfwInit()) {
        printf("Failed to initialize GLFW!");
        return -1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* window;
    window = glfwCreateWindow(WIDTH, HEIGHT, "VulkanRT", NULL, NULL);
    if (!window) {
        printf("Failed to create GLFW window!");

        glfwTerminate();
        return -1;
    }

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!glfwRawMouseMotionSupported()) {
        printf("Raw mouse motion not supported!");

        glfwTerminate();
        return -1;
    }

    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    glfwSetCursorPos(window, 0, 0);

    if (volkInitialize() != VK_SUCCESS) {
        printf("Failed to initialize Volk!");
        glfwTerminate();
        return -1;
    }

    assert(volkGetInstanceVersion() >= VK_API_VERSION_1_2);

    VkInstance instance = createInstance();

    volkLoadInstance(instance);

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

    VkDebugUtilsMessengerEXT debugUtilsMessenger;
    VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance, &debugUtilsMessengerCreateInfo, nullptr, &debugUtilsMessenger));
#endif

    VkSurfaceKHR surface = 0;
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        printf("Failed to create window surface!");

#ifdef _DEBUG
        vkDestroyDebugUtilsMessengerEXT(instance, debugUtilsMessenger, nullptr);
#endif

        vkDestroyInstance(instance, nullptr);

        glfwTerminate();
        return -1;
    }

    VkPhysicalDevice physicalDevice = 0;

    try {
        physicalDevice = pickPhysicalDevice(instance, surface);
    } catch (std::runtime_error& e) {
        printf("%s", e.what());

        vkDestroySurfaceKHR(instance, surface, nullptr);

#ifdef _DEBUG
        vkDestroyDebugUtilsMessengerEXT(instance, debugUtilsMessenger, nullptr);
#endif

        vkDestroyInstance(instance, nullptr);

        glfwTerminate();
        return -1;
    }

    VkPhysicalDeviceRayTracingPropertiesKHR physicalDeviceRayTracingProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2             physicalDeviceProperties2          = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    physicalDeviceProperties2.pNext                                            = &physicalDeviceRayTracingProperties;
    vkGetPhysicalDeviceProperties2(physicalDevice, &physicalDeviceProperties2);

    VkPhysicalDeviceProperties physicalDeviceProperties = physicalDeviceProperties2.properties;

    printf("Selected GPU: %s\n", physicalDeviceProperties.deviceName);

    uint32_t graphicsQueueFamilyIndex = getGraphicsQueueFamilyIndex(physicalDevice);

    const float             queuePriorities       = 1.0f;
    VkDeviceQueueCreateInfo deviceQueueCreateInfo = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    deviceQueueCreateInfo.queueCount              = 1;
    deviceQueueCreateInfo.queueFamilyIndex        = graphicsQueueFamilyIndex;
    deviceQueueCreateInfo.pQueuePriorities        = &queuePriorities;

    std::vector<const char*> deviceExtensions = {
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

    VkDevice device = 0;
    VK_CHECK(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device));

    volkLoadDevice(device);

    VkSurfaceFormatKHR surfaceFormat = {};
    surfaceFormat.format             = VK_FORMAT_B8G8R8A8_UNORM;
    surfaceFormat.colorSpace         = VK_COLORSPACE_SRGB_NONLINEAR_KHR;

    if (!surfaceFormatSupported(physicalDevice, surface, surfaceFormat)) {
        printf("Requested surface format not supported!");

        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);

#ifdef _DEBUG
        vkDestroyDebugUtilsMessengerEXT(instance, debugUtilsMessenger, nullptr);
#endif

        vkDestroyInstance(instance, nullptr);

        glfwTerminate();
        return -1;
    }

    VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities));

    uint32_t requestedSwapchainImageCount;
    try {
        requestedSwapchainImageCount = getSwapchainImageCount(surfaceCapabilities);
    } catch (std::runtime_error& e) {
        printf("%s", e.what());

        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);

#ifdef _DEBUG
        vkDestroyDebugUtilsMessengerEXT(instance, debugUtilsMessenger, nullptr);
#endif

        vkDestroyInstance(instance, nullptr);

        glfwTerminate();
        return -1;
    }

    VkExtent2D surfaceExtent = getSurfaceExtent(window, surfaceCapabilities);

    VkPresentModeKHR presentMode = getPresentMode(physicalDevice, surface);
    VkSwapchainKHR   swapchain =
        createSwapchain(device, surface, surfaceFormat, presentMode, requestedSwapchainImageCount, graphicsQueueFamilyIndex, surfaceExtent, VK_NULL_HANDLE);

    std::vector<VkImageView> swapchainImageViews = getSwapchainImageViews(device, swapchain, surfaceFormat.format);
    uint32_t                 swapchainImageCount = static_cast<uint32_t>(swapchainImageViews.size());

    VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &physicalDeviceMemoryProperties);

    VkImage        depthImage       = createImage(device, surfaceExtent, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_FORMAT_D32_SFLOAT_S8_UINT);
    VkDeviceMemory depthImageMemory = 0;
    try {
        VkMemoryRequirements depthImageMemoryRequirements;
        vkGetImageMemoryRequirements(device, depthImage, &depthImageMemoryRequirements);
        depthImageMemory =
            allocateVulkanObjectMemory(device, depthImageMemoryRequirements, physicalDeviceMemoryProperties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    } catch (std::runtime_error& e) {
        printf("%s", e.what());

        vkDestroyImage(device, depthImage, nullptr);

        for (VkImageView& imageView : swapchainImageViews) {
            vkDestroyImageView(device, imageView, nullptr);
        }

        vkDestroySwapchainKHR(device, swapchain, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);

#ifdef _DEBUG
        vkDestroyDebugUtilsMessengerEXT(instance, debugUtilsMessenger, nullptr);
#endif

        vkDestroyInstance(instance, nullptr);

        glfwTerminate();
        return -1;
    }

    vkBindImageMemory(device, depthImage, depthImageMemory, 0);

    VkImageView depthImageView = createImageView(device, depthImage, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_IMAGE_ASPECT_DEPTH_BIT);

    VkRenderPass renderPass = createRenderPass(device, surfaceFormat.format);

    std::vector<VkFramebuffer> framebuffers = createFramebuffers(device, renderPass, swapchainImageCount, swapchainImageViews, depthImageView, surfaceExtent);

    Buffer stagingBuffer =
        createBuffer(device, STAGING_BUFFER_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, physicalDeviceMemoryProperties, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

    VkCommandPool transferCommandPool = createCommandPool(device, graphicsQueueFamilyIndex);

    VkQueue queue = 0;
    vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &queue);

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
    Buffer   vertexBuffer     = createBuffer(device, vertexBufferSize, bufferUsageFlags, physicalDeviceMemoryProperties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                       VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);

    uploadToDeviceLocalBuffer(device, cubeVertices, stagingBuffer.buffer, stagingBuffer.memory, vertexBuffer.buffer, transferCommandPool, queue);

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
    Buffer   indexBuffer     = createBuffer(device, indexBufferSize, bufferUsageFlags, physicalDeviceMemoryProperties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                      VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);

    uploadToDeviceLocalBuffer(device, cubeIndices, stagingBuffer.buffer, stagingBuffer.memory, indexBuffer.buffer, transferCommandPool, queue);

    VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    pipelineCacheCreateInfo.initialDataSize           = 0;

    VkPipelineCache pipelineCache = 0;
    VK_CHECK(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, nullptr, &pipelineCache));

    std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings(4);
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

    VkDescriptorSetLayout descriptorSetLayout = 0;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayout));

    VkPushConstantRange rasterPushConstantRange = {};
    rasterPushConstantRange.offset              = 0;
    rasterPushConstantRange.size                = sizeof(RasterPushData);
    rasterPushConstantRange.stageFlags          = VK_SHADER_STAGE_VERTEX_BIT;

    VkPipelineLayoutCreateInfo rasterPipelineLayoutCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    rasterPipelineLayoutCreateInfo.pushConstantRangeCount     = 1;
    rasterPipelineLayoutCreateInfo.pPushConstantRanges        = &rasterPushConstantRange;
    rasterPipelineLayoutCreateInfo.setLayoutCount             = 1;
    rasterPipelineLayoutCreateInfo.pSetLayouts                = &descriptorSetLayout;

    VkPipelineLayout rasterPipelineLayout = 0;
    VK_CHECK(vkCreatePipelineLayout(device, &rasterPipelineLayoutCreateInfo, nullptr, &rasterPipelineLayout));

    VkShaderModule vertexShader   = loadShader(device, "src/shaders/spirv/vertexShader.spv");
    VkShaderModule fragmentShader = loadShader(device, "src/shaders/spirv/fragmentShader.spv");

    VkPipeline rasterPipeline = createRasterPipeline(device, rasterPipelineLayout, vertexShader, fragmentShader, renderPass, pipelineCache);

    vkDestroyShaderModule(device, fragmentShader, nullptr);
    vkDestroyShaderModule(device, vertexShader, nullptr);

    AccelerationStructure bottomLevelAccelerationStructure = createBottomAccelerationStructure(
        device, static_cast<uint32_t>(cubeVertices.size()), static_cast<uint32_t>(cubeIndices.size() / 3), vertexBuffer.deviceAddress,
        indexBuffer.deviceAddress, physicalDeviceMemoryProperties, queue, graphicsQueueFamilyIndex);

    AccelerationStructure topLevelAccelerationStructure =
        createTopAccelerationStructure(device, bottomLevelAccelerationStructure, physicalDeviceMemoryProperties, queue, graphicsQueueFamilyIndex);

    VkImage        rayTracingImage       = createImage(device, surfaceExtent, VK_IMAGE_USAGE_STORAGE_BIT, VK_FORMAT_B8G8R8A8_UNORM);
    VkDeviceMemory rayTracingImageMemory = 0;
    try {
        VkMemoryRequirements rayTracingImageMemoryRequirements;
        vkGetImageMemoryRequirements(device, rayTracingImage, &rayTracingImageMemoryRequirements);
        rayTracingImageMemory =
            allocateVulkanObjectMemory(device, rayTracingImageMemoryRequirements, physicalDeviceMemoryProperties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    } catch (std::runtime_error& e) {
        printf("%s", e.what());

        vkDestroyImage(device, rayTracingImage, nullptr);

        vkFreeMemory(device, topLevelAccelerationStructure.memory, nullptr);
        vkDestroyAccelerationStructureKHR(device, topLevelAccelerationStructure.accelerationStructure, nullptr);

        vkFreeMemory(device, bottomLevelAccelerationStructure.memory, nullptr);
        vkDestroyAccelerationStructureKHR(device, bottomLevelAccelerationStructure.accelerationStructure, nullptr);

        vkDestroyPipeline(device, rasterPipeline, nullptr);
        vkDestroyPipelineLayout(device, rasterPipelineLayout, nullptr);
        vkDestroyPipelineCache(device, pipelineCache, nullptr);

        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        vkDestroyBuffer(device, indexBuffer.buffer, nullptr);
        vkFreeMemory(device, indexBuffer.memory, nullptr);

        vkDestroyBuffer(device, vertexBuffer.buffer, nullptr);
        vkFreeMemory(device, vertexBuffer.memory, nullptr);

        for (VkFramebuffer& framebuffer : framebuffers) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }

        vkDestroyImageView(device, depthImageView, nullptr);
        vkDestroyImage(device, depthImage, nullptr);
        vkFreeMemory(device, depthImageMemory, nullptr);

        for (VkImageView& imageView : swapchainImageViews) {
            vkDestroyImageView(device, imageView, nullptr);
        }

        vkDestroySwapchainKHR(device, swapchain, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);

#ifdef _DEBUG
        vkDestroyDebugUtilsMessengerEXT(instance, debugUtilsMessenger, nullptr);
#endif

        vkDestroyInstance(instance, nullptr);

        glfwTerminate();
        return -1;
    }

    vkBindImageMemory(device, rayTracingImage, rayTracingImageMemory, 0);

    VkImageView rayTracingImageView = createImageView(device, rayTracingImage, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

    VkPushConstantRange rayTracePushConstantRange = {};
    rayTracePushConstantRange.offset              = 0;
    rayTracePushConstantRange.size                = sizeof(RayTracePushData);
    rayTracePushConstantRange.stageFlags          = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkPipelineLayoutCreateInfo rayTracePipelineLayoutCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    rayTracePipelineLayoutCreateInfo.pushConstantRangeCount     = 1;
    rayTracePipelineLayoutCreateInfo.pPushConstantRanges        = &rayTracePushConstantRange;
    rayTracePipelineLayoutCreateInfo.setLayoutCount             = 1;
    rayTracePipelineLayoutCreateInfo.pSetLayouts                = &descriptorSetLayout;

    VkPipelineLayout rayTracePipelineLayout = 0;
    VK_CHECK(vkCreatePipelineLayout(device, &rayTracePipelineLayoutCreateInfo, nullptr, &rayTracePipelineLayout));

    VkShaderModule raygenShader     = loadShader(device, "src/shaders/spirv/raygenShader.spv");
    VkShaderModule closestHitShader = loadShader(device, "src/shaders/spirv/closestHitShader.spv");
    VkShaderModule missShader       = loadShader(device, "src/shaders/spirv/missShader.spv");

    VkPipeline rayTracePipeline = createRayTracePipeline(device, rayTracePipelineLayout, raygenShader, closestHitShader, missShader, pipelineCache);

    vkDestroyShaderModule(device, missShader, nullptr);
    vkDestroyShaderModule(device, closestHitShader, nullptr);
    vkDestroyShaderModule(device, raygenShader, nullptr);

    // clang-format off
    std::vector<VkDescriptorPoolSize> descriptorPoolSizes = {
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1}
    };
    // clang-format on

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptorPoolCreateInfo.poolSizeCount              = static_cast<uint32_t>(descriptorPoolSizes.size());
    descriptorPoolCreateInfo.pPoolSizes                 = descriptorPoolSizes.data();
    descriptorPoolCreateInfo.maxSets                    = 1;

    VkDescriptorPool descriptorPool = 0;
    VK_CHECK(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &descriptorPool));

    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    descriptorSetAllocateInfo.descriptorPool              = descriptorPool;
    descriptorSetAllocateInfo.descriptorSetCount          = 1;
    descriptorSetAllocateInfo.pSetLayouts                 = &descriptorSetLayout;

    VkDescriptorSet descriptorSet = 0;
    vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &descriptorSet);

    std::vector<VkDescriptorBufferInfo> descriptorBufferInfos(2);
    descriptorBufferInfos[0].buffer = vertexBuffer.buffer;
    descriptorBufferInfos[0].offset = 0;
    descriptorBufferInfos[0].range  = vertexBufferSize;

    descriptorBufferInfos[1].buffer = indexBuffer.buffer;
    descriptorBufferInfos[1].offset = 0;
    descriptorBufferInfos[1].range  = indexBufferSize;

    VkWriteDescriptorSetAccelerationStructureKHR writeDescriptorSetAccelerationStructure = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    writeDescriptorSetAccelerationStructure.accelerationStructureCount                   = 1;
    writeDescriptorSetAccelerationStructure.pAccelerationStructures                      = &topLevelAccelerationStructure.accelerationStructure;

    VkDescriptorImageInfo descriptorImageInfo = {};
    descriptorImageInfo.imageView             = rayTracingImageView;
    descriptorImageInfo.imageLayout           = VK_IMAGE_LAYOUT_GENERAL;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets(3, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET});
    writeDescriptorSets[0].dstSet          = descriptorSet;
    writeDescriptorSets[0].dstBinding      = 0;
    writeDescriptorSets[0].dstArrayElement = 0;
    writeDescriptorSets[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writeDescriptorSets[0].descriptorCount = static_cast<uint32_t>(descriptorBufferInfos.size());
    writeDescriptorSets[0].pBufferInfo     = descriptorBufferInfos.data();

    writeDescriptorSets[1].dstSet          = descriptorSet;
    writeDescriptorSets[1].dstBinding      = 2;
    writeDescriptorSets[1].dstArrayElement = 0;
    writeDescriptorSets[1].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    writeDescriptorSets[1].descriptorCount = 1;
    writeDescriptorSets[1].pNext           = &writeDescriptorSetAccelerationStructure;

    writeDescriptorSets[2].dstSet          = descriptorSet;
    writeDescriptorSets[2].dstBinding      = 3;
    writeDescriptorSets[2].dstArrayElement = 0;
    writeDescriptorSets[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writeDescriptorSets[2].descriptorCount = 1;
    writeDescriptorSets[2].pImageInfo      = &descriptorImageInfo;

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

    VkDeviceSize groupHandleSize        = physicalDeviceRayTracingProperties.shaderGroupHandleSize;
    VkDeviceSize shaderBindingTableSize = groupHandleSize * 3; // Number of shader stages

    std::vector<uint8_t> handleData(shaderBindingTableSize);
    vkGetRayTracingShaderGroupHandlesKHR(device, rayTracePipeline, 0, 3, shaderBindingTableSize, handleData.data());

    Buffer shaderBindingTableBuffer = createBuffer(
        device, shaderBindingTableSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        physicalDeviceMemoryProperties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);
    uploadToDeviceLocalBuffer(device, handleData, stagingBuffer.buffer, stagingBuffer.memory, shaderBindingTableBuffer.buffer, transferCommandPool, queue);

    vkDestroyCommandPool(device, transferCommandPool, nullptr);

    vkFreeMemory(device, stagingBuffer.memory, nullptr);
    vkDestroyBuffer(device, stagingBuffer.buffer, nullptr);

    VkStridedBufferRegionKHR raygenStridedBufferRegion = {};
    raygenStridedBufferRegion.buffer                   = shaderBindingTableBuffer.buffer;
    raygenStridedBufferRegion.offset                   = static_cast<VkDeviceSize>(groupHandleSize * INDEX_RAYGEN);
    raygenStridedBufferRegion.size                     = groupHandleSize;
    raygenStridedBufferRegion.stride                   = groupHandleSize;

    VkStridedBufferRegionKHR closestHitStridedBufferRegion = {};
    closestHitStridedBufferRegion.buffer                   = shaderBindingTableBuffer.buffer;
    closestHitStridedBufferRegion.offset                   = static_cast<VkDeviceSize>(groupHandleSize * INDEX_CLOSEST_HIT);
    closestHitStridedBufferRegion.size                     = groupHandleSize;
    closestHitStridedBufferRegion.stride                   = groupHandleSize;

    VkStridedBufferRegionKHR missStridedBufferRegion = {};
    missStridedBufferRegion.buffer                   = shaderBindingTableBuffer.buffer;
    missStridedBufferRegion.offset                   = static_cast<VkDeviceSize>(groupHandleSize * INDEX_MISS);
    missStridedBufferRegion.size                     = groupHandleSize;
    missStridedBufferRegion.stride                   = groupHandleSize;

    VkCommandPoolCreateInfo commandPoolCreateInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandPoolCreateInfo.flags                   = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    commandPoolCreateInfo.queueFamilyIndex        = graphicsQueueFamilyIndex;

    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandBufferAllocateInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandBufferCount          = 1;

    std::vector<VkCommandPool>   commandPools(swapchainImageCount);
    std::vector<VkCommandBuffer> commandBuffers(swapchainImageCount);
    for (size_t i = 0; i < swapchainImageCount; ++i) {
        VK_CHECK(vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &commandPools[i]));
        commandBufferAllocateInfo.commandPool = commandPools[i];
        VK_CHECK(vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &commandBuffers[i]));
    }

    std::vector<VkSemaphore> imageAvailableSemaphores(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkSemaphore> renderFinishedSemaphores(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkFence>     inFlightFences(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkFence>     imagesInFlight(swapchainImageCount, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreCreateInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo     fenceCreateInfo     = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceCreateInfo.flags                     = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &imageAvailableSemaphores[i]));
        VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &renderFinishedSemaphores[i]));
        VK_CHECK(vkCreateFence(device, &fenceCreateInfo, nullptr, &inFlightFences[i]));
    }

    Camera camera      = {};
    camera.orientation = glm::vec2(0.0f, 0.0f);
    camera.position    = glm::vec3(0.0f, 0.0f, 2.5f);

    RasterPushData rasterPushData      = {};
    rasterPushData.oneOverTanOfHalfFov = 1.0f / tan(0.5f * FOV);
    rasterPushData.oneOverAspectRatio  = static_cast<float>(surfaceExtent.height) / static_cast<float>(surfaceExtent.width);
    rasterPushData.near                = NEAR;

    RayTracePushData rayTracePushData = {};
    rayTracePushData.tanOfHalfFov     = tan(0.5f * FOV);
    rayTracePushData.aspectRatio      = static_cast<float>(surfaceExtent.width) / static_cast<float>(surfaceExtent.height);
    rayTracePushData.oneOverNear      = 1.0f / NEAR;

    uint32_t currentFrame = 0;

    std::chrono::high_resolution_clock::time_point oldTime = std::chrono::high_resolution_clock::now();
    uint32_t                                       time    = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

        uint32_t imageIndex;
        VkResult acquireResult = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            updateSurfaceDependantStructures(device, physicalDevice, window, surface, swapchain, swapchainImageViews, depthImageView, depthImage,
                                             depthImageMemory, renderPass, framebuffers, rayTracingImageView, rayTracingImage, rayTracingImageMemory,
                                             surfaceCapabilities, surfaceExtent, physicalDeviceMemoryProperties, surfaceFormat, presentMode,
                                             swapchainImageCount, graphicsQueueFamilyIndex, rasterPushData.oneOverAspectRatio, rayTracePushData.aspectRatio);

            continue;
        } else if (acquireResult != VK_SUBOPTIMAL_KHR) {
            VK_CHECK(acquireResult);
        }

        if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
            vkWaitForFences(device, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
        }
        imagesInFlight[imageIndex] = inFlightFences[currentFrame];

        vkResetCommandPool(device, commandPools[imageIndex], VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

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

        updateCameraAndPushData(window, camera, rasterPushData, rayTracePushData, frameTime);

        recordCommandBuffer(commandBuffers[imageIndex], renderPass, framebuffers[imageIndex], surfaceExtent, rasterPipeline, rasterPipelineLayout,
                            descriptorSet, rasterPushData, static_cast<uint32_t>(cubeIndices.size()));

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submitInfo         = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.waitSemaphoreCount   = 1;
        submitInfo.pWaitSemaphores      = &imageAvailableSemaphores[currentFrame];
        submitInfo.pWaitDstStageMask    = &waitStage;
        submitInfo.commandBufferCount   = 1;
        submitInfo.pCommandBuffers      = &commandBuffers[imageIndex];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores    = &renderFinishedSemaphores[currentFrame];

        vkResetFences(device, 1, &inFlightFences[currentFrame]);

        VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, inFlightFences[currentFrame]));

        VkPresentInfoKHR presentInfo   = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores    = &renderFinishedSemaphores[currentFrame];
        presentInfo.swapchainCount     = 1;
        presentInfo.pSwapchains        = &swapchain;
        presentInfo.pImageIndices      = &imageIndex;

        VkResult presentResult = vkQueuePresentKHR(queue, &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
            updateSurfaceDependantStructures(device, physicalDevice, window, surface, swapchain, swapchainImageViews, depthImageView, depthImage,
                                             depthImageMemory, renderPass, framebuffers, rayTracingImageView, rayTracingImage, rayTracingImageMemory,
                                             surfaceCapabilities, surfaceExtent, physicalDeviceMemoryProperties, surfaceFormat, presentMode,
                                             swapchainImageCount, graphicsQueueFamilyIndex, rasterPushData.oneOverAspectRatio, rayTracePushData.aspectRatio);

            continue;
        } else {
            VK_CHECK(presentResult);
        }

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    vkDeviceWaitIdle(device);

    for (VkSemaphore& semaphore : imageAvailableSemaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }

    for (VkSemaphore& semaphore : renderFinishedSemaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }

    for (VkFence& fence : inFlightFences) {
        vkDestroyFence(device, fence, nullptr);
    }

    for (size_t i = 0; i < swapchainImageCount; ++i) {
        vkFreeCommandBuffers(device, commandPools[i], 1, &commandBuffers[i]);
        vkDestroyCommandPool(device, commandPools[i], nullptr);
    }

    vkDestroyBuffer(device, shaderBindingTableBuffer.buffer, nullptr);
    vkFreeMemory(device, shaderBindingTableBuffer.memory, nullptr);

    vkDestroyDescriptorPool(device, descriptorPool, nullptr);

    vkDestroyPipeline(device, rayTracePipeline, nullptr);
    vkDestroyPipelineLayout(device, rayTracePipelineLayout, nullptr);

    vkDestroyImageView(device, rayTracingImageView, nullptr);
    vkDestroyImage(device, rayTracingImage, nullptr);
    vkFreeMemory(device, rayTracingImageMemory, nullptr);

    vkFreeMemory(device, topLevelAccelerationStructure.memory, nullptr);
    vkDestroyAccelerationStructureKHR(device, topLevelAccelerationStructure.accelerationStructure, nullptr);

    vkFreeMemory(device, bottomLevelAccelerationStructure.memory, nullptr);
    vkDestroyAccelerationStructureKHR(device, bottomLevelAccelerationStructure.accelerationStructure, nullptr);

    vkDestroyPipeline(device, rasterPipeline, nullptr);
    vkDestroyPipelineLayout(device, rasterPipelineLayout, nullptr);
    vkDestroyPipelineCache(device, pipelineCache, nullptr);

    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    vkDestroyBuffer(device, indexBuffer.buffer, nullptr);
    vkFreeMemory(device, indexBuffer.memory, nullptr);

    vkDestroyBuffer(device, vertexBuffer.buffer, nullptr);
    vkFreeMemory(device, vertexBuffer.memory, nullptr);

    for (VkFramebuffer& framebuffer : framebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }

    vkDestroyImageView(device, depthImageView, nullptr);
    vkDestroyImage(device, depthImage, nullptr);
    vkFreeMemory(device, depthImageMemory, nullptr);

    vkDestroyRenderPass(device, renderPass, nullptr);

    for (VkImageView& imageView : swapchainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }

    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);

#ifdef _DEBUG
    vkDestroyDebugUtilsMessengerEXT(instance, debugUtilsMessenger, nullptr);
#endif

    vkDestroyInstance(instance, nullptr);

    glfwTerminate();
    return 0;
}