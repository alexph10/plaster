#include "Graphics/PostProcessor.h"

#include "Core/FileSystem.h"
#include "Graphics/Vulkan/VulkanContext.h"
#include "Graphics/Vulkan/VulkanUtils.h"

#include <array>
#include <cstring>
#include <stdexcept>

namespace plaster {

namespace {

// Push-constant layout must match post_dither.frag.
struct PushConstants {
    float ditherStrength;
    int   paletteSize;
    int   enableDither;
    int   enablePalette;
    float saturation;
    float warmth;
};

// Convenience for hex literals: 0xAABBGGRR layout matching R8G8B8A8_UNORM
// little-endian byte order.
constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint32_t>(r) |
           (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(255) << 24);
}

} // namespace

PostProcessor::PostProcessor(VulkanContext* ctx,
                             VkCommandPool transferPool,
                             VkRenderPass postRenderPass,
                             const std::vector<VkImageView>& geometryColorViews,
                             VkExtent2D extent)
    : m_ctx(ctx), m_transferPool(transferPool) {
    createPalettePresets();
    createPaletteImage(transferPool);
    uploadPalette(m_currentPalette);

    createDescriptorResources(geometryColorViews);
    createPipeline(postRenderPass, extent);
}

PostProcessor::~PostProcessor() {
    VkDevice device = m_ctx->getDevice();
    vkDeviceWaitIdle(device);

    if (m_pipeline)              vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipelineLayout)        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    if (m_descriptorPool)        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    if (m_descriptorSetLayout)   vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);

    if (m_geometrySampler)       vkDestroySampler(device, m_geometrySampler, nullptr);

    if (m_paletteStagingBuffer)       vkDestroyBuffer(device, m_paletteStagingBuffer, nullptr);
    if (m_paletteStagingBufferMemory) vkFreeMemory(device, m_paletteStagingBufferMemory, nullptr);

    if (m_paletteSampler)        vkDestroySampler(device, m_paletteSampler, nullptr);
    if (m_paletteImageView)      vkDestroyImageView(device, m_paletteImageView, nullptr);
    if (m_paletteImage)          vkDestroyImage(device, m_paletteImage, nullptr);
    if (m_paletteImageMemory)    vkFreeMemory(device, m_paletteImageMemory, nullptr);
}

void PostProcessor::createPalettePresets() {
    // Warm/high-saturation palettes lead. Each preset is a 5-entry ramp
    // running dark -> light; the shader iterates exactly colorsRGBA8.size()
    // entries. Keep entries <= kMaxPaletteEntries (16).
    //
    // The first three presets are the engine's signature warm set:
    //   - Ember Hearth: candlelight orange dominant, deep ember shadows.
    //   - Sunken Gold:  burnished gold midtones; reads "tomb / reliquary".
    //   - Blood & Brass: heavy reds + brass highlights; aggressive mood.
    //
    // The remaining presets are the original cooler/desaturated looks,
    // kept for contrast and for the Souls-style "swap atmosphere only"
    // workflow described in plastiboo.md.
    m_presets = {
        
        {"Ember Hearth", {
            rgba( 18,   8,   6),
            rgba( 80,  28,  14),
            rgba(170,  70,  20),
            rgba(225, 140,  40),
            rgba(245, 210, 130),
        }},
        {"Sunken Gold", {
            rgba( 16,  10,   4),
            rgba( 70,  40,  10),
            rgba(150,  95,  20),
            rgba(210, 160,  55),
            rgba(245, 220, 145),
        }},
        {"Blood & Brass", {
            rgba( 14,   6,   8),
            rgba( 75,  18,  18),
            rgba(160,  40,  30),
            rgba(210, 120,  45),
            rgba(240, 200, 120),
        }},

       
        {"Bone & Pitch", {
            rgba( 12,  10,  14),
            rgba( 52,  48,  50),
            rgba(110, 100,  92),
            rgba(180, 170, 152),
            rgba(220, 210, 190),
        }},
        {"Plague", {
            rgba( 10,  12,   8),
            rgba( 30,  40,  22),
            rgba( 80, 110,  30),
            rgba(160, 180,  55),
            rgba(220, 220, 110),
        }},
        {"Cinder", {
            rgba( 10,   6,   6),
            rgba( 50,  18,  14),
            rgba(120,  40,  30),
            rgba(180,  80,  50),
            rgba(220, 160, 110),
        }},
        {"Bilious Forest", {
            rgba( 12,  14,  12),
            rgba( 35,  50,  35),
            rgba( 70,  90,  50),
            rgba(130, 130,  70),
            rgba(200, 190, 130),
        }},
        {"Reliquary", {
            rgba(  8,   6,   4),
            rgba( 40,  30,  16),
            rgba(110,  80,  35),
            rgba(180, 140,  70),
            rgba(230, 210, 160),
        }},
    };

   
    m_currentPalette = 0;
}

