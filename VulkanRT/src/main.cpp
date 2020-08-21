#pragma warning( disable : 26812) // Prefer 'enum class' over 'enum'.

#define VK_ENABLE_BETA_EXTENSIONS
#define VOLK_IMPLEMENTATION
#define GLFW_INCLUDE_VULKAN

#include "volk.h"
#include "glfw3.h"

#include <assert.h>
#include <cstdio>
#include <stdexcept>
#include <vector>

#define ARRAYSIZE(object) sizeof(object)/sizeof(object[0])
#define VK_CHECK(call) { VkResult result = call; assert(result == VK_SUCCESS); }

#define API_DUMP 0
#define VERBOSE 0
#define INFO 0

#define WIDTH 1280
#define HEIGHT 720

#define MAX_FRAMES_IN_FLIGHT 2

static VkBool32 VKAPI_CALL debugUtilsCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
	const char* severity =
		(messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "ERROR" :
		(messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) ? "WARNING" :
		(messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) ? "INFO" :
		"VERBOSE";

	const char* type =
		(messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) ? "GENERAL" :
		(messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) ? "VALIDATION" :
		"PERFORMANCE";

	printf("%s-%s: %s\n\n", severity, type, pCallbackData->pMessage);

	return VK_FALSE;
}

VkInstance createInstance() {
	VkApplicationInfo applicationInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	applicationInfo.apiVersion = VK_API_VERSION_1_2;
	applicationInfo.applicationVersion = 0;
	applicationInfo.pApplicationName = NULL;
	applicationInfo.pEngineName = NULL;
	applicationInfo.engineVersion = 0;

	VkInstanceCreateInfo createInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	createInfo.pApplicationInfo = &applicationInfo;
	createInfo.enabledLayerCount = 0;
	createInfo.enabledExtensionCount = 0;

#ifdef _DEBUG
	const char* layers[] = {
#if API_DUMP
		"VK_LAYER_LUNARG_api_dump",
#endif
		"VK_LAYER_KHRONOS_validation"
	};

	createInfo.enabledLayerCount = ARRAYSIZE(layers);
	createInfo.ppEnabledLayerNames = layers;
#endif

	uint32_t glfwExtensionCount;
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

#ifdef _DEBUG
	extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

	createInfo.enabledExtensionCount = extensions.size();
	createInfo.ppEnabledExtensionNames = extensions.data();

	VkInstance instance = 0;

	VK_CHECK(vkCreateInstance(&createInfo, nullptr, &instance));

	return instance;
}

int getGraphicsQueueFamilyIndex(const VkPhysicalDevice physicalDevice) {
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

bool pickPhysicalDevice(const VkInstance instance, const VkSurfaceKHR surface, VkPhysicalDevice& physicalDevice) {
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

		graphicsQueueIndex = getGraphicsQueueFamilyIndex(physicalDevice);

		if (graphicsQueueIndex == -1) {
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

bool surfaceFormatSupported(const VkPhysicalDevice physicalDevice, const VkSurfaceKHR surface, const VkSurfaceFormatKHR& desiredSurfaceFormat) {
	uint32_t surfaceFormatsCount;
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatsCount, 0));

	std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatsCount);
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatsCount, surfaceFormats.data()));

	for (VkSurfaceFormatKHR surfaceFormat : surfaceFormats) {
		if (surfaceFormat.format == desiredSurfaceFormat.format && surfaceFormat.colorSpace == desiredSurfaceFormat.colorSpace) {
			return true;
		}
	}

	return false;
}

VkPresentModeKHR getPresentMode(const VkPhysicalDevice physicalDevice, const VkSurfaceKHR surface) {
	uint32_t presentModesCount;
	VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModesCount, 0));

	std::vector<VkPresentModeKHR> presentModes(presentModesCount);
	VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModesCount, presentModes.data()));

	bool immediatePresentModeSupported = false;
	for (VkPresentModeKHR presentMode : presentModes) {
		immediatePresentModeSupported = immediatePresentModeSupported || presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR;
	}

	return immediatePresentModeSupported ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR;
}

uint32_t getSwapchainImageCount(const VkPhysicalDevice physicalDevice, const VkSurfaceKHR surface) {
	VkSurfaceCapabilitiesKHR surfaceCapabilities;
	VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities));
	if (surfaceCapabilities.maxImageCount < 2 && surfaceCapabilities.maxImageCount != 0) {
		throw std::runtime_error("Couldn't get enough swapchain images!");
	}

	uint32_t imageCount = surfaceCapabilities.minImageCount + 1;
	if (surfaceCapabilities.maxImageCount > 0 && imageCount > surfaceCapabilities.maxImageCount) {
		imageCount = surfaceCapabilities.minImageCount;
	}

	return imageCount;
}

