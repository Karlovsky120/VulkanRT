#pragma once

#include "common.h"

#include "rayTracing.h"
#include "resources.h"
#include "sharedStructures.h"

#include "common.h"

#pragma warning(push, 0)
#define VK_ENABLE_BETA_EXTENSIONS
#include "volk.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_XYZW_ONLY
#include "glm/fwd.hpp"
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#pragma warning(pop)

struct GLFWwindow;

struct Camera {
    glm::vec2 orientation = glm::vec2();
    glm::vec3 position    = glm::vec3();
    glm::vec3 velocity    = glm::vec3();
};

class Application {
  public:
    void run();

    ~Application();

  private:
    GLFWwindow* window = nullptr;

    VkInstance               m_instance                 = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugUtilsMessenger      = VK_NULL_HANDLE;
    VkSurfaceKHR             m_surface                  = VK_NULL_HANDLE;
    VkPhysicalDevice         m_physicalDevice           = VK_NULL_HANDLE;
    VkDevice                 m_device                   = VK_NULL_HANDLE;
    VkSwapchainKHR           m_swapchain                = VK_NULL_HANDLE;
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
    VkSurfaceCapabilitiesKHR         m_surfaceCapabilities            = {};

    VkSurfaceFormatKHR m_surfaceFormat;
    VkPresentModeKHR   m_presentMode;

    Buffer                m_vertexBuffer                     = {};
    Buffer                m_indexBuffer                      = {};
    Buffer                m_shaderBindingTableBuffer         = {};
    AccelerationStructure m_topLevelAccelerationStructure    = {};
    AccelerationStructure m_bottomLevelAccelerationStructure = {};

    Camera             m_camera             = {};
    RasterPushData     m_rasterPushData     = {};
    RayTracingPushData m_rayTracingPushData = {};

    std::vector<VkImage>         m_swapchainImages;
    std::vector<VkImageView>     m_swapchainImageViews;
    std::vector<VkFramebuffer>   m_framebuffers;
    std::vector<VkDescriptorSet> m_descriptorSets;
    std::vector<VkCommandPool>   m_commandPools;
    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkFence>         m_inFlightFences;
    std::vector<VkSemaphore>     m_renderFinishedSemaphores;
    std::vector<VkSemaphore>     m_imageAvailableSemaphores;

    uint32_t m_queueFamilyIndex = UINT32_MAX;
    uint32_t m_swapchainImageCount = UINT32_MAX;

    VkInstance                 createInstance() const;
    uint32_t                   getGraphicsQueueFamilyIndex(const VkPhysicalDevice& physicalDevice) const;
    VkPhysicalDevice           pickPhysicalDevice() const;
    VkRenderPass               createRenderPass() const;
    std::vector<VkFramebuffer> createFramebuffers() const;
    VkShaderModule             loadShader(const char* pathToSource) const;
    VkPipeline                 createRasterPipeline(const VkShaderModule& vertexShader, const VkShaderModule& fragmentShader) const;
    VkPipeline                 createRayTracingPipeline(const VkShaderModule& raygenShaderModule, const VkShaderModule& closestHitShaderModule,
                                                        const VkShaderModule& missShaderModule) const;
    void                       recordRasterCommandBuffer(const uint32_t& frameIndex, const uint32_t& indexCount) const;
    void                       recordRayTracingCommandBuffer(const uint32_t& frameIndex, const VkStridedBufferRegionKHR& raygenStridedBufferRegion,
                                                             const VkStridedBufferRegionKHR& closestHitStridedBufferRegion, const VkStridedBufferRegionKHR& missStridedBufferRegion,
                                                             const VkStridedBufferRegionKHR& callableBufferRegion) const;
    void                       updateCameraAndPushData(const uint32_t& frameTime, bool& rayTracing);
    void                       updateSurfaceDependantStructures(const uint32_t& graphicsQueueFamilyIndex);

    static VkBool32 VKAPI_CALL debugUtilsCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                                  const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* /*pUserData*/);
};
