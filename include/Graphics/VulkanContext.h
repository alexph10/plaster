#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>

namespace plaster {

class Window;

class VulkanContext {
public:
  VulkanContext(Window* window);
  ~VulkanContext();

  VkInstance getInstance() const { return m_instance; }
  VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
  VkDevice getDevice() const  { return m_device; }
  VkSurfaceKHR getSurface() const { return m_surface; }
  VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
  uint32_t getGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
private:
  VkInstance m_instance;
  VkPhysicalDevice m_physicalDevice;
  VkDevice m_device;
  VkSurfaceKHR m_surface;
  VkQueue m_graphicsQueue;
  uint32_t m_graphicsQueueFamily;
  Window* m_window;

  void createInstance();
  void pickPhysicalDevice();
  void createLogicalDevice();
  void createSurface();
};

} // namespace plaster
