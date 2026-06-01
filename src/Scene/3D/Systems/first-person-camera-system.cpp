#include "Scene/3D/Systems/first-person-camera-system.hpp"

#include "Core/Input.h"
#include "Scene/3D/Components/camera.hpp"
#include "Scene/3D/Components/first-person-camera.hpp"
#include "Scene/3D/Components/transform.hpp"
#include "Scene/3D/Core/scene.hpp"

#include <algorithm>
#include <cmath>

namespace plaster::scene3d {

namespace {

// Build a normalised forward vector from yaw + pitch using a right-handed
// coordinate system where:
//   yaw = 0,  pitch = 0  -> look down  -Z
//   yaw = +90 deg        -> look down  +X  (right)
//   pitch = +90 deg      -> look up    +Y
//
// Keeping this in one place means the controller and CameraSystem are
// guaranteed to agree on the convention.
glm::vec3 forwardFromYawPitch(float yaw, float pitch) {
    return glm::vec3(std::cos(pitch) * std::sin(yaw),
                     std::sin(pitch),
                     -std::cos(pitch) * std::cos(yaw));
}

} // namespace

void FirstPersonCameraController::Update(Scene& scene, glm::vec2 mouseDelta, float dt) {
    auto& registry = scene.GetRegistry();

    // Find the primary FPS-controlled camera. Iterate the smallest of the
    // viewed component sets (entt picks one as the driver).
    auto view = registry.view<TransformComponent, CameraComponent,
                              PrimaryCameraComponent, FirstPersonCameraComponent>();

    for (auto entity : view) {
        auto& xform = view.get<TransformComponent>(entity);
        auto& fps   = view.get<FirstPersonCameraComponent>(entity);

        // ---- Look ----
        // Mouse +X (right) -> yaw right (+yaw).
        // Mouse +Y (down)  -> pitch down (-pitch). Cursor coords increase
        //                     downward on screen, hence the negation.
        fps.Yaw   += mouseDelta.x * fps.MouseSensitivity;
        fps.Pitch -= mouseDelta.y * fps.MouseSensitivity;

        if (fps.ConstrainPitch) {
            fps.Pitch = std::clamp(fps.Pitch, fps.MinPitch, fps.MaxPitch);
        }

        // ---- Move ----
        // Flat forward / right: WASD walks on the XZ plane regardless of
        // pitch, matching Doom / Wolfenstein. Vertical motion is on
        // Space / LeftControl (noclip-flight, useful for early dev).
        const glm::vec3 forward = forwardFromYawPitch(fps.Yaw, fps.Pitch);
        glm::vec3 flatForward(forward.x, 0.0f, forward.z);
        const float flatLen = glm::length(flatForward);
        if (flatLen > 1e-5f) {
            flatForward /= flatLen;
        } else {
            // Looking straight up/down: pick an arbitrary fallback so WASD
            // still does something sensible.
            flatForward = glm::vec3(std::sin(fps.Yaw), 0.0f, -std::cos(fps.Yaw));
        }
        const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        const glm::vec3 right = glm::normalize(glm::cross(flatForward, worldUp));

        glm::vec3 wishDir(0.0f);
        if (Input::IsKeyDown(Key::W)) wishDir += flatForward;
        if (Input::IsKeyDown(Key::S)) wishDir -= flatForward;
        if (Input::IsKeyDown(Key::D)) wishDir += right;
        if (Input::IsKeyDown(Key::A)) wishDir -= right;
        if (Input::IsKeyDown(Key::Space))       wishDir += worldUp;
        if (Input::IsKeyDown(Key::LeftControl)) wishDir -= worldUp;

        const float l = glm::length(wishDir);
        if (l > 1e-5f) {
            wishDir /= l;
            float speed = fps.MoveSpeed;
            if (Input::IsKeyDown(Key::LeftShift)) {
                speed *= fps.SprintMultiplier;
            }
            xform.Translation += wishDir * speed * dt;
        }

        // Only the first matching camera is driven per frame.
        break;
    }
}

} // namespace plaster::scene3d
