#include "Scene/3D/Systems/render-system.hpp"

#include "Scene/3D/Core/entity.hpp"
#include "Scene/3D/Core/scene.hpp"

namespace plaster::scene3d {

// Scene-driven rendering is roadmap step 5 ("Scene + camera"). For now the
// Vulkan renderer owns the cube directly. We provide compile-time stubs so
// Scene::OnRender() links cleanly and so future code can fill these in
// without touching any other translation unit.
//
// The intended contract once filled in:
//   1. Find the primary camera entity.
//   2. Build a RenderQueue of (Mesh, Material, WorldTransform) commands.
//   3. Sort by material/depth.
//   4. Hand the queue to the active rendering backend.

void RenderSystem::Render(Scene& /*scene*/) {
    // no-op: rendering is driven by Renderer for now.
}

Entity RenderSystem::GetPrimaryCamera(Scene& /*scene*/) {
    return Entity{};
}

bool RenderSystem::CanRender(Scene& /*scene*/, Entity /*camera*/, entt::entity /*entity*/) {
    return false;
}

bool RenderSystem::MatchesCameraLayerMask(Scene& /*scene*/, Entity /*camera*/,
                                          entt::entity /*entity*/) {
    return false;
}

std::vector<RenderCommand> RenderSystem::BuildRenderQueue(Scene& /*scene*/,
                                                          Entity /*camera*/) {
    return {};
}

RenderCommand RenderSystem::BuildRenderCommand(Scene& /*scene*/, entt::entity entity) {
    RenderCommand cmd{};
    cmd.EntityHandle = entity;
    return cmd;
}

float RenderSystem::ComputeDistanceToCamera(Scene& /*scene*/, Entity /*camera*/,
                                            entt::entity /*entity*/) {
    return 0.0f;
}

void RenderSystem::SortRenderQueue(std::vector<RenderCommand>& /*queue*/) {
    // no-op
}

void RenderSystem::SubmitRenderQueue(Scene& /*scene*/, Entity /*camera*/,
                                     const std::vector<RenderCommand>& /*queue*/) {
    // no-op
}

} // namespace plaster::scene3d
