#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace plaster {

class VulkanContext;

namespace vk_utils {

// Find a memory type that satisfies both the resource's memory type bitmask
// (returned by vkGet*MemoryRequirements) and the requested property flags.
// Throws std::runtime_error if no compatible type exists.
uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
                        uint32_t typeFilter,
                        VkMemoryPropertyFlags properties);

// Allocate a VkBuffer with backing memory. Caller owns both handles and
// must destroy / free them.
void createBuffer(VulkanContext* ctx,
                  VkDeviceSize size,
                  VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags properties,
                  VkBuffer& outBuffer,
                  VkDeviceMemory& outMemory);

// Allocate a 2D VkImage with backing memory. Caller owns both handles.
void createImage2D(VulkanContext* ctx,
                   uint32_t width,
                   uint32_t height,
                   VkFormat format,
                   VkImageTiling tiling,
                   VkImageUsageFlags usage,
                   VkMemoryPropertyFlags properties,
                   VkImage& outImage,
                   VkDeviceMemory& outMemory);

// Create a 2D image view covering the whole image, single mip, single layer.
VkImageView createImageView2D(VkDevice device,
                              VkImage image,
                              VkFormat format,
                              VkImageAspectFlags aspect);

// Begin a one-shot primary command buffer allocated from the given pool.
// Use with endSingleTimeCommands to record short-lived transfer / layout
// transition work.
VkCommandBuffer beginSingleTimeCommands(VkDevice device, VkCommandPool pool);

// Submit and wait on the work recorded by beginSingleTimeCommands, then
// release the command buffer back to the pool.
void endSingleTimeCommands(VkDevice device,
                           VkCommandPool pool,
                           VkQueue queue,
                           VkCommandBuffer commandBuffer);

// Issue an image layout transition with a conservative pipeline barrier
// covering the transitions we actually use: UNDEFINED -> TRANSFER_DST,
// TRANSFER_DST -> SHADER_READ_ONLY.
void transitionImageLayout(VkCommandBuffer cmd,
                           VkImage image,
                           VkImageLayout oldLayout,
                           VkImageLayout newLayout,
                           VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

// Copy a packed pixel buffer into a VkImage. The image must already be in
// TRANSFER_DST_OPTIMAL layout.
void copyBufferToImage(VkCommandBuffer cmd,
                       VkBuffer buffer,
                       VkImage image,
                       uint32_t width,
                       uint32_t height);

// Load a SPIR-V binary from disk and create a VkShaderModule.
// The file is expected at <exe_dir>/shaders/<filename>.
VkShaderModule loadShaderModule(VkDevice device, const std::filesystem::path& path);

} // namespace vk_utils
} // namespace plaster
