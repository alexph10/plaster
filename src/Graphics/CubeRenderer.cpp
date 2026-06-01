#include "Graphics/CubeRenderer.h"

#include "Core/FileSystem.h"
#include "Graphics/FogParams.h"
#include "Graphics/Vulkan/VulkanContext.h"
#include "Graphics/Vulkan/VulkanUtils.h"

#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace plaster {

namespace {

// Per-vertex layout: position, normal, uv, light (36 B).
// Light is a per-vertex baked sector-light multiplier (0..1). The cube
// itself has no sector context, so every vertex ships 1.0 - it stays
// fully lit regardless of the room it's standing in.
struct CubeVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    float     light;
};

// Push constants must match the layout declared in cube.vert / cube.frag.
struct PushConstants {
    glm::mat4 mvp;
    glm::vec4 lowResSize;     // xy = pixel size, zw = pad
    glm::vec4 fogColorEnable; // rgb = fog colour, a = enable flag
    glm::vec4 fogParams;      // x = start, y = end, z = bands
};

// 24 vertices: 4 per face so each face gets its own normal and unique UVs.
// 36 indices: 2 triangles per face.
//
// Faces are wound CCW when viewed from outside, matching VK_FRONT_FACE_COUNTER_CLOCKWISE
// in the pipeline.
constexpr std::array<CubeVertex, 24> kCubeVertices = {{
    // +X face (right)
    {{ 0.5f, -0.5f, -0.5f}, { 1, 0, 0}, {0.0f, 1.0f}, 1.0f},
    {{ 0.5f, -0.5f,  0.5f}, { 1, 0, 0}, {1.0f, 1.0f}, 1.0f},
    {{ 0.5f,  0.5f,  0.5f}, { 1, 0, 0}, {1.0f, 0.0f}, 1.0f},
    {{ 0.5f,  0.5f, -0.5f}, { 1, 0, 0}, {0.0f, 0.0f}, 1.0f},
    // -X face (left)
    {{-0.5f, -0.5f,  0.5f}, {-1, 0, 0}, {0.0f, 1.0f}, 1.0f},
    {{-0.5f, -0.5f, -0.5f}, {-1, 0, 0}, {1.0f, 1.0f}, 1.0f},
    {{-0.5f,  0.5f, -0.5f}, {-1, 0, 0}, {1.0f, 0.0f}, 1.0f},
    {{-0.5f,  0.5f,  0.5f}, {-1, 0, 0}, {0.0f, 0.0f}, 1.0f},
    // +Y face (top)
    {{-0.5f,  0.5f, -0.5f}, { 0, 1, 0}, {0.0f, 1.0f}, 1.0f},
    {{ 0.5f,  0.5f, -0.5f}, { 0, 1, 0}, {1.0f, 1.0f}, 1.0f},
    {{ 0.5f,  0.5f,  0.5f}, { 0, 1, 0}, {1.0f, 0.0f}, 1.0f},
    {{-0.5f,  0.5f,  0.5f}, { 0, 1, 0}, {0.0f, 0.0f}, 1.0f},
    // -Y face (bottom)
    {{-0.5f, -0.5f,  0.5f}, { 0,-1, 0}, {0.0f, 1.0f}, 1.0f},
    {{ 0.5f, -0.5f,  0.5f}, { 0,-1, 0}, {1.0f, 1.0f}, 1.0f},
    {{ 0.5f, -0.5f, -0.5f}, { 0,-1, 0}, {1.0f, 0.0f}, 1.0f},
    {{-0.5f, -0.5f, -0.5f}, { 0,-1, 0}, {0.0f, 0.0f}, 1.0f},
    // +Z face (back, in right-handed -Z forward)
    {{ 0.5f, -0.5f,  0.5f}, { 0, 0, 1}, {0.0f, 1.0f}, 1.0f},
    {{-0.5f, -0.5f,  0.5f}, { 0, 0, 1}, {1.0f, 1.0f}, 1.0f},
    {{-0.5f,  0.5f,  0.5f}, { 0, 0, 1}, {1.0f, 0.0f}, 1.0f},
    {{ 0.5f,  0.5f,  0.5f}, { 0, 0, 1}, {0.0f, 0.0f}, 1.0f},
    // -Z face (front)
    {{-0.5f, -0.5f, -0.5f}, { 0, 0,-1}, {0.0f, 1.0f}, 1.0f},
    {{ 0.5f, -0.5f, -0.5f}, { 0, 0,-1}, {1.0f, 1.0f}, 1.0f},
    {{ 0.5f,  0.5f, -0.5f}, { 0, 0,-1}, {1.0f, 0.0f}, 1.0f},
    {{-0.5f,  0.5f, -0.5f}, { 0, 0,-1}, {0.0f, 0.0f}, 1.0f},
}};

