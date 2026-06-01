#pragma once

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include <cstdint>

namespace plaster {

class VulkanContext;
class GridMap;
struct FogParams;

// MapRenderer turns a GridMap into renderable world geometry.
//
// At construction it walks the grid, generates one interleaved vertex
// buffer (position / normal / UV) and one index buffer covering every
// floor, ceiling, and inward-facing wall quad, then builds a Vulkan
// graphics pipeline using the existing cube.vert / cube.frag shaders.
//
// The PS1 vertex jitter, affine-UV interpolation and 4-band lighting in
// those shaders apply uniformly to all world geometry, which is exactly
// what gives the dungeon a coherent Plastiboo look.
//
// Future direction: this class is shaped so that swapping the binary
// grid for richer level data (variable heights, sloped tiles, sector
// textures, etc.) only requires changing the mesh-generation step; the
// pipeline / descriptor / draw path stays identical.
class MapRenderer {
public:
    MapRenderer(VulkanContext* ctx,
                VkCommandPool transferPool,
                VkRenderPass offscreenRenderPass,
                VkExtent2D offscreenExtent,
                const GridMap& map);
    ~MapRenderer();

    MapRenderer(const MapRenderer&) = delete;
    MapRenderer& operator=(const MapRenderer&) = delete;

    // Record the draw inside an already-begun render pass.
    // `viewProj` is the camera's V*P matrix; the map is in world space so
    // no model matrix is required. `fog` is the engine-wide stylized fog
    // state, pushed every frame so changes are picked up immediately.
    void record(VkCommandBuffer cmd, const glm::mat4& viewProj,
                VkExtent2D extent, const FogParams& fog);

private:
    void createGeometry(VkCommandPool transferPool, const GridMap& map);
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
