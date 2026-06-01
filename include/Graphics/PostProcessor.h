#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace plaster {

class VulkanContext;

// PostProcessor implements the Plastiboo dither + palette pass.
//
// It draws a fullscreen triangle that:
//   - samples the geometry-pass color image,
//   - perturbs it with an 8x8 Bayer threshold,
//   - snaps the result to the nearest entry in a runtime-swappable palette.
//
// The class owns its own pipeline, descriptor pool, palette image, and one
// descriptor set *per geometry input* (typically one per frame-in-flight).
// The render pass and the output framebuffer are owned by the Renderer so
// it can compose them with the swapchain blit.
class PostProcessor {
public:
    // Hard cap matching the shader's loop bound; presets must not exceed.
    static constexpr int kMaxPaletteEntries = 16;

    struct PalettePreset {
        std::string name;
        // Each entry is RGBA8 (sRGB-encoded byte values, alpha = 255).
        std::vector<uint32_t> colorsRGBA8;
    };

    PostProcessor(VulkanContext* ctx,
                  VkCommandPool transferPool,
                  VkRenderPass postRenderPass,
                  const std::vector<VkImageView>& geometryColorViews,
                  VkExtent2D extent);
    ~PostProcessor();

    PostProcessor(const PostProcessor&) = delete;
    PostProcessor& operator=(const PostProcessor&) = delete;

    // Record draw commands inside an already-begun render pass.
    // `geometryIndex` selects which pre-built descriptor set to bind
    // (usually = current frame-in-flight index).
    void record(VkCommandBuffer cmd, uint32_t geometryIndex, VkExtent2D extent);

    // ---- Runtime knobs (driven by ImGui) ----
    void  setDitherStrength(float v)  { m_ditherStrength = v; }
    float getDitherStrength() const   { return m_ditherStrength; }

    void  setDitherEnabled(bool b)    { m_enableDither = b; }
    bool  getDitherEnabled() const    { return m_enableDither; }

    void  setPaletteEnabled(bool b)   { m_enablePalette = b; }
    bool  getPaletteEnabled() const   { return m_enablePalette; }

    int   getPaletteIndex() const     { return m_currentPalette; }
    void  setPaletteIndex(int idx);

    const std::vector<PalettePreset>& getPresets() const { return m_presets; }

    // ---- Colour grading (applied before dither + palette quantize) ----
    //
    // The engine ships with a warm, high-saturation default look. The
    // grading runs upstream of the palette snap so saturation/warmth
    // change which palette entry a pixel ends up resolving to, rather
    // than tinting an already-quantized image.
    void  setSaturation(float v)      { m_saturation = v; }
    float getSaturation() const       { return m_saturation; }

    void  setWarmth(float v)          { m_warmth = v; }
    float getWarmth() const           { return m_warmth; }

    // Convenience: restore the engine's signature warm/saturated defaults
    // (also called automatically at construction).
    void  resetGradeToDefaults();

private:
    void createPalettePresets();
    void createPaletteImage(VkCommandPool transferPool);
    void uploadPalette(int presetIndex);

    void createDescriptorResources(const std::vector<VkImageView>& geometryColorViews);
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent);

    VulkanContext* m_ctx;
    VkCommandPool  m_transferPool;

    // Palette resources.
    VkImage        m_paletteImage       = VK_NULL_HANDLE;
    VkDeviceMemory m_paletteImageMemory = VK_NULL_HANDLE;
    VkImageView    m_paletteImageView   = VK_NULL_HANDLE;
    VkSampler      m_paletteSampler     = VK_NULL_HANDLE;

    // Reusable staging buffer for palette uploads (max 16 RGBA8 entries).
    VkBuffer       m_paletteStagingBuffer       = VK_NULL_HANDLE;
    VkDeviceMemory m_paletteStagingBufferMemory = VK_NULL_HANDLE;

    // Sampler for the geometry color input (NEAREST so the dither stays
    // crisp through the post pass).
    VkSampler m_geometrySampler = VK_NULL_HANDLE;

    // Descriptor objects. One set per geometry input.
    VkDescriptorSetLayout        m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool             m_descriptorPool      = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;

    // Pipeline.
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline       = VK_NULL_HANDLE;

    // Presets and current state.
    std::vector<PalettePreset> m_presets;
    int                        m_currentPalette = 0;

    float m_ditherStrength = 0.07f;
    bool  m_enableDither   = true;
    bool  m_enablePalette  = true;

    // Default colour grade: warm, high saturation - the engine's signature.
    float m_saturation = 1.35f; // 1.0 = neutral; > 1.0 = punchier
    float m_warmth     = 0.20f; // 0.0 = neutral; > 0.0 = warmer
};

} // namespace plaster
