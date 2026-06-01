#include "Graphics/MapRenderer.h"

#include "Core/FileSystem.h"
#include "Graphics/FogParams.h"
#include "Graphics/GridMap.h"
#include "Graphics/Vulkan/VulkanContext.h"
#include "Graphics/Vulkan/VulkanUtils.h"

#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace plaster {

namespace {

// Matches the layout used by cube.vert (locations 0..3).
struct MapVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    float     light;
};

// Push-constant layout matches cube.vert / cube.frag.
struct PushConstants {
    glm::mat4 mvp;
    glm::vec4 lowResSize;       // xy = pixel size, zw = pad
    glm::vec4 fogColorEnable;   // rgb = colour, a = enable
    glm::vec4 fogParams;        // x = start, y = end, z = bands
};

// ---- Mesh generation helpers ---------------------------------------------

// Append a planar quad to (vertices, indices) given the bottom-left world
// corner and two in-plane edge vectors. Triangle winding follows the right
// -hand rule on (u x v), so passing the correct normal here is enough to
// keep front-faces facing the player.
void appendQuad(std::vector<MapVertex>& vertices,
                std::vector<uint32_t>&  indices,
                const glm::vec3& origin,
                const glm::vec3& u,
                const glm::vec3& v,
                const glm::vec3& normal,
                const glm::vec2& uvScale,
                float            light) {
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    vertices.push_back({origin,         normal, {0.0f,      0.0f     }, light});
    vertices.push_back({origin + u,     normal, {uvScale.x, 0.0f     }, light});
    vertices.push_back({origin + u + v, normal, {uvScale.x, uvScale.y}, light});
    vertices.push_back({origin + v,     normal, {0.0f,      uvScale.y}, light});

    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
}

// Emit a vertical wall quad on one edge of the current empty cell,
// stretching from y=y0 to y=y1 (with y1 > y0). `origin`, `u`, `normal`
// describe the same in-plane axes as the full-wall case. The vertical
// extent is `u` cross `n`'s direction so we just pass it in via `up`.
//
// Helper exists so the wall / floor-step / ceiling-step cases share the
// same orientation logic.
void appendWallSlice(std::vector<MapVertex>& vertices,
                     std::vector<uint32_t>&  indices,
                     const glm::vec3& origin,   // bottom-left of slice
                     const glm::vec3& u,        // horizontal edge vector
                     const glm::vec3& normal,
                     float height,
                     float uLen,
                     float light) {
    if (height <= 0.0f) return;
    const glm::vec3 up(0.0f, height, 0.0f);
    appendQuad(vertices, indices, origin, u, up, normal,
               glm::vec2(uLen, height), light);
}

