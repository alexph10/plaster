#pragma once

#include <vector>

#include <entt/entity/fwd.hpp>
#include <glm/glm.hpp>

namespace plaster::scene3d {
class Scene;
class Entity;

struct RenderCommand {
  entt::entity EntityHandle{entt::null};

  uint32_t Mesh{0};
  uint32_t Material{0};
  uint32_t SubmeshIndex{0};

  glm::mat4 WorldTransform{1.0f};

  uint32_t Layer{1};
  float DistanceToCamera{0.0f};
};

class RenderSystem {
public:
  static void Render(Scene &scene);

private:
  static Entity GetPrimaryCamera(Scene &scene);

  static bool CanRender(Scene &scene, Entity camera, entt::entity entity);
  static bool MatchesCameraLayerMask(Scene &scene, Entity camera,
                                     entt::entity entity);
  static std::vector<RenderCommand> BuildRenderQueue(Scene &scene,
                                                     Entity camera);
  static RenderCommand BuildRenderCommand(Scene &scene, entt::entity entity);
  static float ComputeDistanceToCamera(Scene &scene, Entity camera,
                                       entt::entity entity);
  static void SortRenderQueue(std::vector<RenderCommand> &queue);
  static void SubmitRenderQueue(Scene &scene, Entity camera,
                                const std::vector<RenderCommand> &queue);
};

} // namespace plaster::scene3d
