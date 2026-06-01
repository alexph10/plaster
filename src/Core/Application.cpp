#include "Core/Application.h"

#include "Core/Input.h"
#include "Core/Window.h"
#include "Graphics/CameraMatrices.h"
#include "Graphics/GridMap.h"
#include "Graphics/Renderer.h"
#include "Graphics/SpriteRenderer.h"
#include "Graphics/Vulkan/VulkanContext.h"
#include "Scene/3D/Components/components.hpp"
#include "Scene/3D/Core/scene.hpp"
#include "Scene/3D/Systems/camera-system.hpp"
#include "Scene/3D/Systems/first-person-camera-system.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <glm/gtc/constants.hpp>

#include <algorithm>

#include <imgui.h>

namespace plaster {

Application::Application() {
    m_window        = std::make_unique<Window>(1280, 720, "plaster");
    m_vulkanContext = std::make_unique<VulkanContext>(m_window.get());
    m_renderer      = std::make_unique<Renderer>(m_window.get(), m_vulkanContext.get());
    m_scene         = std::make_unique<scene3d::Scene>();

    // World layout.
    //
    //   # = solid wall column        . = empty (walkable) cell
    //   columns -> +X (east)         rows -> +Z (south)
    //
    // Designed so that as soon as the player spawns and presses W, they
    // get the Doom-style payoff: long sightline -> compressed doorway ->
    // open room. Two small pillar rooms flank a central corridor with a
    // single mid-pillar to break the silhouette.
    const std::vector<std::string> rows = {
        "################",
        "#..............#",
        "#..............#",
        "#..####..####..#",
        "#..#..#..#..#..#",
        "#..#..#..#..#..#",
        "#..####..####..#",
        "#..............#",
        "#..............#",
        "#......##......#",
        "#......##......#",
        "#..............#",
        "#..####..####..#",
        "#..#..#..#..#..#",
        "#..####..####..#",
        "################",
    };
    m_map = std::make_unique<GridMap>(GridMap::FromAscii(rows,
                                                         /*cellSize*/ 2.0f,
                                                         /*floorY*/   0.0f,
                                                         /*ceilingY*/ 3.0f));

    // ---- Per-cell heights ----
    //
    // Three pieces of vertical interest to show off the system:
    //
    // 1. A short staircase climbing south down the main corridor
    //    (cells 1,5..1,8 rise +0.3 each), then a raised pad at row 9-10.
    //    The cube already sits at world (3, 0.5, 17), inside cell (1,8).
    // 2. A taller "cathedral" ceiling over the central pillar area
    //    (rows 9-10, cols 4..11), so when the player breaks out of the
    //    corridor into the central hall there's a clear sense of scale.
    // 3. The far north-east room (cols 11..13, rows 3..5) has a sunken
    //    floor: a -0.4 m pit, walkable but visually "lower".
    auto setFloor   = [&](int x, int y, float v) { m_map->SetFloorY(x, y, v); };
    auto setCeiling = [&](int x, int y, float v) { m_map->SetCeilingY(x, y, v); };

    // (1) Staircase down corridor + raised pad (cube room).
    setFloor(1, 5, 0.3f);
    setFloor(1, 6, 0.6f);
    setFloor(1, 7, 0.9f);
    setFloor(1, 8, 1.2f);   // landing under the cube
    setFloor(1, 9, 0.9f);
    setFloor(1,10, 0.6f);
    setFloor(1,11, 0.3f);

    // (2) Cathedral ceiling over central area.
    for (int x = 4; x <= 11; ++x) {
        setCeiling(x, 9,  4.5f);
        setCeiling(x, 10, 4.5f);
    }

    // (3) Sunken pit at (13, 4) - the empty cell tucked into the NE
    //      pillar room. Walkable but visually lower; demonstrates a
    //      drop the player can step down into.
    setFloor(13, 4, -0.4f);

    // ---- Sector lighting ----
    //
    // Doom-style per-cell brightness. Levels chosen to read as a small
    // story rather than just "rooms are random brightnesses":
    //
    //   1.00 - daylight (never used here; the whole map is interior)
    //   0.75 - "lit corridor": somewhere a candle or torch is burning
    //   0.55 - "central hall": ambient light from the cathedral ceiling
    //   0.40 - "side rooms":   dim, livable
    //   0.25 - "back corners": you need to squint
    //   0.10 - "the pit":      almost pitch, the eye fills in shape from
    //                          fog colour + silhouette
    //
    // Start from a uniform mid-dim base so any unset cell still feels
    // Plastiboo (no accidental 1.0 "lights on" cells), then paint the
    // story on top.
    auto setLight = [&](int x, int y, float v) { m_map->SetLightLevel(x, y, v); };
    for (int y = 0; y < m_map->Height(); ++y) {
        for (int x = 0; x < m_map->Width(); ++x) {
            if (m_map->IsEmpty(x, y)) setLight(x, y, 0.40f);
        }
    }

    // Central hall (rows 7..11, cols 3..12) - the cathedral. Ambient
    // mid-bright pool the player breaks out into after the corridor.
    for (int y = 7; y <= 11; ++y) {
        for (int x = 3; x <= 12; ++x) {
            if (m_map->IsEmpty(x, y)) setLight(x, y, 0.55f);
        }
    }

    // Main corridor along col 1 (rows 1..14) - "lit corridor". Brighter
    // at the spawn end so the player can read the geometry, falling off
    // as they descend the staircase toward the cube landing.
    setLight(1, 1, 0.85f);
    setLight(1, 2, 0.80f);
    setLight(1, 3, 0.75f);
    setLight(1, 4, 0.70f);
    setLight(1, 5, 0.65f);
    setLight(1, 6, 0.60f);
    setLight(1, 7, 0.55f);
    setLight(1, 8, 0.75f);   // warm pool right under the cube landmark
    setLight(1, 9, 0.55f);
    setLight(1,10, 0.50f);
    setLight(1,11, 0.45f);
    setLight(1,12, 0.40f);
    setLight(1,13, 0.35f);
    setLight(1,14, 0.30f);   // the long corridor swallows light at the end

    // North-east pillar room with the sunken pit. The pit itself is
    // the darkest cell in the map - everything around it is "side
    // room" dim so the pit reads as a black hole the eye drops into.
    setLight(13, 1, 0.40f);
    setLight(13, 2, 0.35f);
    setLight(13, 3, 0.25f);
    setLight(13, 4, 0.10f);  // the pit
    setLight(13, 5, 0.25f);
    setLight(14, 1, 0.45f);  // sprite stands here - just light enough to read

    // South-east "back corner" - the dimmest open area.
    setLight(14,13, 0.20f);
    setLight(14,14, 0.20f);

    m_renderer->loadMap(*m_map);

    // A few hooded figures scattered through the level. They are placed
    // at empty cell centres, their bases on the floor. Heights of ~1.8m
    // read as roughly player-sized.
    std::vector<SpriteInstance> sprites;
    auto addSprite = [&](int cx, int cy) {
        const glm::vec3 c = m_map->CellCenter(cx, cy);
        SpriteInstance s;
        // Feet sit on whatever the floor of that specific cell is, so
        // sprites placed on raised pads or in pits read correctly.
        s.position = glm::vec3(c.x, m_map->FloorYAt(cx, cy), c.z);
        s.size     = glm::vec2(1.2f, 1.8f);
        // Sprite inherits its cell's sector light. A figure in the back
        // corner reads as silhouette in shadow; the one standing in the
        // cathedral hall is clearly visible.
        s.light    = m_map->LightLevelAt(cx, cy);
        sprites.push_back(s);
    };
    addSprite(8, 8);   // dead centre, framed by the central pillar
    addSprite(1, 14);  // far south-west, end of the long corridor
    addSprite(14, 1);  // far north-east, peeking from the opposite room
    addSprite(7, 13);  // crouched in the southern pillar nook
    m_renderer->setSprites(std::move(sprites));

    setupScene();

    m_lastFrameTime = glfwGetTime();
}

Application::~Application() = default;

void Application::setupScene() {
    using namespace scene3d;

    // Player / primary camera.
    //
    // Aspect ratio matches the renderer's offscreen target (4:3) so the
    // cube isn't squished; the swapchain blit handles the actual window
    // aspect by stretching deliberately.
    Entity player = m_scene->CreateEntity("Player");

    // Spawn inside the map at cell (1, 1) - the top-left empty cell of
    // the open corridor. Eye height ~1.5 m above floor; camera looks
    // south down the corridor (default-yaw camera faces -Z = north, so
    // a 180-degree yaw flips it to face +Z = south).
    glm::vec3 spawnPos(0.0f, 1.5f, 0.0f);
    if (m_map) {
        const glm::vec3 c = m_map->CellCenter(1, 1);
        spawnPos = glm::vec3(c.x, m_map->FloorY() + 1.5f, c.z);
    }
    player.AddComponent<TransformComponent>(spawnPos);

    player.AddComponent<CameraComponent>(CameraComponent::CreatePerspective(
        glm::radians(70.0f),
        static_cast<float>(Renderer::kLowResExtent.width) /
            static_cast<float>(Renderer::kLowResExtent.height),
        0.1f,
        100.0f));

    player.AddComponent<PrimaryCameraComponent>();

    FirstPersonCameraComponent fps;
    fps.Yaw   = glm::pi<float>(); // face +Z (south) down the long corridor
    fps.Pitch = 0.0f;
    player.AddComponent<FirstPersonCameraComponent>(fps);

    m_playerEntity = player.GetHandle();
}

void Application::handleCaptureToggles() {
    // F1 toggles the cursor capture; Esc always releases.
    if (Input::IsKeyPressed(Key::F1)) {
        m_window->setCursorCaptured(!m_window->isCursorCaptured());
    }
    if (Input::IsKeyPressed(Key::Escape) && m_window->isCursorCaptured()) {
        m_window->setCursorCaptured(false);
    }

    // While the cursor is captured, ImGui should not respond to mouse
    // input - the player is "in the game". Toggle the flag based on
    // current capture state every frame so it stays in sync.
    ImGuiIO& io = ImGui::GetIO();
    if (m_window->isCursorCaptured()) {
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    } else {
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }
}

void Application::resolvePlayerCollision(const glm::vec3& preMovePos) {
    auto& reg = m_scene->GetRegistry();
    auto* tc = reg.try_get<scene3d::TransformComponent>(m_playerEntity);
    if (!tc) return;

    // Player AABB radius on the XZ plane. Slightly smaller than half a
    // cell so the player fits cleanly through 1-cell-wide gaps.
    constexpr float kRadius = 0.30f;
    // How far the eye stays from floor/ceiling.
    constexpr float kEyeFromFloor    = 1.5f;
    constexpr float kHeadFromCeiling = 0.2f;
    // Maximum step a player can auto-climb. Walls/risers taller than
    // this block movement; lower ones the player just steps up onto.
    // Classic Doom uses 24 units (~half a typical player height); 0.6 m
    // matches that proportionally for our 1.5 m eye height.
    constexpr float kStepHeight = 0.6f;

    const glm::vec3 wantedPos = tc->Translation;
    const glm::vec3 delta     = wantedPos - preMovePos;

    glm::vec3 resolved = preMovePos;

    // Slide X first. A trial move is rejected if it would intersect a
    // wall OR step up onto a riser taller than kStepHeight relative to
    // the player's current floor (the player's "knees").
    const float currentFloor = m_map->MaxFloorYInAABB(resolved.x, resolved.z, kRadius);
    {
        const float nx = resolved.x + delta.x;
        if (!m_map->IsBlockedAABB(nx, resolved.z, kRadius)) {
            const float trialFloor = m_map->MaxFloorYInAABB(nx, resolved.z, kRadius);
            if (trialFloor - currentFloor <= kStepHeight) {
                resolved.x = nx;
            }
        }
    }
    // Then slide Z. Independent-axis testing is what produces the
    // classic "slide along the wall" behaviour for diagonal input.
    {
        const float nz = resolved.z + delta.z;
        if (!m_map->IsBlockedAABB(resolved.x, nz, kRadius)) {
            const float trialFloor = m_map->MaxFloorYInAABB(resolved.x, nz, kRadius);
            if (trialFloor - currentFloor <= kStepHeight) {
                resolved.z = nz;
            }
        }
    }

    // Y: snap the eye to the highest floor under the player's feet,
    // plus eye height. This gives free step-up / step-down behaviour
    // (no jumping, no flight) at the cost of removing Space/LCtrl flight
    // - which is the right tradeoff now that there's a real map under
    // the player. A noclip toggle can come back as an ImGui control.
    const float floorHere   = m_map->MaxFloorYInAABB(resolved.x, resolved.z, kRadius);
    const float ceilingHere = m_map->MinCeilingYInAABB(resolved.x, resolved.z, kRadius);
    float       y           = floorHere + kEyeFromFloor;
    const float yMax        = ceilingHere - kHeadFromCeiling;
    if (y > yMax) y = yMax; // ducked rooms: cap below ceiling.
    resolved.y = y;
    (void)wantedPos; // Space/LCtrl input ignored deliberately.

    tc->Translation = resolved;
}

Application::MouseDelta Application::sampleMouseDelta() {
    double x = 0.0, y = 0.0;
    glfwGetCursorPos(m_window->getHandle(), &x, &y);

    const bool captured = m_window->isCursorCaptured();

    // Whenever the capture state changes (or on the very first sample),
    // reset our reference so the next-frame delta is computed from the
    // newly-captured baseline. Without this the camera would snap on
    // every Esc/F1 press.
    if (!m_hasLastCursor || captured != m_lastCursorCapturedState) {
        m_lastCursorX = x;
        m_lastCursorY = y;
        m_hasLastCursor = true;
        m_lastCursorCapturedState = captured;
        return {0.0f, 0.0f};
    }

    MouseDelta d{static_cast<float>(x - m_lastCursorX),
                 static_cast<float>(y - m_lastCursorY)};
    m_lastCursorX = x;
    m_lastCursorY = y;

    // Only let the delta drive the camera while the cursor is locked.
    if (!captured) {
        return {0.0f, 0.0f};
    }
    return d;
}

void Application::run() {
    while (!m_window->shouldClose()) {
        m_window->pollEvents();
        Input::Update();

        // ---- Time ----
        const double now = glfwGetTime();
        const float dt = static_cast<float>(now - m_lastFrameTime);
        m_lastFrameTime = now;

        // ---- Input layer (capture toggles, ImGui mouse routing) ----
        handleCaptureToggles();

        // ---- Player controller ----
        //
        // The controller writes the desired position directly into the
        // player's TransformComponent. To do grid collision without
        // teaching the scene module about the map, we snapshot the
        // position before the update and resolve the requested delta
        // against the grid afterwards with axis-separated slide.
        glm::vec3 preMovePos(0.0f);
        bool havePreMovePos = false;
        if (m_playerEntity != entt::null && m_map) {
            auto& reg = m_scene->GetRegistry();
            if (auto* tc = reg.try_get<scene3d::TransformComponent>(m_playerEntity)) {
                preMovePos = tc->Translation;
                havePreMovePos = true;
            }
        }

        const MouseDelta md = sampleMouseDelta();
        scene3d::FirstPersonCameraController::Update(
            *m_scene, glm::vec2(md.x, md.y), dt);

        if (havePreMovePos) {
            resolvePlayerCollision(preMovePos);
        }

        // ---- ECS update (TransformSystem propagates locals to worlds) ----
        m_scene->OnUpdate(dt);

        // ---- Build camera matrices from the scene ----
        const scene3d::CameraData camData =
            scene3d::CameraSystem::GetPrimaryCameraData(*m_scene);

        CameraMatrices matrices{};
        if (camData.Valid) {
            matrices.view     = camData.View;
            matrices.proj     = camData.Projection;
            matrices.position = camData.Position;
        }

        // ---- Render ----
        m_renderer->render(matrices, dt);
    }
}

} // namespace plaster