void generateMapMesh(const GridMap& map,
                     std::vector<MapVertex>& vertices,
                     std::vector<uint32_t>&  indices) {
    const float s = map.CellSize();

    // Each edge of an empty cell C produces up to three quads when the
    // neighbour N is empty:
    //   - If N.floor > C.floor: an upward-facing riser from C.floor up
    //     to N.floor (visible from inside C).
    //   - If N.ceiling < C.ceiling: a downward-facing soffit from
    //     N.ceiling up to C.ceiling.
    // If N is a wall (or OOB), the whole interval [C.floor, C.ceiling]
    // is a flat wall on this edge.
    auto emitEdge = [&](int cx, int cy,
                        int nx, int ny,
                        const glm::vec3& origin, // bottom-left at C.floor
                        const glm::vec3& u,      // horizontal edge vector
                        const glm::vec3& normal,
                        float uLen,
                        float light) {
        const float cFloor = map.FloorYAt(cx, cy);
        const float cCeil  = map.CeilingYAt(cx, cy);

        if (map.IsWall(nx, ny)) {
            // Full wall from this cell's floor to its ceiling.
            const float h = cCeil - cFloor;
            appendWallSlice(vertices, indices, origin, u, normal, h, uLen, light);
            return;
        }

        // Neighbour is empty - only the height difference shows.
        const float nFloor = map.FloorYAt(nx, ny);
        const float nCeil  = map.CeilingYAt(nx, ny);

        if (nFloor > cFloor) {
            // Riser: from cFloor to nFloor. Origin is already at cFloor.
            appendWallSlice(vertices, indices, origin, u, normal,
                            nFloor - cFloor, uLen, light);
        }
        if (nCeil < cCeil) {
            // Soffit: from nCeil up to cCeil.
            const glm::vec3 soffitOrigin(origin.x, nCeil, origin.z);
            appendWallSlice(vertices, indices, soffitOrigin, u, normal,
                            cCeil - nCeil, uLen, light);
        }
    };

    for (int cy = 0; cy < map.Height(); ++cy) {
        for (int cx = 0; cx < map.Width(); ++cx) {
            if (!map.IsEmpty(cx, cy)) continue;

            const float x0 = cx       * s;
            const float x1 = (cx + 1) * s;
            const float z0 = cy       * s;
            const float z1 = (cy + 1) * s;
            const float fh = map.FloorYAt(cx, cy);
            const float ch = map.CeilingYAt(cx, cy);
            // Every quad emitted for this cell takes the cell's light.
            // Walls between cells of different brightness already get
            // emitted twice (once per side, facing inward) so each face
            // takes the brightness of the room it's visible from.
            const float L  = map.LightLevelAt(cx, cy);

            // Floor: normal +Y.
            appendQuad(vertices, indices,
                       glm::vec3(x0, fh, z1),
                       glm::vec3(s, 0, 0),
                       glm::vec3(0, 0, -s),
                       glm::vec3(0, 1, 0),
                       glm::vec2(s, s), L);

            // Ceiling: normal -Y.
            appendQuad(vertices, indices,
                       glm::vec3(x0, ch, z0),
                       glm::vec3(s, 0, 0),
                       glm::vec3(0, 0, s),
                       glm::vec3(0, -1, 0),
                       glm::vec2(s, s), L);

            // North edge (z = z0).
            emitEdge(cx, cy, cx, cy - 1,
                     glm::vec3(x0, fh, z0),
                     glm::vec3(s, 0, 0),
                     glm::vec3(0, 0, 1),
                     s, L);
            // South edge (z = z1).
            emitEdge(cx, cy, cx, cy + 1,
                     glm::vec3(x1, fh, z1),
                     glm::vec3(-s, 0, 0),
                     glm::vec3(0, 0, -1),
                     s, L);
            // East edge (x = x1).
            emitEdge(cx, cy, cx + 1, cy,
                     glm::vec3(x1, fh, z0),
                     glm::vec3(0, 0, s),
                     glm::vec3(-1, 0, 0),
                     s, L);
            // West edge (x = x0).
            emitEdge(cx, cy, cx - 1, cy,
                     glm::vec3(x0, fh, z1),
                     glm::vec3(0, 0, -s),
                     glm::vec3(1, 0, 0),
                     s, L);
        }
    }
}

// ---- Procedural stone texture --------------------------------------------

// Cheap integer hash, fine for visual noise.
uint32_t hash2(uint32_t x, uint32_t y) {
    uint32_t h = x * 374761393u + y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

std::vector<uint8_t> generateStoneTexture(uint32_t& outW, uint32_t& outH) {
    constexpr uint32_t kSize = 32;
    outW = kSize;
    outH = kSize;
    std::vector<uint8_t> px(kSize * kSize * 4);

    constexpr uint32_t brickH  = 8;
    constexpr uint32_t brickW  = 16;
    constexpr uint32_t mortarT = 1;

    for (uint32_t y = 0; y < kSize; ++y) {
        const uint32_t brickRow = y / brickH;
        // Alternating half-brick offset gives proper running-bond brickwork.
        const uint32_t offset   = (brickRow & 1u) * (brickW / 2);

        for (uint32_t x = 0; x < kSize; ++x) {
            const uint32_t localY = y % brickH;
            const uint32_t shiftedX = (x + offset) % brickW;

            const bool isHorizontalMortar = (localY < mortarT);
            const bool isVerticalMortar   = (shiftedX < mortarT);
            const bool isMortar = isHorizontalMortar || isVerticalMortar;

            const uint32_t h = hash2(x, y);

            uint8_t grey;
            if (isMortar) {
                grey = static_cast<uint8_t>(25 + (h % 18));   // dark joints
            } else {
                grey = static_cast<uint8_t>(70 + (h % 55));   // stone face
            }

            const size_t i = (y * kSize + x) * 4;
            px[i + 0] = grey;
            px[i + 1] = grey;
            px[i + 2] = grey;
            px[i + 3] = 255;
        }
    }
    return px;
}

// ---- Upload helper (same shape as CubeRenderer's) -------------------------

void uploadToDeviceLocalBuffer(VulkanContext* ctx,
                               VkCommandPool transferPool,
                               const void* data,
                               VkDeviceSize size,
                               VkBufferUsageFlags usage,
                               VkBuffer& outBuffer,
                               VkDeviceMemory& outMemory) {
    VkDevice device = ctx->getDevice();

    VkBuffer       staging       = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    vk_utils::createBuffer(ctx, size,
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           staging, stagingMemory);

    void* mapped = nullptr;
    vkMapMemory(device, stagingMemory, 0, size, 0, &mapped);
    std::memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(device, stagingMemory);

    vk_utils::createBuffer(ctx, size,
                           VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           outBuffer, outMemory);

    VkCommandBuffer cmd = vk_utils::beginSingleTimeCommands(device, transferPool);
    VkBufferCopy copy{0, 0, size};
    vkCmdCopyBuffer(cmd, staging, outBuffer, 1, &copy);
    vk_utils::endSingleTimeCommands(device, transferPool, ctx->getGraphicsQueue(), cmd);

    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
}

} // namespace

