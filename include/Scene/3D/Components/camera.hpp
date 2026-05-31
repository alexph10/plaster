#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace plaster::scene3d {

// Camera projection determines how 3D space is mapped to 2D screen space.
// Perspective creates realistic depth scaling (objects farther away appear smaller).
// Orthographic maintains uniform size regardless of distance (used in 2D, CAD, UI).
enum class ProjectionType {
  Perspective,  // Realistic 3D depth with field of view
  Orthographic  // Parallel projection, no depth scaling
};

// CameraComponent defines the lens and projection properties of a camera.
// This is pure data - the rendering system uses this component along with
// the entity's TransformComponent to compute view and projection matrices.
//
// Design rationale:
// - Field of view (FOV) is stored in radians for consistency with GLM
// - Aspect ratio can be set manually or computed from viewport dimensions
// - Near/far planes should maintain a reasonable ratio (far/near < 10000)
//   to avoid z-fighting and depth precision issues
// - Projection matrices are NOT cached here - they are computed by systems
//   as needed, keeping components as pure data
struct CameraComponent {
  ProjectionType ProjectionMode = ProjectionType::Perspective;

  // Perspective-specific settings
  float FieldOfView = glm::radians(60.0f); // In radians (60° default)
  float AspectRatio = 16.0f / 9.0f;

  // Orthographic-specific settings
  float OrthographicSize = 10.0f; // Half-height of the view volume

  // Shared clipping planes
  float NearPlane = 0.1f;
  float FarPlane = 1000.0f;

  CameraComponent() = default;

  // Perspective camera constructor
  static CameraComponent CreatePerspective(
      float fovRadians = glm::radians(60.0f),
      float aspectRatio = 16.0f / 9.0f,
      float nearPlane = 0.1f,
      float farPlane = 1000.0f) {
    CameraComponent camera;
    camera.ProjectionMode = ProjectionType::Perspective;
    camera.FieldOfView = fovRadians;
    camera.AspectRatio = aspectRatio;
    camera.NearPlane = nearPlane;
    camera.FarPlane = farPlane;
    return camera;
  }

  // Orthographic camera constructor
  static CameraComponent CreateOrthographic(
      float size = 10.0f,
      float aspectRatio = 16.0f / 9.0f,
      float nearPlane = -100.0f,
      float farPlane = 100.0f) {
    CameraComponent camera;
    camera.ProjectionMode = ProjectionType::Orthographic;
    camera.OrthographicSize = size;
    camera.AspectRatio = aspectRatio;
    camera.NearPlane = nearPlane;
    camera.FarPlane = farPlane;
    return camera;
  }

  // Compute the projection matrix based on current settings.
  // This is provided as a helper method for systems to use.
  // In ECS, the camera system or renderer typically calls this
  // rather than caching the matrix in the component.
  glm::mat4 GetProjectionMatrix() const {
    if (ProjectionMode == ProjectionType::Perspective) {
      return glm::perspective(FieldOfView, AspectRatio, NearPlane, FarPlane);
    } else {
      float halfWidth = OrthographicSize * AspectRatio;
      float halfHeight = OrthographicSize;
      return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight,
                        NearPlane, FarPlane);
    }
  }

  // Update aspect ratio (typically called when viewport resizes)
  void SetAspectRatio(float width, float height) {
    AspectRatio = width / height;
  }

  void SetFieldOfViewDegrees(float degrees) {
    FieldOfView = glm::radians(degrees);
  }

  float GetFieldOfViewDegrees() const { return glm::degrees(FieldOfView); }

  bool IsPerspective() const {
    return ProjectionMode == ProjectionType::Perspective;
  }

  bool IsOrthographic() const {
    return ProjectionMode == ProjectionType::Orthographic;
  }
};

// Tag component to mark the primary/active camera.
// Only one entity should have this component at a time.
// The rendering system queries for entities with both CameraComponent
// and PrimaryCameraComponent to determine which camera to use.
//
// Design rationale:
// - Using a tag component instead of a boolean flag allows efficient
//   ECS queries (systems can directly filter for primary cameras)
// - Supports multiple cameras in the scene with easy switching
// - Follows the "composition over configuration" ECS principle
struct PrimaryCameraComponent {
  // Empty tag component - presence indicates this is the active camera
};

// Optional component for camera-specific rendering settings.
// This separates projection data (CameraComponent) from rendering concerns.
struct CameraRenderSettings {
  // Clear color when rendering (background color)
  glm::vec4 ClearColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);

  // Viewport rectangle (in pixels, relative to window)
  // x, y = bottom-left corner, z, w = width, height
  // Default (0, 0, 0, 0) means use full window
  glm::vec4 Viewport = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

  // Rendering layer mask (which layers this camera renders)
  // Bit flags - set bit N to render layer N
  uint32_t CullingMask = 0xFFFFFFFF; // All layers by default

  // Depth for camera stacking (higher = rendered later, on top)
  int32_t Depth = 0;

  CameraRenderSettings() = default;
};

} // namespace plaster::scene3d