constexpr std::array<uint16_t, 36> kCubeIndices = {{
     0,  1,  2,   0,  2,  3,   // +X
     4,  5,  6,   4,  6,  7,   // -X
     8,  9, 10,   8, 10, 11,   // +Y
    12, 13, 14,  12, 14, 15,   // -Y
    16, 17, 18,  16, 18, 19,   // +Z
    20, 21, 22,  20, 22, 23,   // -Z
}};

// Upload a host-side blob into a device-local buffer via a staging buffer.
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

// Generate an 8x8 "bone & stone" checker. Two muted tones chosen to feel
// in-style with the Plastiboo palette before any palette/dither pass exists.
std::vector<uint8_t> generateCheckerTexture(uint32_t& outWidth,
                                            uint32_t& outHeight) {
    constexpr uint32_t kSize = 8;
    outWidth = kSize;
    outHeight = kSize;

    std::vector<uint8_t> pixels(kSize * kSize * 4);
    const uint8_t bone[4]  = {220, 210, 190, 255};
    const uint8_t stone[4] = { 60,  55,  50, 255};

    for (uint32_t y = 0; y < kSize; ++y) {
        for (uint32_t x = 0; x < kSize; ++x) {
            const uint8_t* c = ((x ^ y) & 1) ? bone : stone;
            const size_t i = (y * kSize + x) * 4;
            pixels[i + 0] = c[0];
            pixels[i + 1] = c[1];
            pixels[i + 2] = c[2];
            pixels[i + 3] = c[3];
        }
    }
    return pixels;
}

} // namespace

CubeRenderer::CubeRenderer(VulkanContext* ctx,
                           VkCommandPool transferPool,
                           VkRenderPass offscreenRenderPass,
                           VkExtent2D offscreenExtent)
    : m_ctx(ctx) {
    createGeometry(transferPool);
    createTexture(transferPool);
    createDescriptorResources();
    createPipeline(offscreenRenderPass, offscreenExtent);
}

CubeRenderer::~CubeRenderer() {
    VkDevice device = m_ctx->getDevice();
    vkDeviceWaitIdle(device);

    if (m_pipeline)              vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipelineLayout)        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    if (m_descriptorPool)        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    if (m_descriptorSetLayout)   vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);

    if (m_textureSampler)        vkDestroySampler(device, m_textureSampler, nullptr);
    if (m_textureImageView)      vkDestroyImageView(device, m_textureImageView, nullptr);
    if (m_textureImage)          vkDestroyImage(device, m_textureImage, nullptr);
    if (m_textureImageMemory)    vkFreeMemory(device, m_textureImageMemory, nullptr);

    if (m_indexBuffer)           vkDestroyBuffer(device, m_indexBuffer, nullptr);
    if (m_indexBufferMemory)     vkFreeMemory(device, m_indexBufferMemory, nullptr);
    if (m_vertexBuffer)          vkDestroyBuffer(device, m_vertexBuffer, nullptr);
    if (m_vertexBufferMemory)    vkFreeMemory(device, m_vertexBufferMemory, nullptr);
}

void CubeRenderer::createGeometry(VkCommandPool transferPool) {
    uploadToDeviceLocalBuffer(m_ctx, transferPool,
                              kCubeVertices.data(),
                              sizeof(kCubeVertices),
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              m_vertexBuffer, m_vertexBufferMemory);

    uploadToDeviceLocalBuffer(m_ctx, transferPool,
                              kCubeIndices.data(),
                              sizeof(kCubeIndices),
                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                              m_indexBuffer, m_indexBufferMemory);

    m_indexCount = static_cast<uint32_t>(kCubeIndices.size());
}