void PostProcessor::createPaletteImage(VkCommandPool transferPool) {
    VkDevice device = m_ctx->getDevice();
    const uint32_t width = kMaxPaletteEntries;

.
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_1D;
    imageInfo.extent = {width, 1, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    if (vkCreateImage(device, &imageInfo, nullptr, &m_paletteImage) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImage (palette) failed");
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, m_paletteImage, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = vk_utils::findMemoryType(
        m_ctx->getPhysicalDevice(),
        memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_paletteImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateMemory (palette) failed");
    }
    vkBindImageMemory(device, m_paletteImage, m_paletteImageMemory, 0);


    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_paletteImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_1D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &viewInfo, nullptr, &m_paletteImageView) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImageView (palette) failed");
    }

  
    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler.unnormalizedCoordinates = VK_FALSE;
    sampler.compareEnable = VK_FALSE;
    sampler.compareOp = VK_COMPARE_OP_ALWAYS;
    if (vkCreateSampler(device, &sampler, nullptr, &m_paletteSampler) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSampler (palette) failed");
    }

    
    vk_utils::createBuffer(m_ctx,
                           static_cast<VkDeviceSize>(kMaxPaletteEntries) * 4,
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           m_paletteStagingBuffer,
                           m_paletteStagingBufferMemory);

    
    VkSamplerCreateInfo gsampler = sampler;
    gsampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    gsampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &gsampler, nullptr, &m_geometrySampler) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSampler (geometry) failed");
    }

    (void)transferPool; 
}

void PostProcessor::uploadPalette(int presetIndex) {
    if (presetIndex < 0 || presetIndex >= static_cast<int>(m_presets.size())) {
        return;
    }
    VkDevice device = m_ctx->getDevice();
    const auto& preset = m_presets[presetIndex];
    const uint32_t count = static_cast<uint32_t>(preset.colorsRGBA8.size());
    if (count == 0 || count > kMaxPaletteEntries) {
        throw std::runtime_error("Palette preset has invalid entry count");
    }

    // Fill staging buffer; pad unused entries with the last colour so any
    // off-by-one in shader bounds still samples something legal.
    uint32_t packed[kMaxPaletteEntries];
    for (uint32_t i = 0; i < kMaxPaletteEntries; ++i) {
        packed[i] = (i < count) ? preset.colorsRGBA8[i]
                                : preset.colorsRGBA8[count - 1];
    }

    void* mapped = nullptr;
    vkMapMemory(device, m_paletteStagingBufferMemory, 0, sizeof(packed), 0, &mapped);
    std::memcpy(mapped, packed, sizeof(packed));
    vkUnmapMemory(device, m_paletteStagingBufferMemory);

    // Idle: palette swaps are user-driven and rare; the simplest correct
    // sync is "wait for everything", then upload, then continue.
    vkDeviceWaitIdle(device);

    VkCommandBuffer cmd = vk_utils::beginSingleTimeCommands(device, m_transferPool);

    // Transition palette image into TRANSFER_DST for the copy. We don't
    // know its current layout (UNDEFINED on first call, SHADER_READ_ONLY
    // on subsequent calls), so use a barrier that handles either.
    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = m_paletteImage;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toDst);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {kMaxPaletteEntries, 1, 1};
    vkCmdCopyBufferToImage(cmd, m_paletteStagingBuffer, m_paletteImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toShader{};
    toShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShader.image = m_paletteImage;
    toShader.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toShader);

    vk_utils::endSingleTimeCommands(device, m_transferPool,
                                    m_ctx->getGraphicsQueue(), cmd);
}

