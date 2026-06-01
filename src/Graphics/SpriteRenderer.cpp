#include "Graphics/SpriteRenderer.h"

#include "Core/FileSystem.h"
#include "Graphics/FogParams.h"
#include "Graphics/Vulkan/VulkanContext.h"
#include "Graphics/Vulkan/VulkanUtils.h"

#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace plaster {

namespace {

// Vertex layout matches sprite.vert: (vec2 local, vec2 uv).
struct SpriteVertex {
    glm::vec2 local;
    glm::vec2 uv;
};

// Push-constant layout must match sprite.vert / sprite.frag exactly.
// Fields are vec4-packed to fit inside Vulkan's guaranteed 128 bytes of
// push-constant space: 64 (mat4) + 4*16 (vec4) = 128 bytes.
//
//   cameraPosLight:     xyz = camera world pos,   w = sprite light (0..1)
//   worldCenterFogStart:xyz = sprite feet,        w = fog start distance
//   sizeFog:            xy  = sprite (w, h),      z = fog end, w = bands
//   fogColorEnable:     rgb = fog colour,         a = enable (0/1)
struct SpritePush {
    glm::mat4 viewProj;
    glm::vec4 cameraPosLight;
    glm::vec4 worldCenterFogStart;
    glm::vec4 sizeFog;
    glm::vec4 fogColorEnable;
};

// 4 corners of the sprite quad.
//   Local X in [-0.5, 0.5] (left -> right)
//   Local Y in [ 0.0, 1.0] (feet -> head)
//   UV.y inverted vs local Y so the texture's row 0 ends up at the top.
constexpr std::array<SpriteVertex, 4> kQuadVertices = {{
    {{-0.5f, 0.0f}, {0.0f, 1.0f}}, // BL (feet, left)
    {{ 0.5f, 0.0f}, {1.0f, 1.0f}}, // BR (feet, right)
    {{ 0.5f, 1.0f}, {1.0f, 0.0f}}, // TR (head, right)
    {{-0.5f, 1.0f}, {0.0f, 0.0f}}, // TL (head, left)
}};

// Winding: BL -> BR -> TR is CCW from the camera's POV after the
// billboard transform, matching VK_FRONT_FACE_COUNTER_CLOCKWISE. We
// still disable back-face culling in the pipeline as a safety net for
// edge cases (e.g. camera directly above the sprite).
constexpr std::array<uint16_t, 6> kQuadIndices = {{0, 1, 2, 0, 2, 3}};

// Procedural silhouette: small hooded figure 
//
// 32 wide x 64 tall RGBA. The shape is built from two analytic regions:
//   - Top ~35%: hood half-ellipse, biased so the front opening sits
//     darker than the cloak edges.
//   - Bottom ~65%: trapezoidal robe widening toward the feet.
// A cheap hash on (x, y) modulates the value so the silhouette has the
// same "stone fleck" noise as the wall texture, keeping it in style.
uint32_t hash2(uint32_t x, uint32_t y) {
    uint32_t h = x * 374761393u + y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

std::vector<uint8_t> generateHoodedFigure(uint32_t& outW, uint32_t& outH) {
    constexpr uint32_t W = 32;
    constexpr uint32_t H = 64;
    outW = W;
    outH = H;
    std::vector<uint8_t> px(W * H * 4, 0);

    constexpr float kHoodSplit = 0.35f; // fraction of height occupied by hood

    for (uint32_t py = 0; py < H; ++py) {
        const float v = (py + 0.5f) / static_cast<float>(H); // 0 = top
        for (uint32_t px_ = 0; px_ < W; ++px_) {
            const float u = (px_ + 0.5f) / static_cast<float>(W) - 0.5f;

            bool inside = false;
            float shade = 0.0f; // 0 = darkest, 1 = lightest among silhouette

            if (v < kHoodSplit) {
                // Hood: half-ellipse, narrow at top, slightly wider at the
                // shoulders. Centre vertically lined up with kHoodSplit.
                const float t  = v / kHoodSplit;        // 0 at top, 1 at shoulders
                const float rx = 0.18f + 0.14f * t;
                const float ry = kHoodSplit;
                const float du = u / rx;
                const float dv = (v - kHoodSplit) / ry;
                inside = (du * du + dv * dv) < 1.0f;
                // Centre of hood opening is darker (face in shadow).
                const float faceOpening =
                    (t > 0.55f && std::fabs(u) < 0.08f) ? 0.0f : 0.6f;
                shade = faceOpening;
            } else {
                // Robe: vertical trapezoid widening downward.
                const float t = (v - kHoodSplit) / (1.0f - kHoodSplit);
                const float halfW = 0.18f + 0.16f * t;
                inside = std::fabs(u) < halfW;
                // A faint vertical fold down the middle reads as cloth.
                const bool fold = std::fabs(u) < 0.02f;
                shade = fold ? 0.4f : 0.7f;
            }

            if (!inside) continue;

            const uint32_t h = hash2(px_, py);
            const int noise = static_cast<int>(h & 0x1F) - 16; // -16..15
            int grey = static_cast<int>(20.0f + 55.0f * shade) + noise;
            if (grey < 0)   grey = 0;
            if (grey > 255) grey = 255;

            const size_t i = (py * W + px_) * 4;
            px[i + 0] = static_cast<uint8_t>(grey);
            px[i + 1] = static_cast<uint8_t>(grey);
            px[i + 2] = static_cast<uint8_t>(grey);
            px[i + 3] = 255;
        }
    }
    return px;
}

// Identical staging-buffer upload helper used by CubeRenderer / MapRenderer.
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



SpriteRenderer::SpriteRenderer(VulkanContext* ctx,
                               VkCommandPool transferPool,
                               VkRenderPass offscreenRenderPass,
                               VkExtent2D offscreenExtent)
    : m_ctx(ctx) {
    createQuadGeometry(transferPool);
    createTexture(transferPool);
    createDescriptorResources();
    createPipeline(offscreenRenderPass, offscreenExtent);
}

SpriteRenderer::~SpriteRenderer() {
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

void SpriteRenderer::createQuadGeometry(VkCommandPool transferPool) {
    uploadToDeviceLocalBuffer(m_ctx, transferPool,
                              kQuadVertices.data(),
                              sizeof(kQuadVertices),
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              m_vertexBuffer, m_vertexBufferMemory);
    uploadToDeviceLocalBuffer(m_ctx, transferPool,
                              kQuadIndices.data(),
                              sizeof(kQuadIndices),
                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                              m_indexBuffer, m_indexBufferMemory);
}

void SpriteRenderer::createTexture(VkCommandPool transferPool) {
    VkDevice device = m_ctx->getDevice();

    uint32_t width = 0, height = 0;
    std::vector<uint8_t> pixels = generateHoodedFigure(width, height);
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

    // CLAMP_TO_EDGE: the sprite quad uses UV in [0, 1] exactly, but using
    // clamp instead of repeat avoids the texel from the other side of
    // the silhouette bleeding into the cutout edge when the sampler
    // straddles a boundary (Y-axis billboards never tile a sprite).
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_textureSampler) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSampler (sprite) failed");
    }
}

void SpriteRenderer::createDescriptorResources() {
    VkDevice device = m_ctx->getDevice();

    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &b;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                    &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorSetLayout (sprite) failed");
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
        throw std::runtime_error("vkCreateDescriptorPool (sprite) failed");
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;
    if (vkAllocateDescriptorSets(device, &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateDescriptorSets (sprite) failed");
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = m_textureImageView;
    imageInfo.sampler = m_textureSampler;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void SpriteRenderer::createPipeline(VkRenderPass renderPass, VkExtent2D extent) {
    VkDevice device = m_ctx->getDevice();

    const std::filesystem::path shadersDir = GetExecutableDirectory() / "shaders";
    VkShaderModule vert = vk_utils::loadShaderModule(device, shadersDir / "sprite.vert.spv");
    VkShaderModule frag = vk_utils::loadShaderModule(device, shadersDir / "sprite.frag.spv");

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
    binding.stride = sizeof(SpriteVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attrs{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset = offsetof(SpriteVertex, local);
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[1].offset = offsetof(SpriteVertex, uv);

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
    // Sprites are double-sided by definition - cull NONE removes any
    // winding edge cases (e.g. camera directly above the sprite).
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Alpha-tested in the fragment shader (discard) -> depth-correct
    // without sorting, and sprites can occlude each other / world geom.
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
    pushRange.size = sizeof(SpritePush);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr,
                               &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("vkCreatePipelineLayout (sprite) failed");
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
        throw std::runtime_error("vkCreateGraphicsPipelines (sprite) failed");
    }

    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
}

void SpriteRenderer::record(VkCommandBuffer cmd,
                            const glm::mat4& viewProj,
                            const glm::vec3& cameraPos,
                            VkExtent2D extent,
                            const std::vector<SpriteInstance>& sprites,
                            const FogParams& fog) {
    if (sprites.empty()) return;

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

    VkBuffer vbs[]      = {m_vertexBuffer};
    VkDeviceSize offs[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vbs, offs);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT16);

    SpritePush push{};
    push.viewProj       = viewProj;
    push.fogColorEnable = glm::vec4(fog.color, fog.enable ? 1.0f : 0.0f);

    // One push + draw per sprite. Fine for a few dozen sprites; if/when
    // counts grow we'd move to instanced rendering with a per-instance
    // vertex buffer, but the call shape here stays the same.
    for (const auto& s : sprites) {
        push.cameraPosLight      = glm::vec4(cameraPos, s.light);
        push.worldCenterFogStart = glm::vec4(s.position, fog.start);
        push.sizeFog             = glm::vec4(s.size, fog.end, fog.bands);
        vkCmdPushConstants(cmd, m_pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
    }
}

} // namespace plaster
