#ifdef CPP_SHADER_STRUCTURE
#pragma warning(push, 0)
#include "glm/fwd.hpp"
#include "glm/mat4x4.hpp"
#include "glm/vec3.hpp"
#pragma warning(pop)

#define mat4 glm::mat4
#define vec3 glm::vec3
#endif

struct PushData {
    // Camera rotation and position
    mat4 rotation;
    vec3 position;

    // Perspective parameters for reverse z
    float oneOverTanOfHalfFov;
    float oneOverAspectRatio;
    float near;
};

#ifdef CPP_SHADER_STRUCTURE
#undef mat4
#undef vec3
#endif