// ===========================================================================

MapRenderer::MapRenderer(VulkanContext* ctx,
                         VkCommandPool transferPool,
                         VkRenderPass offscreenRenderPass,
                         VkExtent2D offscreenExtent,
                         const GridMap& map)
    : m_ctx(ctx) {
    createGeometry(transferPool, map);
    createTexture(transferPool);
    createDescriptorResources();
    createPipeline(offscreenRenderPass, offscreenExtent);
}

MapRenderer::~MapRenderer() {
    VkDevice device = m_ctx->getDevice();
    vkDeviceWaitIdle(device);

    if (m_pipeline)            vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipelineLayout)      vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    if (m_descriptorPool)      vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    if (m_descriptorSetLayout) vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);

    if (m_textureSampler)      vkDestroySampler(device, m_textureSampler, nullptr);
    if (m_textureImageView)    vkDestroyImageView(device, m_textureImageView, nullptr);
    if (m_textureImage)        vkDestroyImage(device, m_textureImage, nullptr);
    if (m_textureImageMemory)  vkFreeMemory(device, m_textureImageMemory, nullptr);

    if (m_indexBuffer)         vkDestroyBuffer(device, m_indexBuffer, nullptr);
    if (m_indexBufferMemory)   vkFreeMemory(device, m_indexBufferMemory, nullptr);
    if (m_vertexBuffer)        vkDestroyBuffer(device, m_vertexBuffer, nullptr);
    if (m_vertexBufferMemory)  vkFreeMemory(device, m_vertexBufferMemory, nullptr);
}

void MapRenderer::createGeometry(VkCommandPool transferPool, const GridMap& map) {
    std::vector<MapVertex> vertices;
    std::vector<uint32_t>  indices;
    vertices.reserve(static_cast<size_t>(map.Width()) * map.Height() * 8);
    indices .reserve(static_cast<size_t>(map.Width()) * map.Height() * 12);

    generateMapMesh(map, vertices, indices);
    m_indexCount = static_cast<uint32_t>(indices.size());

    if (m_indexCount == 0) {
        throw std::runtime_error("MapRenderer: generated mesh has zero indices");
    }

    uploadToDeviceLocalBuffer(m_ctx, transferPool,
                              vertices.data(),
                              vertices.size() * sizeof(MapVertex),
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              m_vertexBuffer, m_vertexBufferMemory);

    uploadToDeviceLocalBuffer(m_ctx, transferPool,
                              indices.data(),
                              indices.size() * sizeof(uint32_t),
                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                              m_indexBuffer, m_indexBufferMemory);
}

