#include "Graphics/ImGuiManager.h"
#include "Graphics/VulkanContext.h"
#include "Core/Window.h"

#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"

#include <vector>

namespace plaster {

ImGuiManager::ImGuiManager(Window* window, VulkanContext* vulkanContext, VkRenderPass renderPass)
    : m_window(window), m_vulkanContext(vulkanContext), m_imguiDescriptorPool(VK_NULL_HANDLE) {
    
  // Setup ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  // Setup Dear ImGui style
  setTheme();

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForVulkan(m_window->getHandle(), true);
    
  // Create descriptor pool for ImGui
  VkDescriptorPoolSize poolSizes[] = {
      {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
  };
    
  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  poolInfo.maxSets = 1000;
  poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
  poolInfo.pPoolSizes = poolSizes;

  vkCreateDescriptorPool(m_vulkanContext->getDevice(), &poolInfo, nullptr, &m_imguiDescriptorPool);

  // Initialize ImGui Vulkan backend
  ImGui_ImplVulkan_InitInfo initInfo{};
  initInfo.Instance = m_vulkanContext->getInstance();
  initInfo.PhysicalDevice = m_vulkanContext->getPhysicalDevice();
  initInfo.Device = m_vulkanContext->getDevice();
  initInfo.QueueFamily = m_vulkanContext->getGraphicsQueueFamily();
  initInfo.Queue = m_vulkanContext->getGraphicsQueue();
  initInfo.DescriptorPool = m_imguiDescriptorPool;
  initInfo.MinImageCount = 2;
  initInfo.ImageCount = 2;
  initInfo.CheckVkResultFn = nullptr;
  initInfo.PipelineInfoMain.RenderPass = renderPass;

  ImGui_ImplVulkan_Init(&initInfo);
}

ImGuiManager::~ImGuiManager() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (m_imguiDescriptorPool) {
        vkDestroyDescriptorPool(m_vulkanContext->getDevice(), m_imguiDescriptorPool, nullptr);
    }
}

void ImGuiManager::newFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiManager::render(VkCommandBuffer commandBuffer) {
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer);
}

