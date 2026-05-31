#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace plaster::scene3d {

// Asset handle types for mesh and material references.
// These are opaque identifiers that the rendering system uses to look up
// actual asset data from resource managers.
//
// Design rationale:
// - Using handles instead of pointers/references allows:
//   * Hot-reloading assets without invalidating components
//   * Serialization (just save the ID)
//   * Thread-safe sharing across systems
//   * Deferred loading (handle exists before asset is loaded)
// - uint32_t provides 4 billion unique assets, sufficient for any game
// - Zero-cost abstraction: compiles to the same code as raw uint32_t
//
// Future: Replace with a proper Handle<T> template that includes
// generation counters for detecting stale handles.
using MeshHandle = uint32_t;
using MaterialHandle = uint32_t;

// Special sentinel value for invalid/unset handles
constexpr MeshHandle kInvalidMeshHandle = 0;
constexpr MaterialHandle kInvalidMaterialHandle = 0;

// Shadow casting behavior for mesh renderers.
// Determines how the entity participates in shadow rendering passes.
//
// Design rationale:
// - Matches Unity/Unreal conventions for maximum familiarity
// - ShadowsOnly is critical for optimization: invisible geometry that
//   only affects lighting (e.g., simplified shadow proxies)
// - TwoSided handles special cases like foliage where backfaces cast shadows
enum class ShadowCastingMode : uint8_t {
  Off = 0,        // No shadow casting
  On = 1,         // Cast shadows from front faces only
  TwoSided = 2,   // Cast shadows from both front and back faces
  ShadowsOnly = 3 // Cast shadows but don't render the mesh itself
};

// MeshRendererComponent defines what geometry and material to render
// for an entity, along with rendering flags and layer configuration.
//
// This is the core component that makes an entity visible in the scene.
// The rendering system queries for entities with both MeshRendererComponent
// and TransformComponent to build the render queue each frame.
//
// Design rationale:
// - Follows Unity's proven RenderMesh pattern from years of production use
// - Shared component semantics: entities with identical render settings
//   can be batched together efficiently (critical for Vulkan performance)
// - Separate mesh and material handles allow:
//   * Same mesh with different materials (character color variants)
//   * Same material on different meshes (shared shaders)
//   * Independent hot-reloading of mesh vs material assets
// - Layer system enables selective rendering (main view vs minimap vs shadows)
// - Shadow flags are per-component, not global, for fine-grained control
//
// Memory layout: 20 bytes on 64-bit systems
// - 4 bytes: MeshHandle
// - 4 bytes: MaterialHandle
// - 4 bytes: Layer
// - 4 bytes: SubmeshIndex
// - 1 byte:  ShadowCastingMode
// - 1 byte:  ReceiveShadows
// - 1 byte:  Visible
// - 1 byte:  padding
struct MeshRendererComponent {
  // Core rendering references
  MeshHandle Mesh = kInvalidMeshHandle;
  MaterialHandle Material = kInvalidMaterialHandle;

  // Rendering layer (bit mask for selective rendering)
  // Layer 0 (bit 0): Default rendering
  // Layer 1 (bit 1): UI/overlay
  // Layer 2 (bit 2): Minimap only
  // etc.
  // The camera's culling mask is ANDed with this to determine visibility
  uint32_t Layer = 1; // Default layer (bit 0 set)

  // For meshes with multiple sub-meshes (e.g., character with separate
  // head/body/clothing materials). Most meshes use submesh 0.
  uint32_t SubmeshIndex = 0;

  // Shadow configuration
  ShadowCastingMode CastShadows = ShadowCastingMode::On;
  bool ReceiveShadows = true;

  // Visibility toggle (allows disabling rendering without removing component)
  // The rendering system skips entities where Visible == false
  bool Visible = true;

  // Default constructor - creates an invalid/empty renderer
  // Must call SetMesh() and SetMaterial() before this is renderable
  MeshRendererComponent() = default;

  // Primary constructor - creates a fully configured renderer
  MeshRendererComponent(MeshHandle mesh, MaterialHandle material,
                        uint32_t layer = 1,
                        ShadowCastingMode castShadows = ShadowCastingMode::On,
                        bool receiveShadows = true)
      : Mesh(mesh), Material(material), Layer(layer),
        CastShadows(castShadows), ReceiveShadows(receiveShadows) {}

