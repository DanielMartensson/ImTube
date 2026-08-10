#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace imtube {

// Minimal GPU handles needed to create and upload CPU-side textures. Filled by
// the application from the VulkanContext.
struct GpuContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
};

// A sampled 2D RGBA8 texture renderable by the Dear ImGui Vulkan backend
// (registered via ImGui_ImplVulkan_AddTexture). Used for video frames and
// thumbnails. All functions must be called from the render thread.
class VulkanTexture {
public:
    VulkanTexture() = default;
    ~VulkanTexture() { destroy(); }

    VulkanTexture(const VulkanTexture&) = delete;
    VulkanTexture& operator=(const VulkanTexture&) = delete;

    VulkanTexture(VulkanTexture&& other) noexcept;
    VulkanTexture& operator=(VulkanTexture&& other) noexcept;

    // Create a w*h texture. Re-allocates if the size changed.
    bool create(const GpuContext& ctx, int width, int height);

    // Upload full-frame RGBA8 pixels (width*height*4 bytes).
    bool upload(const uint8_t* rgba_pixels);

    void destroy();

    bool valid() const { return m_image != VK_NULL_HANDLE; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    VkDescriptorSet descriptor_set() const { return m_descriptor_set; }
    const GpuContext& context() const { return m_ctx; }

private:
    uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags properties) const;
    bool allocate_image_memory(VkDeviceSize size, VkMemoryPropertyFlags properties, VkDeviceMemory* out) const;
    bool ensure_staging(VkDeviceSize size);
    void submit_single(VkCommandBuffer cmdbuf);

    GpuContext m_ctx;
    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_image_memory = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptor_set = VK_NULL_HANDLE;

    VkBuffer m_staging = VK_NULL_HANDLE;
    VkDeviceMemory m_staging_memory = VK_NULL_HANDLE;
    VkDeviceSize m_staging_size = 0;

    VkFence m_fence = VK_NULL_HANDLE;

    int m_width = 0;
    int m_height = 0;
};

} // namespace imtube