void MapRenderer::createTexture(VkCommandPool transferPool) {
    VkDevice device = m_ctx->getDevice();

    uint32_t width = 0, height = 0;
    std::vector<uint8_t> pixels = generateStoneTexture(width, height);
    const VkDeviceSize size = pixels.size();

    VkBuffer       staging       = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    vk_utils::createBuffer(m_ctx, size,
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           staging, stagingMemory);

    void* mapped = nullptr;
    vkMapMemory(device, stagingMemory, 0, size, 0, &mapped);
    std::memcpy(mapped, pixels.data(), static_cast<size_t>(size));
    vkUnmapMemory(device, stagingMemory);

    vk_utils::createImage2D(m_ctx, width, height,
                            VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            m_textureImage, m_textureImageMemory);

    VkCommandBuffer cmd = vk_utils::beginSingleTimeCommands(device, transferPool);
    vk_utils::transitionImageLayout(cmd, m_textureImage,
                                    VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vk_utils::copyBufferToImage(cmd, staging, m_textureImage, width, height);
    vk_utils::transitionImageLayout(cmd, m_textureImage,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vk_utils::endSingleTimeCommands(device, transferPool, m_ctx->getGraphicsQueue(), cmd);

    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    m_textureImageView = vk_utils::createImageView2D(device, m_textureImage,
                                                     VK_FORMAT_R8G8B8A8_UNORM,
                                                     VK_IMAGE_ASPECT_COLOR_BIT);

    // NEAREST + REPEAT: same reasoning as CubeRenderer - the Plastiboo look
    // depends on unfiltered texels, and our UVs are scaled to tile across
    // cell sizes so REPEAT is the natural address mode.
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_textureSampler) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSampler (map) failed");
    }
}

void MapRenderer::createDescriptorResources() {
    VkDevice device = m_ctx->getDevice();

    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                    &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorSetLayout (map) failed");
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorPool (map) failed");
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;
    if (vkAllocateDescriptorSets(device, &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateDescriptorSets (map) failed");
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = m_textureImageView;
    imageInfo.sampler = m_textureSampler;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void MapRenderer::createPipeline(VkRenderPass renderPass, VkExtent2D extent) {
    VkDevice device = m_ctx->getDevice();

    const std::filesystem::path shadersDir = GetExecutableDirectory() / "shaders";
    VkShaderModule vert = vk_utils::loadShaderModule(device, shadersDir / "cube.vert.spv");
    VkShaderModule frag = vk_utils::loadShaderModule(device, shadersDir / "cube.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(MapVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 4> attrs{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(MapVertex, pos);
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(MapVertex, normal);
    attrs[2].location = 2;
    attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset = offsetof(MapVertex, uv);
    attrs[3].location = 3;
    attrs[3].binding = 0;
    attrs[3].format = VK_FORMAT_R32_SFLOAT;
    attrs[3].offset = offsetof(MapVertex, light);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vertexInput.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_BACK_BIT;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo blendState{};
    blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendState.attachmentCount = 1;
    blendState.pAttachments = &blend;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr,
                               &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("vkCreatePipelineLayout (map) failed");
    }

    VkDynamicState dyns[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{};
    dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dyns;

    VkGraphicsPipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeInfo.stageCount = 2;
    pipeInfo.pStages = stages;
    pipeInfo.pVertexInputState = &vertexInput;
    pipeInfo.pInputAssemblyState = &inputAssembly;
    pipeInfo.pViewportState = &viewportState;
    pipeInfo.pRasterizationState = &raster;
    pipeInfo.pMultisampleState = &multisample;
    pipeInfo.pDepthStencilState = &depthStencil;
    pipeInfo.pColorBlendState = &blendState;
    pipeInfo.pDynamicState = &dynState;
    pipeInfo.layout = m_pipelineLayout;
    pipeInfo.renderPass = renderPass;
    pipeInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeInfo,
                                  nullptr, &m_pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        throw std::runtime_error("vkCreateGraphicsPipelines (map) failed");
    }

    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
}

void MapRenderer::record(VkCommandBuffer cmd, const glm::mat4& viewProj,
                         VkExtent2D extent, const FogParams& fog) {
    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    // Map geometry lives in world space; model matrix is implicit identity
    // so mvp = viewProj.
    PushConstants pc{};
    pc.mvp = viewProj;
    pc.lowResSize = glm::vec4(static_cast<float>(extent.width),
                              static_cast<float>(extent.height), 0.0f, 0.0f);
    pc.fogColorEnable = glm::vec4(fog.color, fog.enable ? 1.0f : 0.0f);
    pc.fogParams      = glm::vec4(fog.start, fog.end, fog.bands, 0.0f);
    vkCmdPushConstants(cmd, m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);

    VkBuffer vbs[]      = {m_vertexBuffer};
    VkDeviceSize offs[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vbs, offs);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);
}

} // namespace plaster
