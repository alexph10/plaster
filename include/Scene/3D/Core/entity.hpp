#pragma once

#include <utility>

#include <entt/entity/entity.hpp>

namespace plaster::scene3d {
class Scene;
class Entity {
public:
  Entity() = default;
  Entity(entt::entity handle, Scene *scene);

  template <typename T, typename... Args>
  T &AddComponent(Args &&...args);

  template <typename T, typename... Args>
  T &AddOrReplaceComponent(Args &&...args);

  template <typename T, typename... Args>
  T &GetComponent();

  template <typename T> const T &GetComponent() const;

  template <typename T> bool HasComponent() const;

  template <typename T> void RemoveComponent();

  entt::entity GetHandle() const;

  bool IsValid() const;

  explicit operator bool() const;

  bool operator==(const Entity &other) const;
  bool operator!=(const Entity &other) const;

private:
  entt::entity m_handle{entt::null};
  Scene* m_scene{nullptr};
};
} // namespace plaster::scene3d
