#include "Scene/3D/Systems/transform-system.hpp"

#include "Scene/3D/Components/transform.hpp"
#include "Scene/3D/Core/scene.hpp"

namespace plaster::scene3d {

// Minimal implementation: for every entity that has a TransformComponent,
// recompute its WorldTransformComponent as if it were a root entity.
// Parent/child hierarchy traversal (RelationshipComponent) is intentionally
// deferred until the scene graph is actually consumed by a renderer.
//
// This keeps Scene::OnUpdate() doing something useful (and testable) without
// committing to a hierarchy traversal strategy before we know which one we
// need (dirty flags, post-order DFS, sorted topological pass, etc.).
void TransformSystem::Update(Scene& scene) {
    auto& registry = scene.GetRegistry();

    auto view = registry.view<TransformComponent>();
    for (auto entity : view) {
        const auto& local = view.get<TransformComponent>(entity);
        registry.emplace_or_replace<WorldTransformComponent>(
            entity, local.GetTransform());
    }
}

// The remaining private helpers in the header are reserved for the future
// hierarchy implementation; intentionally not defined yet to avoid the
// temptation of pre-committing to an algorithm.

} // namespace plaster::scene3d
