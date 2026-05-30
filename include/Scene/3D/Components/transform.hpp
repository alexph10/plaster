#pragma once

#include <vector>

#include <entt/entity/entity.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace plaster::scene3d {

// The transform module defines an entity's spatial state in 3D space,
// including position, rotation, and scale, and provides the data needed
// to derive local and world-space transforms. Local transform data belongs
// to the entity itself, while world transforms are typically derived by
// composing that local state through the entity hierarchy.

struct TransformComponent {
  glm::vec3 Translation{0.0f, 0.0f, 0.0f};
  glm::vec3 Rotation{0.0f, 0.0f, 0.0f}; // Euler angles in radians
  glm::vec3 Scale{1.0f, 1.0f, 1.0f};

  TransformComponent() = default;

  TransformComponent(const glm::vec3 &translation,
                     const glm::vec3 &rotation = glm::vec3{0.0f},
                     const glm::vec3 &scale = glm::vec3{1.0f})
      : Translation(translation), Rotation(rotation), Scale(scale) {}

  glm::quat GetOrientation() const { return glm::quat(Rotation); }

  glm::mat4 GetRotationMatrix() const { return glm::toMat4(GetOrientation()); }

  glm::mat4 GetTranslationMatrix() const {
    return glm::translate(glm::mat4(1.0f), Translation);
  }

  glm::mat4 GetScaleMatrix() const {
    return glm::scale(glm::mat4(1.0f), Scale);
  }

  glm::mat4 GetTransform() const {
    return GetTranslationMatrix() * GetRotationMatrix() * GetScaleMatrix();
  }

  glm::vec3 GetForwardDirection() const {
    return glm::rotate(GetOrientation(), glm::vec3{0.0f, 0.0f, -1.0f});
  }

  glm::vec3 GetBackwardDirection() const { return -GetForwardDirection(); }

  glm::vec3 GetRightDirection() const {
    return glm::rotate(GetOrientation(), glm::vec3{1.0f, 0.0f, 0.0f});
  }

  glm::vec3 GetLeftDirection() const { return -GetRightDirection(); }

  glm::vec3 GetUpDirection() const {
    return glm::rotate(GetOrientation(), glm::vec3{0.0f, 1.0f, 0.0f});
  }

  glm::vec3 GetDownDirection() const { return -GetUpDirection(); }

  void SetTranslation(const glm::vec3 &translation) {
    Translation = translation;
  }

  void SetRotation(const glm::vec3 &rotation) { Rotation = rotation; }

  void SetScale(const glm::vec3 &scale) { Scale = scale; }

  void Translate(const glm::vec3 &delta) { Translation += delta; }

  void Rotate(const glm::vec3 &deltaRadians) { Rotation += deltaRadians; }

  void ScaleBy(const glm::vec3 &factor) { Scale *= factor; }

  void Reset() {
    Translation = glm::vec3{0.0f, 0.0f, 0.0f};
    Rotation = glm::vec3{0.0f, 0.0f, 0.0f};
    Scale = glm::vec3{1.0f, 1.0f, 1.0f};
  }
};

struct WorldTransformComponent {
  glm::mat4 WorldTransform{1.0f};

  WorldTransformComponent() = default;

  explicit WorldTransformComponent(const glm::mat4 &worldTransform)
      : WorldTransform(worldTransform) {}
};

struct RelationshipComponent {
  entt::entity Parent{entt::null};
  std::vector<entt::entity> Children{};

  RelationshipComponent() = default;

  bool HasParent() const { return Parent != entt::null; }

  bool HasChildren() const { return !Children.empty(); }

  std::size_t ChildCount() const { return Children.size(); }

  bool IsRoot() const { return Parent == entt::null; }

  void ClearChildren() { Children.clear(); }
};

struct TransformDirtyComponent {
  bool Dirty = true;

  TransformDirtyComponent() = default;

  explicit TransformDirtyComponent(bool dirty) : Dirty(dirty) {}

  void MarkDirty() { Dirty = true; }

  void ClearDirty() { Dirty = false; }
};

} // namespace plaster::scene3d
