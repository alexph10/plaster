#pragma once

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include <cstdint>

namespace plaster {

class VulkanContext;
struct FogParams;

// CubeRenderer owns everything needed to draw a single textured cube into
// the offscreen low-res target: vertex/index buffers, a procedural texture,
// the descriptor set wiring, and the graphics pipeline.
//
// Lifecycle:
//   - Construct once after the offscreen render pass exists.
//   - Call recreatePipeline() if the offscreen extent ever changes.
//   - Call record() inside an active offscreen render pass each frame.
//
// The cube is centred at the origin in model space; the caller supplies
// the full MVP matrix.
class CubeRenderer {
public:
    CubeRenderer(VulkanContext* ctx,
                 VkCommandPool transferPool,
                 VkRenderPass offscreenRenderPass,
                 VkExtent2D offscreenExtent);
    ~CubeRenderer();

    CubeRenderer(const CubeRenderer&) = delete;
    CubeRenderer& operator=(const CubeRenderer&) = delete;

    // Records vkCmdBindPipeline + draw inside an already-begun render pass.
    // The viewport / scissor are configured to fill `offscreenExtent`.
    // `fog` is the engine-wide stylized fog state; the cube uses the
    // same shader as the map so it gets identical depth-banding.
    void record(VkCommandBuffer cmd,
                const glm::mat4& mvp,
                VkExtent2D offscreenExtent,
                const FogParams& fog);

private:
    void createGeometry(VkCommandPool transferPool);
    void createTexture(VkCommandPool transferPool);
    void createDescriptorResources();
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent);

    VulkanContext* m_ctx;

    // Geometry (device-local).
    VkBuffer       m_vertexBuffer       = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer       m_indexBuffer        = VK_NULL_HANDLE;
    VkDeviceMemory m_indexBufferMemory  = VK_NULL_HANDLE;
    uint32_t       m_indexCount         = 0;

    // Texture.
    VkImage        m_textureImage       = VK_NULL_HANDLE;
    VkDeviceMemory m_textureImageMemory = VK_NULL_HANDLE;
    VkImageView    m_textureImageView   = VK_NULL_HANDLE;
    VkSampler      m_textureSampler     = VK_NULL_HANDLE;

    // Descriptors.
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
    VkDescriptorSet       m_descriptorSet       = VK_NULL_HANDLE;

    // Pipeline.
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline       = VK_NULL_HANDLE;
};

} // namespace plaster
