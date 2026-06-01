#include "Graphics/Renderer.h"

#include "Core/Input.h"
#include "Core/Window.h"
#include "Graphics/CameraMatrices.h"
#include "Graphics/CubeRenderer.h"
#include "Graphics/GridMap.h"
#include "Graphics/MapRenderer.h"
#include "Graphics/PostProcessor.h"
#include "Graphics/SpriteRenderer.h"
#include "Graphics/UI/ImGuiManager.h"
#include "Graphics/Vulkan/VulkanContext.h"
#include "Graphics/Vulkan/VulkanUtils.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>

namespace plaster {

Renderer::Renderer(Window* window, VulkanContext* vulkanContext)
    : m_window(window), m_vulkanContext(vulkanContext) {
    createSwapchain();
    createSwapchainImageViews();
    createSwapchainRenderPass();
    createSwapchainFramebuffers();

    createCommandPool();
    createCommandBuffers();
    createSyncObjects();

    createOffscreenRenderPass();
    createOffscreenResources();
    createOffscreenFramebuffers();

    createPostRenderPass();
    createPostResources();
    createPostFramebuffers();

    m_cubeRenderer = std::make_unique<CubeRenderer>(
        m_vulkanContext, m_commandPool, m_offscreenRenderPass, kLowResExtent);

    m_spriteRenderer = std::make_unique<SpriteRenderer>(
        m_vulkanContext, m_commandPool, m_offscreenRenderPass, kLowResExtent);

    // PostProcessor needs the geometry-pass color views so it can build one
    // descriptor set per frame-in-flight.
    std::vector<VkImageView> geometryColorViews;
    geometryColorViews.reserve(m_offscreenTargets.size());
    for (const auto& t : m_offscreenTargets) {
        geometryColorViews.push_back(t.colorView);
    }
    m_postProcessor = std::make_unique<PostProcessor>(
        m_vulkanContext, m_commandPool, m_postRenderPass,
        geometryColorViews, kLowResExtent);

    m_imguiManager = std::make_unique<ImGuiManager>(
        m_window, m_vulkanContext, m_swapchainRenderPass);
}

Renderer::~Renderer() {
    VkDevice device = m_vulkanContext->getDevice();
    vkDeviceWaitIdle(device);

    // Subsystems hold Vulkan handles owned by this device; destroy first.
    m_imguiManager.reset();
    m_postProcessor.reset();
    m_spriteRenderer.reset();
    m_mapRenderer.reset();
    m_cubeRenderer.reset();

    for (size_t i = 0; i < m_imageAvailableSemaphores.size(); ++i) {
        vkDestroySemaphore(device, m_imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(device, m_renderFinishedSemaphores[i], nullptr);
        vkDestroyFence(device, m_inFlightFences[i], nullptr);
    }

    destroyPostResources();
    if (m_postRenderPass) {
        vkDestroyRenderPass(device, m_postRenderPass, nullptr);
    }

    destroyOffscreenResources();
    if (m_offscreenRenderPass) {
        vkDestroyRenderPass(device, m_offscreenRenderPass, nullptr);
    }

    if (m_commandPool) {
        vkDestroyCommandPool(device, m_commandPool, nullptr);
    }

    destroySwapchainResources();
    if (m_swapchainRenderPass) {
        vkDestroyRenderPass(device, m_swapchainRenderPass, nullptr);
    }
}

// ---------------------------------------------------------------------------
// Swapchain
// ---------------------------------------------------------------------------

VkSurfaceFormatKHR Renderer::chooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    // UNORM matches our offscreen color format (R8G8B8A8_UNORM), so the
    // BLIT is straightforward. If sRGB is the only option we still accept
    // it - the blit handles UNORM->sRGB component conversion - but our
    // colors will look gamma-darker until we introduce a tonemap.
    return formats[0];
}

VkPresentModeKHR Renderer::chooseSwapPresentMode(
    const std::vector<VkPresentModeKHR>& modes) {
    for (const auto& m : modes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    }
    return VK_PRESENT_MODE_FIFO_KHR; // Guaranteed available, vsync.
}

VkExtent2D Renderer::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps) {
    if (caps.currentExtent.width != UINT32_MAX) {
        return caps.currentExtent;
    }
    uint32_t w = m_window->getWidth();
    uint32_t h = m_window->getHeight();
    w = std::clamp(w, caps.minImageExtent.width, caps.maxImageExtent.width);
    h = std::clamp(h, caps.minImageExtent.height, caps.maxImageExtent.height);
    return {w, h};
}

