#define VK_ENABLE_BETA_EXTENSIONS
#define VOLK_IMPLEMENTATION
#define GLFW_INCLUDE_VULKAN

#include "volk.h"
#include "glfw3.h"

#include <assert.h>
#include <cstdio>
#include <vector>

#define ARRAYSIZE(object) sizeof(object)/sizeof(object[0])
#define VK_CHECK(call) { VkResult result = call; assert(result == VK_SUCCESS); }

#define API_DUMP 1

#define WIDTH 1280
#define HEIGHT 720

static VkBool32 VKAPI_CALL debugReportCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData) {
	const char* type = (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) ? "ERROR" : (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) ? "WARNING" : "INFO";
	printf("%s: %s", type, pMessage);

	return VK_FALSE;
}

int main(int argc, char* argv[]) {

	if (!glfwInit()) {
		printf("Failed to initialize GLFW!");
		return -1;
	}

	GLFWwindow* window;
	window = glfwCreateWindow(WIDTH, HEIGHT, "VulkanRT", NULL, NULL);
	if (!window) {
		printf("Failed to create GLFW window!");
		glfwTerminate();
		return -1;
	}

	if (volkInitialize() != VK_SUCCESS) {
		printf("Failed to initialize Volk!");
		return -1;
	}

	assert(volkGetInstanceVersion() >= VK_API_VERSION_1_2);

	VkApplicationInfo applicationInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	applicationInfo.apiVersion = VK_API_VERSION_1_2;
	applicationInfo.applicationVersion = 0;
	applicationInfo.pApplicationName = NULL;
	applicationInfo.pEngineName = NULL;
	applicationInfo.engineVersion = 0;

	VkInstanceCreateInfo instanceCreateInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	instanceCreateInfo.pApplicationInfo = &applicationInfo;
	instanceCreateInfo.enabledLayerCount = 0;
	instanceCreateInfo.enabledExtensionCount = 0;

#ifdef _DEBUG
	const char* layers[] = {
#if API_DUMP
		"VK_LAYER_LUNARG_api_dump",
#endif
		"VK_LAYER_KHRONOS_validation"
	};

	instanceCreateInfo.enabledLayerCount = ARRAYSIZE(layers);
	instanceCreateInfo.ppEnabledLayerNames = layers;
#endif

	const char* extensions[] = {
		VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef _DEBUG
		VK_EXT_DEBUG_REPORT_EXTENSION_NAME
#endif
	};

	instanceCreateInfo.enabledExtensionCount = ARRAYSIZE(extensions);
	instanceCreateInfo.ppEnabledExtensionNames = extensions;

	VkInstance instance = 0;

	VK_CHECK(vkCreateInstance(&instanceCreateInfo, 0, &instance));

	volkLoadInstance(instance);

#ifdef _DEBUG
	VkDebugReportCallbackCreateInfoEXT debugReportCallbackCreateInfo = { VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT };
	debugReportCallbackCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT & VK_DEBUG_REPORT_WARNING_BIT_EXT & VK_DEBUG_REPORT_INFORMATION_BIT_EXT & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
	debugReportCallbackCreateInfo.pfnCallback = debugReportCallback;

	VkDebugReportCallbackEXT debugReportCallback = 0;
	VK_CHECK(vkCreateDebugReportCallbackEXT(instance, &debugReportCallbackCreateInfo, 0, &debugReportCallback));
#endif

	VkSurfaceKHR surface = 0;

	if (!glfwCreateWindowSurface(instance, window, 0, &surface)) {
		printf("Failed to create window surface!");
		return -1;
	}
	
	uint32_t physicalDeviceCount = 0;
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, 0));

	std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data()));

	VkPhysicalDevice physicalDevice = 0;
	VkPhysicalDeviceProperties physicalDeviceProperties;
	uint32_t graphicsQueueIndex = -1;
	bool deviceFound = false;
	for (size_t i = 0; i < physicalDeviceCount; ++i) {
		physicalDevice = physicalDevices[i];

		vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);

		printf("GPU%d: %s\n", i, physicalDeviceProperties.deviceName);

		if (physicalDeviceProperties.apiVersion < VK_API_VERSION_1_2) {
			continue;
		}

		if (physicalDeviceProperties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
			continue;
		}

		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, 0);

		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

		for (size_t j = 0; j < queueFamilyCount; ++j) {
			if (queueFamilies[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				graphicsQueueIndex = j;
				break;
			}
		}

		if (graphicsQueueIndex == -1) {
			continue;
		}

		deviceFound = true;
		break;
	}

	if (!deviceFound) {
		printf("No suitable GPU found!");
		return -1;
	}
	else {
		printf("Selected GPU: %s\n", physicalDeviceProperties.deviceName);
	}

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
	}

#ifdef _DEBUG
	vkDestroyDebugReportCallbackEXT(instance, debugReportCallback, 0);
#endif

	vkDestroyInstance(instance, 0);

	glfwTerminate();

	return 0;
}