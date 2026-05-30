#pragma once

#include <vulkan/vulkan.h>
#include "imgui.h"

namespace plaster {

class Window;
class VulkanContext;

class ImGuiManager {
public:
    ImGuiManager(Window* window, VulkanContext* vulkanContext, VkRenderPass renderPass);
    ~ImGuiManager();

    void newFrame();
    void render(VkCommandBuffer commandBuffer);
    void setTheme();

private:
    VulkanContext* m_vulkanContext;
    Window* m_window;
    VkDescriptorPool m_imguiDescriptorPool;
};

} // namespace plaster

