#pragma once

#include <glm/glm.hpp>

namespace plaster::scene3d {

class Scene;

// Stateless controller that applies player input to the primary camera
// entity (the one carrying CameraComponent + PrimaryCameraComponent +
// FirstPersonCameraComponent + TransformComponent).
//
// The controller is split off from the camera entity so the entity stays
// pure data - all behaviour and platform/input knowledge sits here.
//
// Inputs:
//   mouseDelta - pixel cursor delta since last frame. Pass {0,0} when
//                the cursor is released so the camera doesn't move.
//   dt         - seconds since last frame.
class FirstPersonCameraController {
public:
    // Looks up the primary FPS camera in the scene and updates its
    // yaw / pitch (from mouseDelta) and translation (from WASD + space /
    // left-ctrl). Does nothing if no eligible camera exists.
    static void Update(Scene& scene, glm::vec2 mouseDelta, float dt);
};

} // namespace plaster::scene3d
