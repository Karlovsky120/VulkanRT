#define VOLK_IMPLEMENTATION
#define GLFW_INCLUDE_VULKAN

#include "volk.h"
#include "glfw3.h"

#include <assert.h>
#include <cstdio>

#define ARRAYSIZE(object) sizeof(object)/sizeof(object[0])
#define VK_CHECK(call) assert(call == VK_SUCCESS)

#define API_DUMP

#define WIDTH 1280
#define HEIGHT 720

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

	VkInstanceCreateInfo instanceCreateInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	instanceCreateInfo.pApplicationInfo = &applicationInfo;

#ifdef _DEBUG
	const char* layers[] = {
#ifdef API_DUMP
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

	VkSurfaceKHR surface = 0;

	if (!glfwCreateWindowSurface(instance, window, 0, &surface)) {
		printf("Failed to create window surface!");
		return -1;
	}

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
	}

	glfwTerminate();

	return 0;
}