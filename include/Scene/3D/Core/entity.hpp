#pragma once

#include <utility>

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>

namespace plaster::scene3d {

class Scene;

class Entity {
public:
  Entity() = default;
  Entity(entt::entity handle, Scene *scene);

  // decltype(auto) here so empty tag components (whose emplace/get return
  // void on entt::registry) compile cleanly alongside normal components
  // that return a T&.
  template <typename T, typename... Args>
  decltype(auto) AddComponent(Args &&...args);

  template <typename T, typename... Args>
  decltype(auto) AddOrReplaceComponent(Args &&...args);

  template <typename T> decltype(auto) GetComponent();
  template <typename T> decltype(auto) GetComponent() const;

  template <typename T> bool HasComponent() const;

  template <typename T> void RemoveComponent();

  entt::entity GetHandle() const;
  Scene *GetScene() const { return m_scene; }

  bool IsValid() const;

  explicit operator bool() const;

  bool operator==(const Entity &other) const;
  bool operator!=(const Entity &other) const;

private:
  entt::entity m_handle{entt::null};
  Scene *m_scene{nullptr};

  friend class Scene;
};

} // namespace plaster::scene3d

// Template definitions live in the header so callers can instantiate them.
#include "Scene/3D/Core/scene.hpp"

namespace plaster::scene3d {

template <typename T, typename... Args>
decltype(auto) Entity::AddComponent(Args &&...args) {
  return m_scene->m_registry.template emplace<T>(m_handle, std::forward<Args>(args)...);
}

template <typename T, typename... Args>
decltype(auto) Entity::AddOrReplaceComponent(Args &&...args) {
  return m_scene->m_registry.template emplace_or_replace<T>(m_handle, std::forward<Args>(args)...);
}

template <typename T> decltype(auto) Entity::GetComponent() {
  return m_scene->m_registry.template get<T>(m_handle);
}

template <typename T> decltype(auto) Entity::GetComponent() const {
  return m_scene->m_registry.template get<T>(m_handle);
}

template <typename T> bool Entity::HasComponent() const {
  return m_scene->m_registry.template all_of<T>(m_handle);
}

template <typename T> void Entity::RemoveComponent() {
  m_scene->m_registry.template remove<T>(m_handle);
}

} // namespace plaster::scene3d
