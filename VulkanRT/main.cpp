#pragma warning( disable : 26812) // Prefer 'enum class' over 'enum'.

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

VkInstance createInstance() {
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

	uint32_t glfwExtensionCount;
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

#ifdef _DEBUG
	extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
#endif

	instanceCreateInfo.enabledExtensionCount = extensions.size();
	instanceCreateInfo.ppEnabledExtensionNames = extensions.data();

	VkInstance instance = 0;

	VK_CHECK(vkCreateInstance(&instanceCreateInfo, 0, &instance));

	return instance;
}

int getGraphicsQueueFamilyIndex(VkPhysicalDevice physicalDevice) {
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, 0);

	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

	uint32_t graphicsQueueFamilyIndex = -1;
	for (size_t j = 0; j < queueFamilyCount; ++j) {
		if (queueFamilies[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			graphicsQueueFamilyIndex = j;
			break;
		}
	}

	return graphicsQueueFamilyIndex;
}

bool pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface, VkPhysicalDevice &physicalDevice) {
	uint32_t physicalDeviceCount = 0;
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, 0));

	std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data()));

	uint32_t graphicsQueueIndex = -1;
	bool deviceFound = false;
	for (size_t i = 0; i < physicalDeviceCount; ++i) {
		physicalDevice = physicalDevices[i];

		VkPhysicalDeviceProperties physicalDeviceProperties;
		vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);

		printf("GPU%d: %s\n", i, physicalDeviceProperties.deviceName);

		if (physicalDeviceProperties.apiVersion < VK_API_VERSION_1_2) {
			continue;
		}

		if (physicalDeviceProperties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
			continue;
		}

		if (getGraphicsQueueFamilyIndex(physicalDevice) == -1) {
			continue;
		}

		VkBool32 presentSupported;
		VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, graphicsQueueIndex, surface, &presentSupported));

		if (presentSupported == VK_FALSE) {
			continue;
		}

		deviceFound = true;
		break;
	}

	return deviceFound;
}

int main(int argc, char* argv[]) {

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

	if (volkInitialize() != VK_SUCCESS) {
		printf("Failed to initialize Volk!");
		return -1;
	}

	assert(volkGetInstanceVersion() >= VK_API_VERSION_1_2);

	VkInstance instance = createInstance();

	volkLoadInstance(instance);

#ifdef _DEBUG
	VkDebugReportCallbackCreateInfoEXT debugReportCallbackCreateInfo = { VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT };
	debugReportCallbackCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT & VK_DEBUG_REPORT_WARNING_BIT_EXT & VK_DEBUG_REPORT_INFORMATION_BIT_EXT & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
	debugReportCallbackCreateInfo.pfnCallback = debugReportCallback;

	VkDebugReportCallbackEXT debugReportCallback = 0;
	VK_CHECK(vkCreateDebugReportCallbackEXT(instance, &debugReportCallbackCreateInfo, 0, &debugReportCallback));
#endif

	VkSurfaceKHR surface = 0;
	if (glfwCreateWindowSurface(instance, window, 0, &surface) != VK_SUCCESS) {
		printf("Failed to create window surface!");
		return -1;
	}
	
	VkPhysicalDevice physicalDevice = 0;
	VkPhysicalDeviceProperties physicalDeviceProperties;

	if (pickPhysicalDevice(instance, surface, physicalDevice)) {
		printf("No suitable GPU found!");
		return -1;
	}
	else {
		vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
		printf("Selected GPU: %s\n", physicalDeviceProperties.deviceName);
	}

	uint32_t graphicsQueueFamilyIndex = getGraphicsQueueFamilyIndex(physicalDevice);

	const float queuePriorities =  1.0f;
	VkDeviceQueueCreateInfo deviceQueueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	deviceQueueCreateInfo.queueCount = 1;
	deviceQueueCreateInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
	deviceQueueCreateInfo.pQueuePriorities = &queuePriorities;

	const char* deviceExtensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};

	VkDeviceCreateInfo deviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	deviceCreateInfo.queueCreateInfoCount = 1;
	deviceCreateInfo.pQueueCreateInfos = &deviceQueueCreateInfo;
	deviceCreateInfo.enabledExtensionCount = 1;
	deviceCreateInfo.ppEnabledExtensionNames = &deviceExtensions;

	VkDevice device = 0;
	VK_CHECK(vkCreateDevice(physicalDevice, &deviceCreateInfo, 0, &device));

	volkLoadDevice(device);

	VkSurfaceCapabilitiesKHR surfaceCapabilities;
	VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities));

	if (surfaceCapabilities.maxImageCount < 2 && surfaceCapabilities.maxImageCount != 0) {
		printf("Couldn't get enough swapchain images!");
		return -1;
	}

	uint32_t presentModesCount;
	VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModesCount, 0));

	std::vector<VkPresentModeKHR> presentModes(presentModesCount);
	VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModesCount, presentModes.data()));

	bool immediatePresentModeSupported = false;
	for (VkPresentModeKHR presentMode : presentModes) {
		immediatePresentModeSupported = immediatePresentModeSupported || presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR;
	}

	uint32_t surfaceFormatsCount;
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatsCount, 0));

	std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatsCount);
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatsCount, surfaceFormats.data()));

	bool formatFound = false;
	VkFormat desiredFormat = VK_FORMAT_B8G8R8A8_UNORM;
	VkColorSpaceKHR desiredColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
	for (VkSurfaceFormatKHR surfaceFormat : surfaceFormats) {
		if (surfaceFormat.format == desiredFormat && surfaceFormat.colorSpace == desiredColorSpace) {
			formatFound = true;
			break;
		}
	}

	if (!formatFound) {
		printf("No suitable surface format found!");
		return -1;
	}

	VkSwapchainCreateInfoKHR swapchainCreateInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	swapchainCreateInfo.surface = surface;
	swapchainCreateInfo.minImageCount = 2;
	swapchainCreateInfo.queueFamilyIndexCount = 1;
	swapchainCreateInfo.pQueueFamilyIndices = &graphicsQueueFamilyIndex;
	swapchainCreateInfo.presentMode = immediatePresentModeSupported ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR;
	swapchainCreateInfo.imageFormat = desiredFormat;
	swapchainCreateInfo.imageColorSpace = desiredColorSpace;
	swapchainCreateInfo.imageExtent = VkExtent2D{ WIDTH, HEIGHT };
	swapchainCreateInfo.imageArrayLayers = 1;
	swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

	VkSwapchainKHR swapchain = 0;
	VK_CHECK(vkCreateSwapchainKHR(device, &swapchainCreateInfo, 0, &swapchain));

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
	}

	vkDestroySwapchainKHR(device, swapchain, 0);
	vkDestroyDevice(device, 0);
	vkDestroySurfaceKHR(instance, surface, 0);

#ifdef _DEBUG
	vkDestroyDebugReportCallbackEXT(instance, debugReportCallback, 0);
#endif

	vkDestroyInstance(instance, 0);

	glfwTerminate();

	return 0;
}