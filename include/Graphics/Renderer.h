#pragma once

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include "Graphics/FogParams.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace plaster {

class Window;
class VulkanContext;
class ImGuiManager;
class CubeRenderer;
class MapRenderer;
class GridMap;
class PostProcessor;
class SpriteRenderer;
struct CameraMatrices;
struct SpriteInstance;

// Renderer orchestrates the per-frame Plastiboo pipeline:
//
//   offscreen low-res pass (cube) -> blit nearest -> swapchain pass (ImGui)
//
// The offscreen target is intentionally tiny (kLowResExtent) so geometry
// resolves to chunky pixels. The blit step stretches it across the whole
// swapchain with VK_FILTER_NEAREST, preserving the dither/jitter signature
// instead of bilinear-blurring it.
class Renderer {
public:
    // Offscreen / low-res target size. Drives the camera aspect ratio and
    // the vertex-snap grid in the cube shader.
    static constexpr VkExtent2D kLowResExtent = {320, 240};

    Renderer(Window* window, VulkanContext* vulkanContext);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Renders one frame using the supplied camera matrices. `dt` is the
    // seconds since the previous frame, used for any in-renderer
    // animation (e.g. the demo cube's spin).
    void render(const CameraMatrices& camera, float dt);

    // Builds (or rebuilds) the world geometry from the given grid map.
    // Must be called after construction; safe to call multiple times if
    // the map ever changes (the old MapRenderer is destroyed first).
    void loadMap(const GridMap& map);

    // Replaces the active sprite list. The renderer keeps a copy and
    // draws them every frame inside the geometry pass.
    void setSprites(std::vector<SpriteInstance> sprites);

    ImGuiManager* getImGuiManager() { return m_imguiManager.get(); }

private:
    static constexpr int kMaxFramesInFlight = 2;

    // ---- Setup helpers ----
    void createSwapchain();
    void createSwapchainImageViews();
    void createSwapchainRenderPass();
    void createSwapchainFramebuffers();

    void createOffscreenResources();
    void createOffscreenRenderPass();
    void createOffscreenFramebuffers();

    void createPostResources();
    void createPostRenderPass();
    void createPostFramebuffers();

    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();

    // ---- Teardown helpers ----
    void destroySwapchainResources();
    void destroyOffscreenResources();
    void destroyPostResources();

    // ---- Resize ----
    void recreateSwapchain();

    // ---- Frame ----
    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex,
                             const CameraMatrices& camera);
    void runImGui(const CameraMatrices& camera);

    // ---- Swapchain choice helpers ----
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
    VkPresentModeKHR   chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& modes);
    VkExtent2D         chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    Window*        m_window;
    VulkanContext* m_vulkanContext;

    // Swapchain (window-sized).
    VkSwapchainKHR             m_swapchain          = VK_NULL_HANDLE;
    std::vector<VkImage>       m_swapchainImages;
    std::vector<VkImageView>   m_swapchainImageViews;
    VkFormat                   m_swapchainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D                 m_swapchainExtent      = {0, 0};
    VkRenderPass               m_swapchainRenderPass  = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_swapchainFramebuffers;

    // Offscreen low-res target: one per frame-in-flight so a frame can
    // submit its blit without serializing against the previous frame.
    struct OffscreenTarget {
        VkImage        colorImage       = VK_NULL_HANDLE;
        VkDeviceMemory colorMemory      = VK_NULL_HANDLE;
        VkImageView    colorView        = VK_NULL_HANDLE;

        VkImage        depthImage       = VK_NULL_HANDLE;
        VkDeviceMemory depthMemory      = VK_NULL_HANDLE;
        VkImageView    depthView        = VK_NULL_HANDLE;

        VkFramebuffer  framebuffer      = VK_NULL_HANDLE;
    };
    std::vector<OffscreenTarget> m_offscreenTargets;
    VkRenderPass                 m_offscreenRenderPass = VK_NULL_HANDLE;
    VkFormat                     m_offscreenColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat                     m_offscreenDepthFormat = VK_FORMAT_D32_SFLOAT;

    // Post-process target: stylized color, fed to the swapchain blit.
    // One per frame-in-flight so adjacent frames don't race on the image.
    struct PostTarget {
        VkImage        colorImage  = VK_NULL_HANDLE;
        VkDeviceMemory colorMemory = VK_NULL_HANDLE;
        VkImageView    colorView   = VK_NULL_HANDLE;
        VkFramebuffer  framebuffer = VK_NULL_HANDLE;
    };
    std::vector<PostTarget> m_postTargets;
    VkRenderPass            m_postRenderPass = VK_NULL_HANDLE;

    // Commands and sync.
    VkCommandPool                m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkSemaphore>     m_imageAvailableSemaphores;
    std::vector<VkSemaphore>     m_renderFinishedSemaphores;
    std::vector<VkFence>         m_inFlightFences;
    uint32_t                     m_currentFrame = 0;

    // Subsystems.
    std::unique_ptr<CubeRenderer>   m_cubeRenderer;
    std::unique_ptr<MapRenderer>    m_mapRenderer;   // optional; set via loadMap()
    std::unique_ptr<SpriteRenderer> m_spriteRenderer;
    std::unique_ptr<PostProcessor>  m_postProcessor;
    std::unique_ptr<ImGuiManager>   m_imguiManager;

    // Active sprite list, updated by setSprites(). Stored by value so
    // Application doesn't have to keep the source vector alive.
    std::vector<SpriteInstance> m_sprites;

    // Accumulated seconds, used only to spin the demo cube.
    float m_cubeSpinSeconds = 0.0f;

    // Stylized fog state, shared by every world-space renderer (cube, map,
    // sprites). Editable via the ImGui panel. Defaults live in
    // FogParams.h and lean into the warm-ember Plastiboo palette so a
    // fresh build looks "right" without touching any sliders.
    FogParams m_fog{};
};

} // namespace plaster