void Renderer::createSwapchain() {
    VkPhysicalDevice phys = m_vulkanContext->getPhysicalDevice();
    VkDevice device = m_vulkanContext->getDevice();
    VkSurfaceKHR surface = m_vulkanContext->getSurface();

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, formats.data());

    uint32_t pmCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &pmCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(pmCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &pmCount, presentModes.data());

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(formats);
    VkPresentModeKHR   presentMode   = chooseSwapPresentMode(presentModes);
    VkExtent2D         extent        = chooseSwapExtent(caps);

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    // TRANSFER_DST is required so vkCmdBlitImage can write into the
    // swapchain image; COLOR_ATTACHMENT is required for the ImGui pass.
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = caps.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSwapchainKHR failed");
    }

    vkGetSwapchainImagesKHR(device, m_swapchain, &imageCount, nullptr);
    m_swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, m_swapchain, &imageCount, m_swapchainImages.data());

    m_swapchainImageFormat = surfaceFormat.format;
    m_swapchainExtent = extent;
}

void Renderer::createSwapchainImageViews() {
    VkDevice device = m_vulkanContext->getDevice();
    m_swapchainImageViews.resize(m_swapchainImages.size());
    for (size_t i = 0; i < m_swapchainImages.size(); ++i) {
        m_swapchainImageViews[i] = vk_utils::createImageView2D(
            device, m_swapchainImages[i], m_swapchainImageFormat,
            VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

void Renderer::createSwapchainRenderPass() {
    VkDevice device = m_vulkanContext->getDevice();

    // The swapchain pass runs *after* the blit, so it expects the image
    // already in COLOR_ATTACHMENT_OPTIMAL layout (we transition it
    // explicitly with a barrier in recordCommandBuffer) and loads existing
    // contents instead of clearing.
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    // External -> subpass dependency: wait for our manual blit barrier's
    // COLOR_ATTACHMENT_OUTPUT stage transition before letting the subpass
    // start writing the attachment.
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &colorAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dep;

    if (vkCreateRenderPass(device, &rpInfo, nullptr, &m_swapchainRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateRenderPass (swapchain) failed");
    }
}

void Renderer::createSwapchainFramebuffers() {
    VkDevice device = m_vulkanContext->getDevice();
    m_swapchainFramebuffers.resize(m_swapchainImageViews.size());

    for (size_t i = 0; i < m_swapchainImageViews.size(); ++i) {
        VkImageView attachments[] = {m_swapchainImageViews[i]};

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_swapchainRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = attachments;
        fbInfo.width = m_swapchainExtent.width;
        fbInfo.height = m_swapchainExtent.height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(device, &fbInfo, nullptr,
                                &m_swapchainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateFramebuffer (swapchain) failed");
        }
    }
}

void Renderer::destroySwapchainResources() {
    VkDevice device = m_vulkanContext->getDevice();
    for (auto fb : m_swapchainFramebuffers) {
        vkDestroyFramebuffer(device, fb, nullptr);
    }
    m_swapchainFramebuffers.clear();

    for (auto view : m_swapchainImageViews) {
        vkDestroyImageView(device, view, nullptr);
    }
    m_swapchainImageViews.clear();

    if (m_swapchain) {
        vkDestroySwapchainKHR(device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// Offscreen
// ---------------------------------------------------------------------------

void Renderer::createOffscreenRenderPass() {
    VkDevice device = m_vulkanContext->getDevice();

    VkAttachmentDescription color{};
    color.format = m_offscreenColorFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // End up in SHADER_READ_ONLY so the post-process pass can sample it as
    // a texture without an extra barrier.
    color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depth{};
    depth.format = m_offscreenDepthFormat;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    // Two external dependencies:
    //  - before: make sure any previous use of the offscreen image is done
    //    before we clear/write it.
    //  - after: ensure the color attachment writes complete before the
    //    subsequent TRANSFER stage (the blit) tries to read.
    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    deps[0].srcAccessMask = 0;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {color, depth};

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpInfo.pAttachments = attachments.data();
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = static_cast<uint32_t>(deps.size());
    rpInfo.pDependencies = deps.data();

    if (vkCreateRenderPass(device, &rpInfo, nullptr,
                           &m_offscreenRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateRenderPass (offscreen) failed");
    }
}

void Renderer::createOffscreenResources() {
    VkDevice device = m_vulkanContext->getDevice();
    m_offscreenTargets.resize(kMaxFramesInFlight);

    for (auto& t : m_offscreenTargets) {
        // Color image: usage covers attachment (for the pass) + transfer
        // src (for the blit). SAMPLED is added now so future post passes
        // can read this image as a texture without recreating it.
        vk_utils::createImage2D(
            m_vulkanContext, kLowResExtent.width, kLowResExtent.height,
            m_offscreenColorFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            t.colorImage, t.colorMemory);

        t.colorView = vk_utils::createImageView2D(
            device, t.colorImage, m_offscreenColorFormat,
            VK_IMAGE_ASPECT_COLOR_BIT);

        // Depth image: just attachment usage.
        vk_utils::createImage2D(
            m_vulkanContext, kLowResExtent.width, kLowResExtent.height,
            m_offscreenDepthFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            t.depthImage, t.depthMemory);

        t.depthView = vk_utils::createImageView2D(
            device, t.depthImage, m_offscreenDepthFormat,
            VK_IMAGE_ASPECT_DEPTH_BIT);
    }
}

void Renderer::createOffscreenFramebuffers() {
    VkDevice device = m_vulkanContext->getDevice();
    for (auto& t : m_offscreenTargets) {
        VkImageView attachments[] = {t.colorView, t.depthView};

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_offscreenRenderPass;
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments = attachments;
        fbInfo.width = kLowResExtent.width;
        fbInfo.height = kLowResExtent.height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &t.framebuffer) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateFramebuffer (offscreen) failed");
        }
    }
}

void Renderer::destroyOffscreenResources() {
    VkDevice device = m_vulkanContext->getDevice();
    for (auto& t : m_offscreenTargets) {
        if (t.framebuffer) vkDestroyFramebuffer(device, t.framebuffer, nullptr);
        if (t.colorView)   vkDestroyImageView(device, t.colorView, nullptr);
        if (t.colorImage)  vkDestroyImage(device, t.colorImage, nullptr);
        if (t.colorMemory) vkFreeMemory(device, t.colorMemory, nullptr);
        if (t.depthView)   vkDestroyImageView(device, t.depthView, nullptr);
        if (t.depthImage)  vkDestroyImage(device, t.depthImage, nullptr);
        if (t.depthMemory) vkFreeMemory(device, t.depthMemory, nullptr);
        t = {};
    }
    m_offscreenTargets.clear();
}

// ---------------------------------------------------------------------------
// Post-process (dither + palette)
// ---------------------------------------------------------------------------

void Renderer::createPostRenderPass() {
    VkDevice device = m_vulkanContext->getDevice();

    // Single color attachment. We fully overwrite it (no loadOp clear
    // needed - DONT_CARE), and end up in TRANSFER_SRC_OPTIMAL so the
    // subsequent vkCmdBlitImage reads it without an extra barrier.
    VkAttachmentDescription color{};
    color.format = m_offscreenColorFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    // Two external dependencies:
    //  - before: the geometry pass produced its output and transitioned
    //    its color image into SHADER_READ_ONLY; we need the fragment-stage
    //    reads here to come after those writes.
    //  - after: ensure our color writes complete before the trailing blit.
    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &color;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = static_cast<uint32_t>(deps.size());
    rpInfo.pDependencies = deps.data();

    if (vkCreateRenderPass(device, &rpInfo, nullptr, &m_postRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateRenderPass (post) failed");
    }
}

void Renderer::createPostResources() {
    VkDevice device = m_vulkanContext->getDevice();
    m_postTargets.resize(kMaxFramesInFlight);

    for (auto& t : m_postTargets) {
        vk_utils::createImage2D(
            m_vulkanContext, kLowResExtent.width, kLowResExtent.height,
            m_offscreenColorFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            t.colorImage, t.colorMemory);

        t.colorView = vk_utils::createImageView2D(
            device, t.colorImage, m_offscreenColorFormat,
            VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

void Renderer::createPostFramebuffers() {
    VkDevice device = m_vulkanContext->getDevice();
    for (auto& t : m_postTargets) {
        VkImageView attachments[] = {t.colorView};

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_postRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = attachments;
        fbInfo.width = kLowResExtent.width;
        fbInfo.height = kLowResExtent.height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &t.framebuffer) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateFramebuffer (post) failed");
        }
    }
}

void Renderer::destroyPostResources() {
    VkDevice device = m_vulkanContext->getDevice();
    for (auto& t : m_postTargets) {
        if (t.framebuffer) vkDestroyFramebuffer(device, t.framebuffer, nullptr);
        if (t.colorView)   vkDestroyImageView(device, t.colorView, nullptr);
        if (t.colorImage)  vkDestroyImage(device, t.colorImage, nullptr);
        if (t.colorMemory) vkFreeMemory(device, t.colorMemory, nullptr);
        t = {};
    }
    m_postTargets.clear();
}

// ---------------------------------------------------------------------------
// Commands + sync
// ---------------------------------------------------------------------------

void Renderer::createCommandPool() {
    VkDevice device = m_vulkanContext->getDevice();
    VkCommandPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = m_vulkanContext->getGraphicsQueueFamily();
    if (vkCreateCommandPool(device, &info, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateCommandPool failed");
    }
}

void Renderer::createCommandBuffers() {
    VkDevice device = m_vulkanContext->getDevice();
    m_commandBuffers.resize(kMaxFramesInFlight);

    VkCommandBufferAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool = m_commandPool;
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());
    if (vkAllocateCommandBuffers(device, &info, m_commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateCommandBuffers failed");
    }
}

void Renderer::createSyncObjects() {
    VkDevice device = m_vulkanContext->getDevice();
    m_imageAvailableSemaphores.resize(kMaxFramesInFlight);
    m_renderFinishedSemaphores.resize(kMaxFramesInFlight);
    m_inFlightFences.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo sInfo{};
    sInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fInfo{};
    fInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        vkCreateSemaphore(device, &sInfo, nullptr, &m_imageAvailableSemaphores[i]);
        vkCreateSemaphore(device, &sInfo, nullptr, &m_renderFinishedSemaphores[i]);
        vkCreateFence(device, &fInfo, nullptr, &m_inFlightFences[i]);
    }
}

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------

void Renderer::recreateSwapchain() {
    // Block while minimized (extent 0,0). Without this Vulkan rejects the
    // recreate.
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window->getHandle(), &width, &height);
    while (width == 0 || height == 0) {
        glfwWaitEvents();
        glfwGetFramebufferSize(m_window->getHandle(), &width, &height);
    }

    vkDeviceWaitIdle(m_vulkanContext->getDevice());

    destroySwapchainResources();
    createSwapchain();
    createSwapchainImageViews();
    createSwapchainFramebuffers();
    // Render passes and offscreen resources are unchanged: the offscreen
    // target stays at kLowResExtent regardless of window size, and the
    // swapchain render pass is format-compatible across resizes.
}

// ---------------------------------------------------------------------------
// Frame recording
// ---------------------------------------------------------------------------

void Renderer::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex,
                                   const CameraMatrices& camera) {
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &begin);

    // --- 1. Offscreen geometry pass ---
    const OffscreenTarget& off = m_offscreenTargets[m_currentFrame];

    std::array<VkClearValue, 2> clearValues{};
    // Pitch-black clear matches the Plastiboo "Bone & Pitch" mood.
    clearValues[0].color = {{0.02f, 0.02f, 0.03f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo offPass{};
    offPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    offPass.renderPass = m_offscreenRenderPass;
    offPass.framebuffer = off.framebuffer;
    offPass.renderArea.offset = {0, 0};
    offPass.renderArea.extent = kLowResExtent;
    offPass.clearValueCount = static_cast<uint32_t>(clearValues.size());
    offPass.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(cmd, &offPass, VK_SUBPASS_CONTENTS_INLINE);

    // The view and projection come from the caller (built by the scene's
    // CameraSystem). We apply two Vulkan-specific fix-ups here so the
    // scene side can stay Vulkan-agnostic:
    //   - Y-flip [1][1] of the projection: Vulkan clip space has Y down.
    //   - Vertex jitter in the shader assumes the same low-res grid we
    //     render into; the cube renderer pushes lowResSize via the same
    //     push-constant block.
    glm::mat4 proj = camera.proj;
    proj[1][1] *= -1.0f;

    // Cube model: spinning landmark placed inside the map so the player
    // sees something moving down the corridor as soon as they spawn.
    // World coords picked to land in cell (1, 8) of the loaded map
    // (cellSize=2 -> centre at x=3, z=17). Y = 1.2 (raised landing floor)
    // + 0.5 (half cube height) so the cube rests on the top step. The
    // staircase in Application.cpp must keep cell (1, 8) at this height
    // for the cube to read correctly.
    const glm::vec3 kCubeWorldPos(3.0f, 1.7f, 17.0f);
    glm::mat4 model = glm::translate(glm::mat4(1.0f), kCubeWorldPos) *
                      glm::rotate(glm::mat4(1.0f),
                                  m_cubeSpinSeconds * 0.6f,
                                  glm::vec3(0.3f, 1.0f, 0.2f));

    const glm::mat4 viewProj = proj * camera.view;

    // Draw the static map first (huge background geometry). Depth test
    // will then early-out for occluded cube fragments behind walls.
    if (m_mapRenderer) {
        m_mapRenderer->record(cmd, viewProj, kLowResExtent, m_fog);
    }

    glm::mat4 mvp = viewProj * model;
    m_cubeRenderer->record(cmd, mvp, kLowResExtent, m_fog);

    // Sprites last in the geometry pass: alpha-tested, depth-tested, so
    // they correctly occlude / are occluded by walls and the cube.
    if (m_spriteRenderer && !m_sprites.empty()) {
        m_spriteRenderer->record(cmd, viewProj, camera.position,
                                 kLowResExtent, m_sprites, m_fog);
    }

    vkCmdEndRenderPass(cmd);

    // --- 2. Post-process pass: dither + palette quantization ---
    const PostTarget& post = m_postTargets[m_currentFrame];

    VkRenderPassBeginInfo postPass{};
    postPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    postPass.renderPass = m_postRenderPass;
    postPass.framebuffer = post.framebuffer;
    postPass.renderArea.offset = {0, 0};
    postPass.renderArea.extent = kLowResExtent;
    postPass.clearValueCount = 0;
    postPass.pClearValues = nullptr;
    vkCmdBeginRenderPass(cmd, &postPass, VK_SUBPASS_CONTENTS_INLINE);

    m_postProcessor->record(cmd, m_currentFrame, kLowResExtent);

    vkCmdEndRenderPass(cmd);

    // --- 3. Transition swapchain image UNDEFINED -> TRANSFER_DST ---
    VkImage swapImg = m_swapchainImages[imageIndex];
    {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = swapImg;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
    }

    // --- 4. Blit post-output -> swapchain with NEAREST filter ---
    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = 0;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {(int32_t)kLowResExtent.width, (int32_t)kLowResExtent.height, 1};

    blit.dstSubresource = blit.srcSubresource;
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {(int32_t)m_swapchainExtent.width,
                          (int32_t)m_swapchainExtent.height, 1};

    vkCmdBlitImage(cmd,
                   post.colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, VK_FILTER_NEAREST);

    // --- 5. Transition swapchain image TRANSFER_DST -> COLOR_ATTACHMENT ---
    {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = swapImg;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
    }

    // --- 6. Swapchain pass: ImGui draws on top of the blitted image ---
    VkRenderPassBeginInfo scPass{};
    scPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    scPass.renderPass = m_swapchainRenderPass;
    scPass.framebuffer = m_swapchainFramebuffers[imageIndex];
    scPass.renderArea.offset = {0, 0};
    scPass.renderArea.extent = m_swapchainExtent;
    scPass.clearValueCount = 0; // loadOp = LOAD
    scPass.pClearValues = nullptr;
    vkCmdBeginRenderPass(cmd, &scPass, VK_SUBPASS_CONTENTS_INLINE);

    m_imguiManager->render(cmd);

    vkCmdEndRenderPass(cmd);

    vkEndCommandBuffer(cmd);
}

// ---------------------------------------------------------------------------
// ImGui per-frame UI
// ---------------------------------------------------------------------------

void Renderer::runImGui(const CameraMatrices& camera) {
    m_imguiManager->newFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 380), ImGuiCond_FirstUseEver);

    ImGui::Begin("plaster");
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Offscreen: %ux%u (NEAREST blit)",
                kLowResExtent.width, kLowResExtent.height);
    ImGui::Text("Window:    %ux%u", m_swapchainExtent.width, m_swapchainExtent.height);

    ImGui::Separator();
    ImGui::TextUnformatted("Camera");
    ImGui::Text("Pos: %.2f, %.2f, %.2f",
                camera.position.x, camera.position.y, camera.position.z);
    ImGui::TextUnformatted("Capture: F1 toggle  /  Esc release");
    ImGui::TextUnformatted("Move: WASD  +  Space/LCtrl  +  Shift = sprint");

    ImGui::Separator();
    ImGui::TextUnformatted("Plastiboo post pass");

    // ---- Colour grade (runs first, shapes palette quantization) ----
    ImGui::TextUnformatted("Colour grade");

    float saturation = m_postProcessor->getSaturation();
    if (ImGui::SliderFloat("Saturation", &saturation, 0.0f, 2.5f, "%.2f")) {
        m_postProcessor->setSaturation(saturation);
    }

    float warmth = m_postProcessor->getWarmth();
    if (ImGui::SliderFloat("Warmth", &warmth, -1.0f, 1.0f, "%+0.2f")) {
        m_postProcessor->setWarmth(warmth);
    }

    if (ImGui::Button("Reset grade")) {
        m_postProcessor->resetGradeToDefaults();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Dither + palette");

    bool dither = m_postProcessor->getDitherEnabled();
    if (ImGui::Checkbox("Dither (Bayer 8x8)", &dither)) {
        m_postProcessor->setDitherEnabled(dither);
    }

    float strength = m_postProcessor->getDitherStrength();
    if (ImGui::SliderFloat("Strength", &strength, 0.0f, 0.4f, "%.3f")) {
        m_postProcessor->setDitherStrength(strength);
    }

    bool palette = m_postProcessor->getPaletteEnabled();
    if (ImGui::Checkbox("Palette quantize", &palette)) {
        m_postProcessor->setPaletteEnabled(palette);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Stylized fog");

    ImGui::Checkbox("Fog enable", &m_fog.enable);

    float fogColor[3] = {m_fog.color.r, m_fog.color.g, m_fog.color.b};
    if (ImGui::ColorEdit3("Fog colour", fogColor)) {
        m_fog.color = glm::vec3(fogColor[0], fogColor[1], fogColor[2]);
    }
    // Start/end in world units; map cellSize is 2.0 so the defaults
    // (6..28) cover roughly 3-14 cells from the camera.
    ImGui::SliderFloat("Fog start", &m_fog.start, 0.0f, 40.0f, "%.1f m");
    ImGui::SliderFloat("Fog end",   &m_fog.end,   1.0f, 80.0f, "%.1f m");
    if (m_fog.end <= m_fog.start) m_fog.end = m_fog.start + 0.5f;

    int bandsInt = static_cast<int>(m_fog.bands + 0.5f);
    if (ImGui::SliderInt("Fog bands", &bandsInt, 1, 12)) {
        m_fog.bands = static_cast<float>(bandsInt);
    }

    if (ImGui::Button("Reset fog")) {
        m_fog = FogParams{};
    }

    ImGui::Separator();
    const auto& presets = m_postProcessor->getPresets();
    int current = m_postProcessor->getPaletteIndex();
    const char* preview = (current >= 0 && current < (int)presets.size())
                              ? presets[current].name.c_str()
                              : "<none>";
    if (ImGui::BeginCombo("Palette", preview)) {
        for (int i = 0; i < (int)presets.size(); ++i) {
            const bool selected = (i == current);
            if (ImGui::Selectable(presets[i].name.c_str(), selected)) {
                m_postProcessor->setPaletteIndex(i);
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Map loading
// ---------------------------------------------------------------------------

void Renderer::loadMap(const GridMap& map) {
    // Tear down any previous map first; MapRenderer's dtor waits on the
    // device, so it's safe to release here even mid-game.
    m_mapRenderer.reset();
    m_mapRenderer = std::make_unique<MapRenderer>(
        m_vulkanContext, m_commandPool, m_offscreenRenderPass,
        kLowResExtent, map);
}

void Renderer::setSprites(std::vector<SpriteInstance> sprites) {
    m_sprites = std::move(sprites);
}

// ---------------------------------------------------------------------------
// Frame entry point
// ---------------------------------------------------------------------------

void Renderer::render(const CameraMatrices& camera, float dt) {
    VkDevice device = m_vulkanContext->getDevice();

    // Advance demo cube animation.
    m_cubeSpinSeconds += dt;

    vkWaitForFences(device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult acquire = vkAcquireNextImageKHR(
        device, m_swapchain, UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    } else if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("vkAcquireNextImageKHR failed");
    }

    // Only reset the fence once we know we are submitting work.
    vkResetFences(device, 1, &m_inFlightFences[m_currentFrame]);
    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);

    runImGui(camera);
    ImGui::Render();

    recordCommandBuffer(m_commandBuffers[m_currentFrame], imageIndex, camera);

    VkSemaphore         waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
    VkPipelineStageFlags waitStages[]    = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore         signalSemaphores[] = {m_renderFinishedSemaphores[m_currentFrame]};

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = waitSemaphores;
    submit.pWaitDstStageMask = waitStages;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &m_commandBuffers[m_currentFrame];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(m_vulkanContext->getGraphicsQueue(), 1, &submit,
                      m_inFlightFences[m_currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("vkQueueSubmit failed");
    }

    VkSwapchainKHR swapchains[] = {m_swapchain};
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = signalSemaphores;
    present.swapchainCount = 1;
    present.pSwapchains = swapchains;
    present.pImageIndices = &imageIndex;

    VkResult presentResult = vkQueuePresentKHR(m_vulkanContext->getGraphicsQueue(), &present);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("vkQueuePresentKHR failed");
    }

    m_currentFrame = (m_currentFrame + 1) % kMaxFramesInFlight;
}

} // namespace plaster
