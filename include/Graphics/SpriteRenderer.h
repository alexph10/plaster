#pragma once

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace plaster {

class VulkanContext;
struct FogParams;

// One sprite instance in the world. Position is the *base* of the
// sprite (its feet) so placing one at the floor of a cell needs no
// vertical offset gymnastics. `light` is the sector light multiplier
// the sprite inherits from its cell (1.0 = full bright).
struct SpriteInstance {
    glm::vec3 position{0.0f};       // world-space base
    glm::vec2 size{1.0f, 2.0f};     // width, height in world units
    float     light{1.0f};          // 0..1, multiplied into the sprite colour
};

// SpriteRenderer draws a list of Y-axis billboarded sprites into the
// offscreen low-res target. All sprites currently share a single
// procedural texture (a hooded figure); future work can swap that for a
// sprite atlas + per-instance UV ranges or a per-sprite texture array.
//
// Lifecycle:
//   - Construct once after the offscreen render pass exists.
//   - Call record() inside an active offscreen render pass each frame
//     with the current sprite list and camera info.
class SpriteRenderer {
public:
    SpriteRenderer(VulkanContext* ctx,
                   VkCommandPool transferPool,
                   VkRenderPass offscreenRenderPass,
                   VkExtent2D offscreenExtent);
    ~SpriteRenderer();

    SpriteRenderer(const SpriteRenderer&) = delete;
    SpriteRenderer& operator=(const SpriteRenderer&) = delete;

    void record(VkCommandBuffer cmd,
                const glm::mat4& viewProj,
                const glm::vec3& cameraPos,
                VkExtent2D extent,
                const std::vector<SpriteInstance>& sprites,
                const FogParams& fog);

private:
    void createQuadGeometry(VkCommandPool transferPool);
    void createTexture(VkCommandPool transferPool);
    void createDescriptorResources();
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent);

    VulkanContext* m_ctx;

    // Unit quad (4 verts, 6 indices) shared by every sprite draw.
    VkBuffer       m_vertexBuffer       = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer       m_indexBuffer        = VK_NULL_HANDLE;
    VkDeviceMemory m_indexBufferMemory  = VK_NULL_HANDLE;

    // Procedural silhouette texture shared by every sprite.
    VkImage        m_textureImage       = VK_NULL_HANDLE;
    VkDeviceMemory m_textureImageMemory = VK_NULL_HANDLE;
    VkImageView    m_textureImageView   = VK_NULL_HANDLE;
    VkSampler      m_textureSampler     = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
    VkDescriptorSet       m_descriptorSet       = VK_NULL_HANDLE;

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline       = VK_NULL_HANDLE;
};

} // namespace plaster
