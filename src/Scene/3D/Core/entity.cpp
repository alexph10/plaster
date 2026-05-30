#include "Scene/3D/Core/entity.hpp"

#include <entt/entity/entity.hpp>

namespace plaster::scene3d {
Entity::Entity(entt::entity handle, Scene *scene)
    : m_handle(handle), m_scene(scene) {}

entt::entity Entity::GetHandle() const { return m_handle; }

bool Entity::IsValid() const {
  return m_scene != nullptr && m_handle != entt::null;
}

Entity::operator bool() const { return IsValid(); }

bool Entity::operator==(const Entity &other) const {
    return m_handle == other.m_handle && m_scene == other.m_scene;
}

bool Entity::operator!=(const Entity &other) const { return !(*this == other); }

} // namespace plaster::scene3d
