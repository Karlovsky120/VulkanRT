#define VOLK_IMPLEMENTATION

#include "volk.h"

#include <assert.h>

#define ARRAYSIZE(object) sizeof(object)/sizeof(object[0])

#define VK_CHECK(call) assert(call == VK_SUCCESS)

int main(int argc, char* argv[]) {
	if (volkInitialize() != VK_SUCCESS) {
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

	return 0;
}