VkSwapchainKHR createSwapchain(const VkDevice device, const VkSurfaceKHR surface, const VkSurfaceFormatKHR surfaceFormat, const VkPresentModeKHR presentMode, const uint32_t imageCount, const uint32_t graphicsQueueFamilyIndex) {
	VkSwapchainCreateInfoKHR swapchainCreateInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	swapchainCreateInfo.surface = surface;
	swapchainCreateInfo.minImageCount = imageCount;
	swapchainCreateInfo.queueFamilyIndexCount = 1;
	swapchainCreateInfo.pQueueFamilyIndices = &graphicsQueueFamilyIndex;
	swapchainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	swapchainCreateInfo.presentMode = presentMode;
	swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchainCreateInfo.imageFormat = surfaceFormat.format;
	swapchainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
	swapchainCreateInfo.imageExtent = VkExtent2D{ WIDTH, HEIGHT };
	swapchainCreateInfo.imageArrayLayers = 1;
	swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

	VkSwapchainKHR swapchain = 0;
	VK_CHECK(vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain));

	return swapchain;
}

std::vector<VkImageView> getSwapchainImageViews(const VkDevice device, const VkSwapchainKHR swapchain, const VkFormat surfaceFormat) {
	uint32_t swapchainImageCount;
	VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, 0));

	std::vector<VkImage> swapchainImages(swapchainImageCount);
	VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, swapchainImages.data()));

	std::vector<VkImageView> swapchainImageViews(swapchainImageCount);

	VkImageViewCreateInfo imageViewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	imageViewCreateInfo.format = surfaceFormat;
	imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
	imageViewCreateInfo.subresourceRange.levelCount = 1;
	imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
	imageViewCreateInfo.subresourceRange.layerCount = 1;

	for (size_t i = 0; i < swapchainImageCount; ++i) {
		imageViewCreateInfo.image = swapchainImages[i];
		VK_CHECK(vkCreateImageView(device, &imageViewCreateInfo, nullptr, &swapchainImageViews[i]));
	}

	return swapchainImageViews;
}

VkRenderPass createRenderPass(const VkDevice device, const VkFormat surfaceFormat) {
	VkAttachmentDescription attachments[2] = {};
	attachments[0].format = surfaceFormat;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	attachments[1].format = VK_FORMAT_D24_UNORM_S8_UINT;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;
	//TODO enable depth attachment later subpass.pDepthStencilAttachment = &depthRef;

	VkSubpassDependency subpassDependency = {};
	subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	subpassDependency.dstSubpass = 0;
	subpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependency.srcAccessMask = 0;
	subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo renderPassCreateInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	renderPassCreateInfo.attachmentCount = 1; //TODO: enable depth attachment later: ARRAYSIZE(attachments);
	renderPassCreateInfo.pAttachments = attachments;
	renderPassCreateInfo.subpassCount = 1;
	renderPassCreateInfo.pSubpasses = &subpass;
	renderPassCreateInfo.dependencyCount = 1;
	renderPassCreateInfo.pDependencies = &subpassDependency;

	VkRenderPass renderPass = 0;
	VK_CHECK(vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &renderPass));

	return renderPass;
}

VkShaderModule loadShader(const VkDevice device, const char* pathToSource) {
	FILE* source;
	fopen_s(&source, pathToSource, "rb");
	assert(source);

	fseek(source, 0, SEEK_END);
	size_t length = ftell(source);
	assert(length > 0);
	fseek(source, 0, SEEK_SET);

	char* buffer = new char[length];
	size_t readChars = fread(buffer, 1, length, source);
	assert(length == readChars);

	VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	createInfo.codeSize = length;
	createInfo.pCode = reinterpret_cast<uint32_t*>(buffer);

	VkShaderModule shaderModule = 0;
	VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule));

	delete[] buffer;

	return shaderModule;
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
	VkDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
	debugUtilsMessengerCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
#if INFO
	debugUtilsMessengerCreateInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
#endif
#if VERBOSE
	debugUtilsMessengerCreateInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
#endif
	debugUtilsMessengerCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	debugUtilsMessengerCreateInfo.pfnUserCallback = debugUtilsCallback;

	VkDebugUtilsMessengerEXT debugUtilsMessenger;
	VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance, &debugUtilsMessengerCreateInfo, nullptr, &debugUtilsMessenger));
