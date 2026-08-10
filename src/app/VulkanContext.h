#pragma once

#include "render/RenderBackend.h"

#include "imgui_impl_vulkan.h"

#include <SDL3/SDL.h>

struct ImGui_ImplVulkanH_Window;

namespace imtube {

// Vulkan implementation of the RenderBackend interface (PC-oriented; the
// STM32MP25 VeriSilicon Vulkan driver is currently immature, so the default
// backend there is GlesContext). Wraps the instance/device/swapchain lifecycle
// and the Dear ImGui Vulkan backend, mirroring "example_sdl3_vulkan".
class VulkanContext : public RenderBackend {
public:
    VulkanContext() = default;
    ~VulkanContext() override { shutdown(); }

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    // RenderBackend interface -----------------------------------------------
    const char* name() const override { return "Vulkan"; }
    SDL_WindowFlags window_flags() const override { return SDL_WINDOW_VULKAN; }

    // Create the instance, device and swapchain for the given SDL window.
    bool init(SDL_Window* window) override;

    bool init_imgui(SDL_Window* window) override;

    // Release every Vulkan resource. Safe to call multiple times.
    void shutdown() override;

    bool is_initialized() const override { return m_initialized; }

    // True once the presentation path requested a swapchain rebuild.
    bool needs_recreate() const override { return m_swapchain_rebuild; }

    // Recreate the swapchain at the given pixel size (also clears the rebuild flag).
    void recreate(int width, int height) override;

    void new_frame() override;

    // Record ImGui draw data into the current frame's command buffer.
    void render(ImDrawData* draw_data) override;

    // Present the current frame.
    void present() override;

    void wait_idle() override;

    std::unique_ptr<RenderTexture> create_texture(int width, int height) override;

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
    bool m_imgui_initialized = false;
#ifdef IMTUBE_VULKAN_DEBUG
    VkDebugReportCallbackEXT m_debug_report = VK_NULL_HANDLE;
#endif
};

} // namespace imtube
