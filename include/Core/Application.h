#pragma once

namespace plaster {

class Window;
class VulkanContext;
class Renderer;

class Application {
public:
  Application();
  ~Application();

  void run();

private:
  Window* m_window;
  VulkanContext* m_vulkanContext;
  Renderer* m_renderer;
};

} // namespace plaster