  // Asset reference setters
  void SetMesh(MeshHandle mesh) { Mesh = mesh; }

  void SetMaterial(MaterialHandle material) { Material = material; }

  // Validation - checks if this component has valid asset references
  bool IsValid() const {
    return Mesh != kInvalidMeshHandle && Material != kInvalidMaterialHandle;
  }

  // Visibility control
  void SetVisible(bool visible) { Visible = visible; }

  bool IsVisible() const { return Visible; }

  void Show() { Visible = true; }

  void Hide() { Visible = false; }

  // Shadow configuration
  void SetCastShadows(ShadowCastingMode mode) { CastShadows = mode; }

  void SetReceiveShadows(bool receive) { ReceiveShadows = receive; }

  bool DoesCastShadows() const { return CastShadows != ShadowCastingMode::Off; }

  bool DoesReceiveShadows() const { return ReceiveShadows; }

  // Layer management
  void SetLayer(uint32_t layer) { Layer = layer; }

  uint32_t GetLayer() const { return Layer; }

  // Check if this renderer is visible on a specific layer mask
  // Used by the camera system during culling
  bool IsVisibleOnLayerMask(uint32_t cameraMask) const {
    return Visible && (Layer & cameraMask) != 0;
  }

  // Submesh control (for multi-material meshes)
  void SetSubmeshIndex(uint32_t index) { SubmeshIndex = index; }

  uint32_t GetSubmeshIndex() const { return SubmeshIndex; }
};

// Optional component for entities that need custom rendering bounds
// for culling optimization. If absent, the rendering system uses the
// mesh's default bounding volume.
//
// Design rationale:
// - Separated from MeshRendererComponent to keep it lightweight
// - Most entities don't need custom bounds
// - Useful for:
//   * Procedurally generated meshes where bounds aren't precomputed
//   * Animated meshes that expand beyond their rest pose
//   * Particle systems with dynamic extents
struct RenderBoundsComponent {
  // Axis-aligned bounding box in local space
  // Min and Max are relative to the entity's transform
  glm::vec3 Min = glm::vec3{-1.0f, -1.0f, -1.0f};
  glm::vec3 Max = glm::vec3{1.0f, 1.0f, 1.0f};

  RenderBoundsComponent() = default;

  RenderBoundsComponent(const glm::vec3 &min, const glm::vec3 &max)
      : Min(min), Max(max) {}

  // Compute the center point of the bounding box
  glm::vec3 GetCenter() const { return (Min + Max) * 0.5f; }

  // Compute the extents (half-size) of the bounding box
  glm::vec3 GetExtents() const { return (Max - Min) * 0.5f; }

  // Get the size (full dimensions) of the bounding box
  glm::vec3 GetSize() const { return Max - Min; }

  // Expand the bounds to include a point
  void Encapsulate(const glm::vec3 &point) {
    Min = glm::min(Min, point);
    Max = glm::max(Max, point);
  }

  // Expand the bounds to include another bounding box
  void Encapsulate(const RenderBoundsComponent &other) {
    Min = glm::min(Min, other.Min);
    Max = glm::max(Max, other.Max);
  }

  // Check if bounds are valid (Max >= Min on all axes)
  bool IsValid() const {
    return Max.x >= Min.x && Max.y >= Min.y && Max.z >= Min.z;
  }
};

// Tag component to mark entities that should be rendered with wireframe
// overlay. Useful for debugging mesh topology and visualization tools.
//
// Design rationale:
// - Tag component (empty struct) for efficient ECS queries
// - Renderer can query for (MeshRenderer + WireframeOverlay) separately
// - No performance cost for normal rendering (tags have zero memory overhead)
struct WireframeOverlayComponent {
  // Empty tag - presence indicates wireframe rendering
};

// Tag component for entities that should be excluded from frustum culling.
// Forces the entity to always be rendered regardless of camera visibility.
//
// Design rationale:
// - Useful for:
//   * UI elements attached to world space
//   * Debug visualization that should always be visible
//   * Skyboxes and distant decorative geometry
// - Performance note: Use sparingly - culling is critical for performance
struct DisableFrustumCullingComponent {
  // Empty tag - presence disables culling for this entity
};

} // namespace plaster::scene3d