void ImGuiManager::setTheme() {
  ImGuiStyle& style = ImGui::GetStyle();
  ImVec4* colors = style.Colors;

  // Acrylic Orange color palette
  ImVec4 vibrantOrange = ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
  ImVec4 softOrange = ImVec4(1.0f, 0.7f, 0.3f, 1.0f);
  ImVec4 deepOrange = ImVec4(0.9f, 0.4f, 0.1f, 1.0f);
  ImVec4 darkBase = ImVec4(0.06f, 0.06f, 0.06f, 0.85f);
  ImVec4 darkerBase = ImVec4(0.04f, 0.04f, 0.04f, 0.90f);
  ImVec4 accentDark = ImVec4(0.12f, 0.12f, 0.12f, 0.80f);
  ImVec4 starkWhite = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  ImVec4 transparent = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

  // Background & Surface colors
  colors[ImGuiCol_WindowBg] = darkBase;
  colors[ImGuiCol_ChildBg] = ImVec4(0.04f, 0.04f, 0.04f, 0.5f);
  colors[ImGuiCol_PopupBg] = darkerBase;
  colors[ImGuiCol_Border] = ImVec4(vibrantOrange.x, vibrantOrange.y, vibrantOrange.z, 0.3f);
  colors[ImGuiCol_BorderShadow] = transparent;

  // Title bar colors
  colors[ImGuiCol_TitleBg] = accentDark;
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.15f, 0.15f, 0.85f);
  colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.04f, 0.04f, 0.04f, 0.7f);

  // Text colors
  colors[ImGuiCol_Text] = starkWhite;
  colors[ImGuiCol_TextDisabled] = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);

  // Button colors
  colors[ImGuiCol_Button] = ImVec4(accentDark.x, accentDark.y, accentDark.z, 0.6f);
  colors[ImGuiCol_ButtonHovered] = vibrantOrange;
  colors[ImGuiCol_ButtonActive] = deepOrange;

  // Header colors
  colors[ImGuiCol_Header] = ImVec4(accentDark.x, accentDark.y, accentDark.z, 0.7f);
  colors[ImGuiCol_HeaderHovered] = vibrantOrange;
  colors[ImGuiCol_HeaderActive] = deepOrange;

  // Frame/Input colors
  colors[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.6f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.12f, 0.12f, 0.7f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(vibrantOrange.x, vibrantOrange.y, vibrantOrange.z, 0.4f);

  // Scrollbar colors
  colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.4f);
  colors[ImGuiCol_ScrollbarGrab] = accentDark;
  colors[ImGuiCol_ScrollbarGrabHovered] = vibrantOrange;
  colors[ImGuiCol_ScrollbarGrabActive] = deepOrange;

  // Slider colors
  colors[ImGuiCol_SliderGrab] = vibrantOrange;
  colors[ImGuiCol_SliderGrabActive] = deepOrange;

  // Checkbox colors
  colors[ImGuiCol_CheckMark] = vibrantOrange;

  // Tab colors (TRANSPARENT)
  colors[ImGuiCol_Tab] = ImVec4(0.08f, 0.08f, 0.08f, 0.3f);
  colors[ImGuiCol_TabHovered] = ImVec4(vibrantOrange.x, vibrantOrange.y, vibrantOrange.z, 0.6f);
  colors[ImGuiCol_TabActive] = ImVec4(vibrantOrange.x, vibrantOrange.y, vibrantOrange.z, 0.5f);
  colors[ImGuiCol_TabUnfocused] = ImVec4(0.04f, 0.04f, 0.04f, 0.2f);
  colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.08f, 0.08f, 0.08f, 0.4f);

  // Separator colors
  colors[ImGuiCol_Separator] = ImVec4(vibrantOrange.x, vibrantOrange.y, vibrantOrange.z, 0.3f);
  colors[ImGuiCol_SeparatorHovered] = vibrantOrange;
  colors[ImGuiCol_SeparatorActive] = deepOrange;

  // Resize grip colors
  colors[ImGuiCol_ResizeGrip] = ImVec4(vibrantOrange.x, vibrantOrange.y, vibrantOrange.z, 0.2f);
  colors[ImGuiCol_ResizeGripHovered] = ImVec4(vibrantOrange.x, vibrantOrange.y, vibrantOrange.z, 0.6f);
  colors[ImGuiCol_ResizeGripActive] = vibrantOrange;

  // Plot colors
  colors[ImGuiCol_PlotLines] = vibrantOrange;
  colors[ImGuiCol_PlotLinesHovered] = softOrange;
  colors[ImGuiCol_PlotHistogram] = vibrantOrange;
  colors[ImGuiCol_PlotHistogramHovered] = softOrange;

  // Menu bar
  colors[ImGuiCol_MenuBarBg] = ImVec4(0.06f, 0.06f, 0.06f, 0.7f);

  // Drag & Drop
  colors[ImGuiCol_DragDropTarget] = ImVec4(vibrantOrange.x, vibrantOrange.y, vibrantOrange.z, 0.8f);

  // Navigation
  colors[ImGuiCol_NavHighlight] = vibrantOrange;

  // Window styling (acrylic glass effect)
  style.WindowPadding = ImVec2(10.0f, 10.0f);
  style.FramePadding = ImVec2(5.0f, 4.0f);
  style.ItemSpacing = ImVec2(10.0f, 5.0f);
  style.ItemInnerSpacing = ImVec2(5.0f, 5.0f);
  style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
  style.IndentSpacing = 21.0f;
  style.ScrollbarSize = 16.0f;
  style.GrabMinSize = 12.0f;
    
  // Rounding (smooth acrylic edges)
  style.WindowRounding = 8.0f;
  style.ChildRounding = 6.0f;
  style.FrameRounding = 5.0f;
  style.PopupRounding = 6.0f;
  style.ScrollbarRounding = 9.0f;
  style.GrabRounding = 5.0f;
  style.TabRounding = 6.0f;
  
  // Transparency settings
  style.Alpha = 0.98f;
  style.WindowBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;
}

} // namespace plaster