void CubeRenderer::createTexture(VkCommandPool transferPool) {
    VkDevice device = m_ctx->getDevice();

    uint32_t width = 0, height = 0;
    std::vector<uint8_t> pixels = generateCheckerTexture(width, height);
    const VkDeviceSize size = pixels.size();

    // Stage the pixel data in a host-visible buffer.
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

    // Create the GPU image.
    vk_utils::createImage2D(m_ctx, width, height,
                            VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            m_textureImage, m_textureImageMemory);

    // Upload + final layout transition.
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

    // Nearest filter + repeat: the PS1/Plastiboo look depends on chunky
    // unfiltered texels. No mipmaps yet (the offscreen is so small they
    // would add nothing).
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
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_textureSampler) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSampler failed");
    }
}

void CubeRenderer::createDescriptorResources() {
    VkDevice device = m_ctx->getDevice();

    // Set layout: one combined-image-sampler at binding 0, frag-stage only.
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                    &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorSetLayout failed");
    }

    // Pool sized for exactly one set (just the cube). Future systems will
    // own their own pools so we never collide.
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr,
                               &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorPool failed");
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;
    if (vkAllocateDescriptorSets(device, &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateDescriptorSets failed");
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

void CubeRenderer::createPipeline(VkRenderPass renderPass, VkExtent2D extent) {
    VkDevice device = m_ctx->getDevice();

    const std::filesystem::path shadersDir = GetExecutableDirectory() / "shaders";
    VkShaderModule vert = vk_utils::loadShaderModule(device, shadersDir / "cube.vert.spv");
    VkShaderModule frag = vk_utils::loadShaderModule(device, shadersDir / "cube.frag.spv");

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vert;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = frag;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

    // Vertex input: one binding, three attributes.
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(CubeVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 4> attrs{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(CubeVertex, pos);
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(CubeVertex, normal);
    attrs[2].location = 2;
    attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset = offsetof(CubeVertex, uv);
    attrs[3].location = 3;
    attrs[3].binding = 0;
    attrs[3].format = VK_FORMAT_R32_SFLOAT;
    attrs[3].offset = offsetof(CubeVertex, light);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vertexInput.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Dynamic viewport + scissor: lets us reuse the pipeline if we ever
    // change the offscreen size without rebuilding.
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.depthClampEnable = VK_FALSE;
    raster.rasterizerDiscardEnable = VK_FALSE;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    raster.cullMode = VK_CULL_MODE_BACK_BIT;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.sampleShadingEnable = VK_FALSE;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlend{};
    colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlend.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlendState{};
    colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendState.attachmentCount = 1;
    colorBlendState.pAttachments = &colorBlend;

    // Push constant range covers the whole struct, visible to both stages.
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
        throw std::runtime_error("vkCreatePipelineLayout failed");
    }

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                      VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlendState;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                  nullptr, &m_pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        throw std::runtime_error("vkCreateGraphicsPipelines failed");
    }

    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
}

void CubeRenderer::record(VkCommandBuffer cmd,
                          const glm::mat4& mvp,
                          VkExtent2D offscreenExtent,
                          const FogParams& fog) {
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(offscreenExtent.width);
    viewport.height = static_cast<float>(offscreenExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = offscreenExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    PushConstants pc{};
    pc.mvp = mvp;
    pc.lowResSize = glm::vec4(static_cast<float>(offscreenExtent.width),
                              static_cast<float>(offscreenExtent.height),
                              0.0f, 0.0f);
    pc.fogColorEnable = glm::vec4(fog.color, fog.enable ? 1.0f : 0.0f);
    pc.fogParams      = glm::vec4(fog.start, fog.end, fog.bands, 0.0f);
    vkCmdPushConstants(cmd, m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);

    VkBuffer vbs[] = {m_vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vbs, offsets);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT16);

    vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);
}

} // namespace plaster