#endif

	VkSurfaceKHR surface = 0;
	if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
		printf("Failed to create window surface!");
		return -1;
	}

	VkPhysicalDevice physicalDevice = 0;
	VkPhysicalDeviceProperties physicalDeviceProperties;

	if (!pickPhysicalDevice(instance, surface, physicalDevice)) {
		printf("No suitable GPU found!");
		return -1;
	}
	else {
		vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
		printf("Selected GPU: %s\n", physicalDeviceProperties.deviceName);
	}

	uint32_t graphicsQueueFamilyIndex = getGraphicsQueueFamilyIndex(physicalDevice);

	const float queuePriorities = 1.0f;
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
	VK_CHECK(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device));

	volkLoadDevice(device);

	VkSurfaceFormatKHR surfaceFormat;
	surfaceFormat.format = VK_FORMAT_B8G8R8A8_UNORM;
	surfaceFormat.colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;

	if (!surfaceFormatSupported(physicalDevice, surface, surfaceFormat)) {
		printf("Requested surface format not supported!");
		return -1;
	}

	VkPresentModeKHR presentMode = getPresentMode(physicalDevice, surface);
	uint32_t requestedSwapchainImageCount = getSwapchainImageCount(physicalDevice, surface);
	VkSwapchainKHR swapchain = createSwapchain(device, surface, surfaceFormat, presentMode, requestedSwapchainImageCount, graphicsQueueFamilyIndex);

	std::vector<VkImageView> swapchainImageViews = getSwapchainImageViews(device, swapchain, surfaceFormat.format);
	uint32_t swapchainImageCount = swapchainImageViews.size();

	VkRenderPass renderPass = createRenderPass(device, surfaceFormat.format);

	VkFramebufferCreateInfo framebufferCreateInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	framebufferCreateInfo.renderPass = renderPass;
	framebufferCreateInfo.attachmentCount = 1;
	framebufferCreateInfo.width = WIDTH;
	framebufferCreateInfo.height = HEIGHT;
	framebufferCreateInfo.layers = 1;

	std::vector<VkFramebuffer> framebuffers(swapchainImageCount);
	for (size_t i = 0; i < swapchainImageCount; ++i) {
		framebufferCreateInfo.pAttachments = &swapchainImageViews[i];
		VK_CHECK(vkCreateFramebuffer(device, &framebufferCreateInfo, nullptr, &framebuffers[i]));
	}

	VkShaderModule vertexShader = loadShader(device, "src/shaders/spirv/vertexShader.spv");
	VkShaderModule fragmentShader = loadShader(device, "src/shaders/spirv/fragmentShader.spv");

	VkPipelineCacheCreateInfo pipelineCacheCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
	pipelineCacheCreateInfo.initialDataSize = 0;

	VkPipelineCache pipelineCache = 0;
	VK_CHECK(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, nullptr, &pipelineCache));

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };

	VkPipelineLayout pipelineLayout = 0;
	VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

	VkGraphicsPipelineCreateInfo pipelineCreateInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };

	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

	VkPipelineShaderStageCreateInfo vertexStageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	vertexStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertexStageInfo.module = vertexShader;
	vertexStageInfo.pName = "main";
	shaderStages.push_back(vertexStageInfo);

	VkPipelineShaderStageCreateInfo fragmentStageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	fragmentStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragmentStageInfo.module = fragmentShader;
	fragmentStageInfo.pName = "main";
	shaderStages.push_back(fragmentStageInfo);

	pipelineCreateInfo.stageCount = shaderStages.size();
	pipelineCreateInfo.pStages = shaderStages.data();

	VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	pipelineCreateInfo.pVertexInputState = &vertexInputStateCreateInfo;

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	inputAssemblyStateCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyStateCreateInfo;

	VkPipelineViewportStateCreateInfo viewportStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	viewportStateCreateInfo.viewportCount = 1;
	viewportStateCreateInfo.scissorCount = 1;
	pipelineCreateInfo.pViewportState = &viewportStateCreateInfo;

	VkPipelineRasterizationStateCreateInfo rasterizationStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rasterizationStateCreateInfo.lineWidth = 1.0f;
	rasterizationStateCreateInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizationStateCreateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	pipelineCreateInfo.pRasterizationState = &rasterizationStateCreateInfo;

	VkPipelineMultisampleStateCreateInfo multisampleStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	multisampleStateCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	pipelineCreateInfo.pMultisampleState = &multisampleStateCreateInfo;

	VkPipelineDepthStencilStateCreateInfo depthStencilStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	depthStencilStateCreateInfo.depthTestEnable = true;
	depthStencilStateCreateInfo.depthWriteEnable = true;
	depthStencilStateCreateInfo.depthCompareOp = VK_COMPARE_OP_GREATER;
	pipelineCreateInfo.pDepthStencilState = &depthStencilStateCreateInfo;

	VkPipelineColorBlendAttachmentState colorBlendAttachmentState = {};
	colorBlendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo colorBlendStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	colorBlendStateCreateInfo.attachmentCount = 1;
	colorBlendStateCreateInfo.pAttachments = &colorBlendAttachmentState;
	pipelineCreateInfo.pColorBlendState = &colorBlendStateCreateInfo;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicStateCreateInfo.dynamicStateCount = ARRAYSIZE(dynamicStates);
	dynamicStateCreateInfo.pDynamicStates = dynamicStates;
	pipelineCreateInfo.pDynamicState = &dynamicStateCreateInfo;

	pipelineCreateInfo.layout = pipelineLayout;
	pipelineCreateInfo.renderPass = renderPass;

	VkPipeline pipeline = 0;
	VK_CHECK(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));

	vkDestroyShaderModule(device, fragmentShader, nullptr);
	vkDestroyShaderModule(device, vertexShader, nullptr);

	VkQueue queue;
	vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &queue);

	VkCommandPoolCreateInfo commandPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	commandPoolCreateInfo.queueFamilyIndex = graphicsQueueFamilyIndex;

	VkCommandPool commandPool = 0;
	VK_CHECK(vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &commandPool));

	std::vector<VkCommandBuffer> commandBuffers(swapchainImageCount);

	VkCommandBufferAllocateInfo commandBufferAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	commandBufferAllocateInfo.commandPool = commandPool;
	commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandBufferAllocateInfo.commandBufferCount = commandBuffers.size();

	VK_CHECK(vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, commandBuffers.data()));

	VkCommandBufferBeginInfo commandBufferBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

	VkRenderPassBeginInfo renderPassBeginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	renderPassBeginInfo.renderPass = renderPass;
	renderPassBeginInfo.renderArea.offset = { 0, 0 };
	renderPassBeginInfo.renderArea.extent = VkExtent2D{ WIDTH, HEIGHT };

	VkClearValue clearColor = { 0.0f, 0.0f, 0.1f, 1.0f };
	renderPassBeginInfo.clearValueCount = 1;
	renderPassBeginInfo.pClearValues = &clearColor;

	VkViewport viewport = {};
	viewport.width = WIDTH;
	viewport.height = HEIGHT;
	viewport.x = 0;
	viewport.y = 0;
	viewport.minDepth = 1.0f;
	viewport.maxDepth = 0.0f;

	VkRect2D scissor = {};
	scissor.offset = { 0, 0 };
	scissor.extent = { WIDTH, HEIGHT };

	for (size_t i = 0; i < commandBuffers.size(); ++i) {
		VK_CHECK(vkBeginCommandBuffer(commandBuffers[i], &commandBufferBeginInfo));


		vkCmdSetViewport(commandBuffers[i], 0, 1, &viewport);
		vkCmdSetScissor(commandBuffers[i], 0, 1, &scissor);

		renderPassBeginInfo.framebuffer = framebuffers[i];
		vkCmdBeginRenderPass(commandBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);

		vkCmdEndRenderPass(commandBuffers[i]);

		VK_CHECK(vkEndCommandBuffer(commandBuffers[i]));
	}

	std::vector<VkSemaphore> imageAvailableSemaphores(MAX_FRAMES_IN_FLIGHT);
	std::vector<VkSemaphore> renderFinishedSemaphores(MAX_FRAMES_IN_FLIGHT);
	std::vector<VkFence> inFlightFences(MAX_FRAMES_IN_FLIGHT);
	std::vector<VkFence> imagesInFlight(swapchainImageCount, VK_NULL_HANDLE);

	VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &imageAvailableSemaphores[i]));
		VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &renderFinishedSemaphores[i]));
		VK_CHECK(vkCreateFence(device, &fenceCreateInfo, nullptr, &inFlightFences[i]));
	}

	uint32_t currentFrame = 0;

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

		uint32_t imageIndex;
		vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

		if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
			vkWaitForFences(device, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
		}
		imagesInFlight[imageIndex] = inFlightFences[currentFrame];

		VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &imageAvailableSemaphores[currentFrame];
		submitInfo.pWaitDstStageMask = &waitStage;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffers[imageIndex];
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &renderFinishedSemaphores[currentFrame];

		vkResetFences(device, 1, &inFlightFences[currentFrame]);

		VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, inFlightFences[currentFrame]));

		VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &renderFinishedSemaphores[currentFrame];
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapchain;
		presentInfo.pImageIndices = &imageIndex;

		vkQueuePresentKHR(queue, &presentInfo);

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

	vkDestroyCommandPool(device, commandPool, nullptr);
	vkDestroyPipeline(device, pipeline, nullptr);
	vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
	vkDestroyPipelineCache(device, pipelineCache, nullptr);

	for (VkFramebuffer& framebuffer : framebuffers) {
		vkDestroyFramebuffer(device, framebuffer, nullptr);
	}

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