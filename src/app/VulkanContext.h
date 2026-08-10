#pragma once

#include "imgui_impl_vulkan.h"

#include <SDL3/SDL.h>

struct ImGui_ImplVulkanH_Window;

namespace imtube {

// Wraps the Vulkan instance/device/swapchain lifecycle and the Dear ImGui Vulkan
// backend, mirroring the official "example_sdl3_vulkan" bootstrap.
class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext() { shutdown(); }

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    // Create the instance, device and swapchain for the given SDL window.
    bool init(SDL_Window* window);

    // Release every Vulkan resource. Safe to call multiple times.
    void shutdown();

    bool is_initialized() const { return m_initialized; }

    // True once the presentation path requested a swapchain rebuild.
    bool needs_swapchain_rebuild() const { return m_swapchain_rebuild; }

    // Recreate the swapchain at the given pixel size (also clears the rebuild flag).
    void recreate_swapchain(int width, int height);

    // Record ImGui draw data into the current frame's command buffer.
    void frame_render(ImDrawData* draw_data);

    // Present the current frame.
    void frame_present();

    void wait_idle();

    // Accessors used when initializing the ImGui backends.
    VkInstance instance() const { return m_instance; }
    VkPhysicalDevice physical_device() const { return m_physical_device; }
    VkDevice device() const { return m_device; }
    uint32_t queue_family() const { return m_queue_family; }
    VkQueue queue() const { return m_queue; }
    VkDescriptorPool descriptor_pool() const { return m_descriptor_pool; }
    VkCommandPool command_pool() const { return m_command_pool; }
    uint32_t min_image_count() const { return m_min_image_count; }
    ImGui_ImplVulkanH_Window& window_data() { return m_wd; }

    static void check_vk_result(VkResult err);

private:
    void setup_vulkan(SDL_Window* window);
    void setup_vulkan_window(int width, int height);
    void cleanup_vulkan();
    void cleanup_vulkan_window();

    VkAllocationCallbacks* m_allocator = nullptr;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    uint32_t m_queue_family = (uint32_t)-1;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkPipelineCache m_pipeline_cache = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptor_pool = VK_NULL_HANDLE;
    VkCommandPool m_command_pool = VK_NULL_HANDLE;
    uint32_t m_min_image_count = 2;
    ImGui_ImplVulkanH_Window m_wd;
    bool m_swapchain_rebuild = false;
    bool m_initialized = false;
#ifdef IMTUBE_VULKAN_DEBUG
    VkDebugReportCallbackEXT m_debug_report = VK_NULL_HANDLE;
#endif
};

} // namespace imtube
