#include "Scene/3D/Core/scene.hpp"

#include "Scene/3D/Core/entity.hpp"
#include "Scene/3D/Systems/camera-system.hpp"
#include "Scene/3D/Systems/render-system.hpp"
#include "Scene/3D/Systems/transform-system.hpp"

namespace plaster::scene3d {
Entity Scene::CreateEntity() {
  const entt::entity handle = m_registry.create();
  return Entity(handle, this);
}

Entity Scene::CreateEntity(const std::string &name) {
  Entity entity = CreateEntity();

  // TODO: Replace with TagComponent / NameComponent once implemented
  (void)name;

  return entity;
}

void Scene::DestroyEntity(Entity entity) {
  if (!IsValid(entity)) {
    return;
  }

  m_registry.destroy(entity.GetHandle());
}

void Scene::OnUpdate(float deltaTime) {
  (void)deltaTime;

  TransformSystem::Update(*this);
}

void Scene::OnRender() { RenderSystem::Render(*this); }

void Scene::Clear() { m_registry.clear(); }

bool Scene::IsValid(Entity entity) const {
    return entity.m_scene == this && entity.GetHandle() != entt::null && m_registry.valid(entity.GetHandle());
}

} // namespace plaster::scene3d
