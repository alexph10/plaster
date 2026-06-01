#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace plaster::scene3d {

// FirstPersonCameraComponent stores the player-controlled state of a
// classic FPS camera: heading (yaw + pitch) and the tuning knobs the
// controller reads each frame.
//
// Design rationale:
//   - Yaw / pitch live here (not in TransformComponent.Rotation) so the
//     camera system can interpret them in a well-defined order
//     (yaw around world-Y, then pitch around the local right axis)
//     without depending on GLM's quaternion-from-Euler convention.
//   - TransformComponent.Translation is still the camera position; only
//     orientation is duplicated here.
//   - The controller logic that reads input lives in
//     FirstPersonCameraController; this struct is pure data, ECS-style.
struct FirstPersonCameraComponent {
    // Heading. Yaw=0, pitch=0 looks down world -Z.
    float Yaw   = 0.0f;   // radians, around world up (+Y)
    float Pitch = 0.0f;   // radians, around camera right (+X)

    // Tuning.
    float MoveSpeed         = 4.0f;   // world units per second (walk)
    float SprintMultiplier  = 2.5f;   // hold sprint to multiply speed
    float MouseSensitivity  = 0.0025f; // radians per pixel of cursor delta

    // Pitch clamp keeps the camera from flipping over on its back.
    bool  ConstrainPitch = true;
    float MinPitch = -1.5533430f; // glm::radians(-89.0f) precomputed
    float MaxPitch =  1.5533430f; // glm::radians( 89.0f) precomputed
};

} // namespace plaster::scene3d
