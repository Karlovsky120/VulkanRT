#pragma once

#include "common.h"

#include "rayTracing.h"
#include "resources.h"
#include "sharedStructures.h"
#include "swapchain.h"

#pragma warning(push, 0)
#define VK_ENABLE_BETA_EXTENSIONS
#include "volk.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_XYZW_ONLY
#include "glm/fwd.hpp"
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#pragma warning(pop)

#include <map>
#include <memory>
#include <vector>

struct GLFWwindow;

struct KeyState {
    bool    pressed     = false;
    uint8_t transitions = 0;
};

struct Camera {
    glm::vec2 orientation = glm::vec2();
    glm::vec3 position    = glm::vec3();
    glm::vec3 velocity    = glm::vec3();
};

class Application {
  public:
    void run();

    std::map<int, KeyState> m_keyStates;

    ~Application();

  private:
    GLFWwindow* window = nullptr;

    VkInstance               m_instance                 = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugUtilsMessenger      = VK_NULL_HANDLE;
    VkSurfaceKHR             m_surface                  = VK_NULL_HANDLE;
    VkPhysicalDevice         m_physicalDevice           = VK_NULL_HANDLE;
    VkDevice                 m_device                   = VK_NULL_HANDLE;
    VkRenderPass             m_renderPass               = VK_NULL_HANDLE;
    VkDeviceMemory           m_depthImageMemory         = VK_NULL_HANDLE;
    VkImageView              m_depthImageView           = VK_NULL_HANDLE;
    VkImage                  m_depthImage               = VK_NULL_HANDLE;
    VkDescriptorPool         m_descriptorPool           = VK_NULL_HANDLE;
    VkDescriptorSetLayout    m_descriptorSetLayout      = VK_NULL_HANDLE;
    VkPipelineCache          m_pipelineCache            = VK_NULL_HANDLE;
    VkPipelineLayout         m_rasterPipelineLayout     = VK_NULL_HANDLE;
    VkPipeline               m_rasterPipeline           = VK_NULL_HANDLE;
    VkPipelineLayout         m_rayTracingPipelineLayout = VK_NULL_HANDLE;
    VkPipeline               m_rayTracingPipeline       = VK_NULL_HANDLE;
    VkCommandPool            m_transferCommandPool      = VK_NULL_HANDLE;

    VkExtent2D                       m_surfaceExtent                  = {};
    VkPhysicalDeviceMemoryProperties m_physicalDeviceMemoryProperties = {};

    std::unique_ptr<Swapchain> m_swapchain;

    Buffer                m_vertexBuffer                     = {};
    Buffer                m_indexBuffer                      = {};
    Buffer                m_shaderBindingTableBuffer         = {};
    AccelerationStructure m_topLevelAccelerationStructure    = {};
    AccelerationStructure m_bottomLevelAccelerationStructure = {};

    Camera             m_camera             = {};
    RasterPushData     m_rasterPushData     = {};
    RayTracingPushData m_rayTracingPushData = {};

    std::vector<VkFramebuffer>   m_framebuffers;
    std::vector<VkDescriptorSet> m_descriptorSets;
    std::vector<VkCommandPool>   m_commandPools;
    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkFence>         m_inFlightFences;
    std::vector<VkSemaphore>     m_renderFinishedSemaphores;
    std::vector<VkSemaphore>     m_imageAvailableSemaphores;

    uint32_t m_queueFamilyIndex    = UINT32_MAX;
    uint32_t m_swapchainImageCount = UINT32_MAX;

    const VkInstance                 createInstance() const;
    const uint32_t                   getGraphicsQueueFamilyIndex(const VkPhysicalDevice& physicalDevice) const;
    const VkPhysicalDevice           pickPhysicalDevice() const;
    const VkRenderPass               createRenderPass() const;
    const std::vector<VkFramebuffer> createFramebuffers() const;
    const VkShaderModule             loadShader(const char* pathToSource) const;
    const VkPipeline                 createRasterPipeline(const VkShaderModule& vertexShader, const VkShaderModule& fragmentShader) const;
    const VkPipeline                 createRayTracingPipeline(const VkShaderModule& raygenShaderModule, const VkShaderModule& closestHitShaderModule,
                                                              const VkShaderModule& missShaderModule) const;
    void                             recordRasterCommandBuffer(const uint32_t& frameIndex, const uint32_t& indexCount) const;
    void                             recordRayTracingCommandBuffer(const uint32_t& frameIndex, const VkStridedBufferRegionKHR& raygenStridedBufferRegion,
                                                                   const VkStridedBufferRegionKHR& closestHitStridedBufferRegion, const VkStridedBufferRegionKHR& missStridedBufferRegion,
                                                                   const VkStridedBufferRegionKHR& callableBufferRegion) const;
    void                             updateCameraAndPushData(const uint32_t& frameTime);
    void                             updateSurfaceDependantStructures();
    glm::vec3 getPositionOnSpline(const std::vector<glm::vec3>& controlPoints, const uint32_t currentControl, const glm::mat4& bernie, const float t);

    static VkBool32 VKAPI_CALL debugUtilsCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                                  const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* /*pUserData*/);
};
