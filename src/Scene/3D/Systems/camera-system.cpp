#include "Scene/3D/Systems/camera-system.hpp"

#include "Scene/3D/Components/camera.hpp"
#include "Scene/3D/Components/first-person-camera.hpp"
#include "Scene/3D/Components/transform.hpp"
#include "Scene/3D/Core/entity.hpp"
#include "Scene/3D/Core/scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace plaster::scene3d {

CameraData CameraSystem::GetPrimaryCameraData(Scene& scene) {
    Entity cam = GetPrimaryCamera(scene);
    if (!cam.IsValid()) {
        return CameraData{};
    }
    return BuildCameraData(scene, cam);
}

Entity CameraSystem::GetPrimaryCamera(Scene& scene) {
    auto& registry = scene.GetRegistry();
    auto view = registry.view<CameraComponent, PrimaryCameraComponent>();
    if (view.begin() != view.end()) {
        return Entity{*view.begin(), &scene};
    }
    return Entity{};
}

CameraData CameraSystem::BuildCameraData(Scene& scene, Entity camera) {
    CameraData data{};
    data.CameraEntity = camera;

    if (!camera.IsValid()) {
        return data;
    }

    data.View = BuildViewMatrix(scene, camera);
    data.Projection = BuildProjectionMatrix(scene, camera);
    data.ViewProjection = data.Projection * data.View;

    if (camera.HasComponent<TransformComponent>()) {
        const auto& xform = camera.GetComponent<TransformComponent>();
        data.Position = xform.Translation;

        if (camera.HasComponent<FirstPersonCameraComponent>()) {
            // Same convention used by the FPS controller: yaw around +Y,
            // pitch around +X, identity gaze along -Z.
            const auto& fps = camera.GetComponent<FirstPersonCameraComponent>();
            data.Forward = glm::vec3(std::cos(fps.Pitch) * std::sin(fps.Yaw),
                                     std::sin(fps.Pitch),
                                     -std::cos(fps.Pitch) * std::cos(fps.Yaw));
            data.Up = glm::vec3(0.0f, 1.0f, 0.0f);
        } else {
            data.Forward = xform.GetForwardDirection();
            data.Up = xform.GetUpDirection();
        }
    }

    data.Valid = true;
    return data;
}

glm::mat4 CameraSystem::BuildViewMatrix(Scene& /*scene*/, Entity camera) {
    if (!camera.HasComponent<TransformComponent>()) {
        return glm::mat4(1.0f);
    }
    const auto& xform = camera.GetComponent<TransformComponent>();
    const glm::vec3 eye = xform.Translation;

    // Preferred path: an FPS-controlled camera owns its yaw / pitch, which
    // are interpreted in a well-defined order (yaw around world +Y, then
    // pitch around camera +X). This avoids depending on GLM's quaternion-
    // from-Euler convention.
    if (camera.HasComponent<FirstPersonCameraComponent>()) {
        const auto& fps = camera.GetComponent<FirstPersonCameraComponent>();
        const glm::vec3 forward(std::cos(fps.Pitch) * std::sin(fps.Yaw),
                                std::sin(fps.Pitch),
                                -std::cos(fps.Pitch) * std::cos(fps.Yaw));
        return glm::lookAt(eye, eye + forward, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    // Fallback: derive orientation from TransformComponent.
    const glm::vec3 target = eye + xform.GetForwardDirection();
    const glm::vec3 up = xform.GetUpDirection();
    return glm::lookAt(eye, target, up);
}

glm::mat4 CameraSystem::BuildProjectionMatrix(Scene& /*scene*/, Entity camera) {
    if (!camera.HasComponent<CameraComponent>()) {
        return glm::mat4(1.0f);
    }
    return camera.GetComponent<CameraComponent>().GetProjectionMatrix();
}

} // namespace plaster::scene3d
