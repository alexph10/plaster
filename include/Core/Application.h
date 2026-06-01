#pragma once

#include <entt/entity/entity.hpp>

#include <glm/glm.hpp>

#include <memory>

namespace plaster {

class Window;
class VulkanContext;
class Renderer;
class GridMap;

namespace scene3d {
class Scene;
}

// Application owns the engine's top-level lifetime: the window, the Vulkan
// context, the renderer, the scene, and the per-frame tick that ties them
// together. main.cpp constructs one Application and calls run().
class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run();

private:
    void setupScene();
    void handleCaptureToggles();
    // Returns mouse delta in screen pixels since the previous frame, or
    // {0,0} when the cursor is not currently captured.
    struct MouseDelta { float x; float y; };
    MouseDelta sampleMouseDelta();

    // Clamps the player's TransformComponent.Translation against the
    // GridMap walls and floor/ceiling, using axis-separated slide
    // starting from the given pre-move position.
    void resolvePlayerCollision(const glm::vec3& preMovePos);

    std::unique_ptr<Window>        m_window;
    std::unique_ptr<VulkanContext> m_vulkanContext;
    std::unique_ptr<Renderer>      m_renderer;
    std::unique_ptr<scene3d::Scene> m_scene;
    std::unique_ptr<GridMap>       m_map;

    entt::entity m_playerEntity{entt::null};

    // For mouse delta computation. Cleared whenever the capture state
    // changes, so re-capturing the cursor doesn't yank the camera.
    double m_lastCursorX = 0.0;
    double m_lastCursorY = 0.0;
    bool   m_hasLastCursor = false;
    bool   m_lastCursorCapturedState = false;

    double m_lastFrameTime = 0.0;
};

} // namespace plaster