void PostProcessor::createDescriptorResources(
    const std::vector<VkImageView>& geometryColorViews) {
    VkDevice device = m_ctx->getDevice();

    // Two combined-image-sampler bindings: 0 = geometry color, 1 = palette.
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                    &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorSetLayout (post) failed");
    }

    const uint32_t setCount = static_cast<uint32_t>(geometryColorViews.size());

    // Two CIS bindings per set, setCount sets total.
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = setCount * 2;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = setCount;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorPool (post) failed");
    }

    std::vector<VkDescriptorSetLayout> layouts(setCount, m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = setCount;
    allocInfo.pSetLayouts = layouts.data();
    m_descriptorSets.resize(setCount);
    if (vkAllocateDescriptorSets(device, &allocInfo, m_descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateDescriptorSets (post) failed");
    }

    // Wire each set to its geometry image + the shared palette image.
    for (uint32_t i = 0; i < setCount; ++i) {
        VkDescriptorImageInfo colorInfo{};
        colorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        colorInfo.imageView = geometryColorViews[i];
        colorInfo.sampler = m_geometrySampler;

        VkDescriptorImageInfo paletteInfo{};
        paletteInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        paletteInfo.imageView = m_paletteImageView;
        paletteInfo.sampler = m_paletteSampler;

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_descriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &colorInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_descriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &paletteInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
}

void PostProcessor::createPipeline(VkRenderPass renderPass, VkExtent2D extent) {
    VkDevice device = m_ctx->getDevice();

    const std::filesystem::path shadersDir = GetExecutableDirectory() / "shaders";
    VkShaderModule vert = vk_utils::loadShaderModule(device, shadersDir / "fullscreen.vert.spv");
    VkShaderModule frag = vk_utils::loadShaderModule(device, shadersDir / "post_dither.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    // No vertex input - the fullscreen triangle is computed from gl_VertexIndex.
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo blendState{};
    blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendState.attachmentCount = 1;
    blendState.pAttachments = &blend;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
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
        throw std::runtime_error("vkCreatePipelineLayout (post) failed");
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
    pipeInfo.pColorBlendState = &blendState;
    pipeInfo.pDynamicState = &dynState;
    pipeInfo.layout = m_pipelineLayout;
    pipeInfo.renderPass = renderPass;
    pipeInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeInfo,
                                  nullptr, &m_pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        throw std::runtime_error("vkCreateGraphicsPipelines (post) failed");
    }

    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
}

void PostProcessor::record(VkCommandBuffer cmd, uint32_t geometryIndex, VkExtent2D extent) {
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
                            m_pipelineLayout, 0, 1,
                            &m_descriptorSets[geometryIndex], 0, nullptr);

    PushConstants pc{};
    pc.ditherStrength = m_ditherStrength;
    pc.paletteSize    = static_cast<int>(m_presets[m_currentPalette].colorsRGBA8.size());
    pc.enableDither   = m_enableDither ? 1 : 0;
    pc.enablePalette  = m_enablePalette ? 1 : 0;
    pc.saturation     = m_saturation;
    pc.warmth         = m_warmth;
    vkCmdPushConstants(cmd, m_pipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

    // Fullscreen triangle: 3 vertices, 1 instance, no buffer bindings.
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void PostProcessor::setPaletteIndex(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_presets.size()) || idx == m_currentPalette) {
        return;
    }
    m_currentPalette = idx;
    uploadPalette(idx);
}

void PostProcessor::resetGradeToDefaults() {
    // Keep these in sync with the in-class member initialisers, which
    // define the engine's signature warm look.
    m_saturation = 1.35f;
    m_warmth     = 0.20f;
}

} // namespace plaster
