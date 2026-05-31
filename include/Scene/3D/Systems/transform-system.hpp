#pragma once

#include <entt/entity/fwd.hpp>
#include <glm/glm.hpp>

namespace plaster::scene3d {
class Scene;
class Entity;

class TransformSystem {
public:
  static void Update(Scene &scene);

private:
  static void UpdateRootTransform(Scene &scene);
  static void UpdateHierarchy(Scene &scene, entt::entity entity,
                              const glm::mat4 &parentWorldTransform);
  static void UpdateWorldTransform(Scene &scene, entt::entity entity,
                                   const glm::mat4 &worldTransform);
  static bool IsDirty(Scene &scene, entt::entity entity);
  static void MarkClean(Scene &scene, entt::entity entity);
  static void MarkDirtyRecursive(Scene &scene, entt::entity entity);
};

} // namespace plaster::scene3d
