#ifdef CPP_SHADER_STRUCTURE
#pragma warning(push, 0)
#include "glm/fwd.hpp"
#include "glm/mat4x4.hpp"
#pragma warning(pop)

#define mat4 glm::mat4
#endif

struct PushData {
    // Camera transformation
    mat4 cameraTransformation;

    // Perspective parameters for reverse z
    float oneOverTanOfHalfFov;
    float oneOverAspectRatio;
    float near;
};

#ifdef CPP_SHADER_STRUCTURE
#undef mat4
#endif
