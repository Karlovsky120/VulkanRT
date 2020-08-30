#pragma once

#pragma warning(disable : 26812) // Prefer 'enum class' over 'enum'.

#ifndef _DEBUG
#pragma warning(disable : 4189) // *: local variable is initialized but not referenced
#pragma warning(disable : 4464) // Relative include path contains '..'
#pragma warning(disable : 4514) // *: unreferenced inline function has been removed
#pragma warning(disable : 4710) // * function not inlined
#pragma warning(disable : 4711) // Function * selected for automatic line expansion
#endif

#define ARRAYSIZE(object) sizeof(object) / sizeof(object[0])

#define VK_CHECK(call)                                                                                                                                         \
    {                                                                                                                                                          \
        VkResult result = call;                                                                                                                                \
        assert(result == VK_SUCCESS);                                                                                                                          \
    }