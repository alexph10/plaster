#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <memory>

namespace plaster {

class Window;
class VulkanContext;
class ImGuiManager;


class Renderer {
public:
  Renderer(Window* window, VulkanContext* vulkanContext);
  ~Renderer();

  void render();
  ImGuiManager* getImGuiManager() { return m_imguiManager.get(); }

private:
  VulkanContext* m_vulkanContext;
  Window* m_window;
  std::unique_ptr<ImGuiManager> m_imguiManager;

  VkSwapchainKHR m_swapchain;
  std::vector<VkImage> m_swapchainImages;
  std::vector<VkImageView> m_swapchainImageViews;
  VkFormat m_swapchainImageFormat;
  VkExtent2D m_swapchainExtent;

  VkRenderPass m_renderPass;
  std::vector<VkFramebuffer> m_framebuffers;

  VkCommandPool m_commandPool;
  std::vector<VkCommandBuffer> m_commandBuffers;

  std::vector<VkSemaphore> m_imageAvailableSemaphores;
  std::vector<VkSemaphore> m_renderFinishedSemaphores;
  std::vector<VkFence> m_inFlightFences;
  uint32_t m_currentFrame;
  static const int MAX_FRAMES_IN_FLIGHT = 2;

  void createSwapchain();
  void createImageViews();
  void createRenderPass();
  void createFramebuffers();
  void createCommandPool();
  void createCommandBuffers();
  void createSyncObjects();

  void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
  VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
  VkPresentModeKHR  chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
  VkExtent2D        chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
};

} // namespace plaster
