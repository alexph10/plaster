#pragma once

#include <glm/glm.hpp>

namespace plaster::scene3d {

class Scene;
class Entity;

struct CameraData {
  Entity CameraEntity;

  glm::mat4 View{1.0f};
  glm::mat4 Projection{1.0f};
  glm::mat4 ViewProjection{1.0f};

  glm::vec3 Position{0.0f, 0.0f, 0.0f};
  glm::vec3 Forward{0.0f, 0.0f, -1.0f};
  glm::vec3 Up{0.0f, 1.0f, 0.0f};

  bool Valid = false;
};

class CameraSystem {
public:
  static CameraData GetPrimaryCameraData(Scene &scene);
  static Entity GetPrimaryCamera(Scene &scene);

private:
  static CameraData BuildCameraData(Scene &scene, Entity camera);
  static glm::mat4 BuildViewMatrix(Scene &scene, Entity camera);
  static glm::mat4 BuildProjectionMatrix(Scene &scene, Entity camera);
};

} // namespace plaster::scene3d
