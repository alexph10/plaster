#pragma once

#include <string>

#include <entt/entity/registry.hpp>

namespace plaster::scene3d {

class Entity;

class Scene {
  friend class Entity;

public:
  Scene() = default;
  ~Scene() = default;

  Entity CreateEntity();
  Entity CreateEntity(const std::string &name);
  void DestroyEntity(Entity entity);

  void OnUpdate(float deltaTime);
  void OnRender();

  void Clear();

  entt::registry &GetRegistry() { return m_registry; }
  const entt::registry &GetRegistry() const { return m_registry; }

  bool IsValid(Entity entity) const;

private:
  entt::registry m_registry;
};

} // namespace plaster::scene3d
