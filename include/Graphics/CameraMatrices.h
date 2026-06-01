#pragma once

#include <glm/glm.hpp>

namespace plaster {

// Plain bundle of the per-frame camera matrices the Renderer needs.
// Constructed by Application (which builds it from CameraSystem) and
// consumed by Renderer. Keeps the Renderer ignorant of how the camera is
// driven (orbit, FPS, cinematic, follow, ...).
//
// The matrices are computed in a Vulkan-agnostic way - the renderer is
// responsible for applying the Y-flip that Vulkan clip space requires.
struct CameraMatrices {
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::vec3 position{0.0f};
};

} // namespace plaster
