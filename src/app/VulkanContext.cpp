#include "app/VulkanContext.h"

#include "render/VulkanTexture.h"

#include <SDL3/SDL_vulkan.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace imtube {

namespace {

bool IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension)
{
    for (const VkExtensionProperties& p : properties)
        if (strcmp(p.extensionName, extension) == 0)
            return true;
    return false;
}

#if defined(IMTUBE_VULKAN_DEBUG)
VKAPI_ATTR VkBool32 VKAPI_CALL debug_report(
    VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType,
    uint64_t object, size_t location, int32_t messageCode,
    const char* pLayerPrefix, const char* pMessage, void* pUserData)
{
    (void)flags; (void)object; (void)location; (void)messageCode; (void)pUserData; (void)pLayerPrefix;
    fprintf(stderr, "[vulkan] Debug report from ObjectType: %i\nMessage: %s\n\n", objectType, pMessage);
    return VK_FALSE;
}
#endif /* IMTUBE_VULKAN_DEBUG */

} /* namespace */

void VulkanContext::check_vk_result(VkResult err)
{
    if (err == VK_SUCCESS)
        return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0)
        abort();
}

bool VulkanContext::init(SDL_Window* window)
{
    if (m_initialized)
        return true;

    setup_vulkan(window);
    if (m_instance == VK_NULL_HANDLE || m_device == VK_NULL_HANDLE)
        return false;

    if (SDL_Vulkan_CreateSurface(window, m_instance, m_allocator, &m_wd.Surface) == 0)
    {
        fprintf(stderr, "[sdl] Failed to create Vulkan surface: %s\n", SDL_GetError());
        return false;
    }

    int width, height;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    setup_vulkan_window(width, height);

    m_initialized = true;
    return true;
}

bool VulkanContext::init_imgui(SDL_Window* window)
{
    if (!m_initialized || window == nullptr)
        return false;

    ImGui_ImplSDL3_InitForVulkan(window);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = m_instance;
    init_info.PhysicalDevice = m_physical_device;
    init_info.Device = m_device;
    init_info.QueueFamily = m_queue_family;
    init_info.Queue = m_queue;
    init_info.DescriptorPool = m_descriptor_pool;
    init_info.MinImageCount = m_min_image_count;
    init_info.ImageCount = m_wd.ImageCount;
    init_info.PipelineInfoMain.RenderPass = m_wd.RenderPass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = VulkanContext::check_vk_result;
    if (!ImGui_ImplVulkan_Init(&init_info))
    {
        fprintf(stderr, "Error: ImGui_ImplVulkan_Init() failed\n");
        return false;
    }
    m_imgui_initialized = true;
    return true;
}

void VulkanContext::setup_vulkan(SDL_Window* window)
{
    (void)window;

    ImVector<const char*> instance_extensions;
    {
        uint32_t sdl_extensions_count = 0;
        const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extensions_count);
        for (uint32_t n = 0; n < sdl_extensions_count; n++)
            instance_extensions.push_back(sdl_extensions[n]);
    }

    VkResult err;

    /* Create Vulkan Instance. */
    {
        VkInstanceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

        /* Enumerate available extensions. */
        uint32_t properties_count;
        ImVector<VkExtensionProperties> properties;
        vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
        properties.resize(properties_count);
        err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
        check_vk_result(err);

        /* Enable required extensions. */
        if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
            instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
        {
            instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
#endif

        /* Enable validation layers in debug builds. */
#if defined(IMTUBE_VULKAN_DEBUG)
        const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = layers;
        instance_extensions.push_back("VK_EXT_debug_report");
#endif

        create_info.enabledExtensionCount = (uint32_t)instance_extensions.Size;
        create_info.ppEnabledExtensionNames = instance_extensions.Data;
        err = vkCreateInstance(&create_info, m_allocator, &m_instance);
        check_vk_result(err);

        /* Setup the debug report callback. */
#if defined(IMTUBE_VULKAN_DEBUG)
        auto f_vkCreateDebugReportCallbackEXT =
            (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(m_instance, "vkCreateDebugReportCallbackEXT");
        IM_ASSERT(f_vkCreateDebugReportCallbackEXT != nullptr);
        VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
        debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT |
                                VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
        debug_report_ci.pfnCallback = debug_report;
        debug_report_ci.pUserData = nullptr;
        err = f_vkCreateDebugReportCallbackEXT(m_instance, &debug_report_ci, m_allocator, &m_debug_report);
        check_vk_result(err);
#endif
    }

    /* Select Physical Device (GPU). */
    m_physical_device = ImGui_ImplVulkanH_SelectPhysicalDevice(m_instance);
    IM_ASSERT(m_physical_device != VK_NULL_HANDLE);

    /* Select graphics queue family. */
    m_queue_family = ImGui_ImplVulkanH_SelectQueueFamilyIndex(m_physical_device);
    IM_ASSERT(m_queue_family != (uint32_t)-1);

    /* Create Logical Device (with 1 queue). */
    {
        ImVector<const char*> device_extensions;
        device_extensions.push_back("VK_KHR_swapchain");

        /* Enumerate physical device extensions. */
        uint32_t properties_count;
        ImVector<VkExtensionProperties> properties;
        vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr, &properties_count, nullptr);
        properties.resize(properties_count);
        vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr, &properties_count, properties.Data);
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
        if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
            device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

        const float queue_priority[] = { 1.0f };
        VkDeviceQueueCreateInfo queue_info[1] = {};
        queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info[0].queueFamilyIndex = m_queue_family;
        queue_info[0].queueCount = 1;
        queue_info[0].pQueuePriorities = queue_priority;

        VkDeviceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
        create_info.pQueueCreateInfos = queue_info;
        create_info.enabledExtensionCount = (uint32_t)device_extensions.Size;
        create_info.ppEnabledExtensionNames = device_extensions.Data;
        err = vkCreateDevice(m_physical_device, &create_info, m_allocator, &m_device);
        check_vk_result(err);
        vkGetDeviceQueue(m_device, m_queue_family, 0, &m_queue);
    }

    /* Create Descriptor Pool.
     * Sized generously: one descriptor set per thumbnail texture and one for
     * the video frame texture is allocated via ImGui_ImplVulkan_AddTexture(). */
    {
        constexpr uint32_t kTextureDescriptorCount = 64;
        VkDescriptorPoolSize pool_sizes[] =
        {
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kTextureDescriptorCount },
            { VK_DESCRIPTOR_TYPE_SAMPLER, kTextureDescriptorCount },
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 0;
        for (VkDescriptorPoolSize& pool_size : pool_sizes)
            pool_info.maxSets += pool_size.descriptorCount;
        pool_info.poolSizeCount = (uint32_t)IM_COUNTOF(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        err = vkCreateDescriptorPool(m_device, &pool_info, m_allocator, &m_descriptor_pool);
        check_vk_result(err);
    }

    /* Dedicated command pool for the render thread. Used to upload CPU textures
     * (video frames, thumbnails) outside of the per-frame swapchain command pools. */
    {
        VkCommandPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = m_queue_family;
        err = vkCreateCommandPool(m_device, &pool_info, m_allocator, &m_command_pool);
        check_vk_result(err);
    }
}

void VulkanContext::setup_vulkan_window(int width, int height)
{
    /* Check for WSI support. */
    VkBool32 res;
    vkGetPhysicalDeviceSurfaceSupportKHR(m_physical_device, m_queue_family, m_wd.Surface, &res);
    if (res != VK_TRUE)
    {
        fprintf(stderr, "[vulkan] Error: no WSI support on physical device\n");
        exit(-1);
    }

    /* Select Surface Format. */
    const VkFormat request_surface_image_format[] = {
        VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM
    };
    const VkColorSpaceKHR request_surface_color_space = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    m_wd.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        m_physical_device, m_wd.Surface, request_surface_image_format,
        (size_t)IM_COUNTOF(request_surface_image_format), request_surface_color_space);

    /* Select Present Mode (FIFO: vsync). */
    VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_FIFO_KHR };
    m_wd.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
        m_physical_device, m_wd.Surface, &present_modes[0], IM_COUNTOF(present_modes));

    /* Create SwapChain, RenderPass, Framebuffers, etc. */
    IM_ASSERT(m_min_image_count >= 2);
    ImGui_ImplVulkanH_CreateOrResizeWindow(
        m_instance, m_physical_device, m_device, &m_wd, m_queue_family,
        m_allocator, width, height, m_min_image_count, 0);
}

void VulkanContext::recreate(int width, int height)
{
    ImGui_ImplVulkan_SetMinImageCount(m_min_image_count);
    ImGui_ImplVulkanH_CreateOrResizeWindow(
        m_instance, m_physical_device, m_device, &m_wd, m_queue_family,
        m_allocator, width, height, m_min_image_count, 0);
    m_wd.FrameIndex = 0;
    m_swapchain_rebuild = false;
}

void VulkanContext::new_frame()
{
    ImGui_ImplVulkan_NewFrame();
}

void VulkanContext::render(ImDrawData* draw_data)
{
    ImGui_ImplVulkanH_Window* wd = &m_wd;
    VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkResult err = vkAcquireNextImageKHR(m_device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        m_swapchain_rebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR)
        return;
    if (err != VK_SUBOPTIMAL_KHR)
        check_vk_result(err);

    ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
    {
        err = vkWaitForFences(m_device, 1, &fd->Fence, VK_TRUE, UINT64_MAX); /* wait indefinitely */
        check_vk_result(err);

        err = vkResetFences(m_device, 1, &fd->Fence);
        check_vk_result(err);
    }
    {
        err = vkResetCommandPool(m_device, fd->CommandPool, 0);
        check_vk_result(err);
        VkCommandBufferBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
        check_vk_result(err);
    }
    {
        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = wd->RenderPass;
        info.framebuffer = fd->Framebuffer;
        info.renderArea.extent.width = wd->Width;
        info.renderArea.extent.height = wd->Height;
        info.clearValueCount = 1;
        info.pClearValues = &wd->ClearValue;
        vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    }

    /* Record Dear ImGui primitives into the command buffer. */
    ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

    vkCmdEndRenderPass(fd->CommandBuffer);
    {
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &image_acquired_semaphore;
        info.pWaitDstStageMask = &wait_stage;
        info.commandBufferCount = 1;
        info.pCommandBuffers = &fd->CommandBuffer;
        info.signalSemaphoreCount = 1;
        info.pSignalSemaphores = &render_complete_semaphore;

        err = vkEndCommandBuffer(fd->CommandBuffer);
        check_vk_result(err);
        err = vkQueueSubmit(m_queue, 1, &info, fd->Fence);
        check_vk_result(err);
    }
}

void VulkanContext::present()
{
    ImGui_ImplVulkanH_Window* wd = &m_wd;
    if (m_swapchain_rebuild)
        return;

    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &render_complete_semaphore;
    info.swapchainCount = 1;
    info.pSwapchains = &wd->Swapchain;
    info.pImageIndices = &wd->FrameIndex;
    VkResult err = vkQueuePresentKHR(m_queue, &info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        m_swapchain_rebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR)
        return;
    if (err != VK_SUBOPTIMAL_KHR)
        check_vk_result(err);
    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount; /* Use the next set of semaphores */
}

void VulkanContext::wait_idle()
{
    if (m_device != VK_NULL_HANDLE)
    {
        VkResult err = vkDeviceWaitIdle(m_device);
        check_vk_result(err);
    }
}

std::unique_ptr<RenderTexture> VulkanContext::create_texture(int width, int height)
{
    GpuContext ctx;
    ctx.instance = m_instance;
    ctx.physical_device = m_physical_device;
    ctx.device = m_device;
    ctx.queue = m_queue;
    ctx.command_pool = m_command_pool;

    auto tex = std::make_unique<VulkanTexture>();
    tex->create(ctx, width, height);
    return tex;
}

void VulkanContext::cleanup_vulkan_window()
{
    if (m_device != VK_NULL_HANDLE && m_wd.Surface != VK_NULL_HANDLE)
    {
        ImGui_ImplVulkanH_DestroyWindow(m_instance, m_device, &m_wd, m_allocator);
        vkDestroySurfaceKHR(m_instance, m_wd.Surface, m_allocator);
        m_wd.Surface = VK_NULL_HANDLE;
    }
}

void VulkanContext::cleanup_vulkan()
{
    if (m_device != VK_NULL_HANDLE)
    {
        if (m_command_pool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(m_device, m_command_pool, m_allocator);
            m_command_pool = VK_NULL_HANDLE;
        }
        vkDestroyDescriptorPool(m_device, m_descriptor_pool, m_allocator);
        m_descriptor_pool = VK_NULL_HANDLE;
    }

#if defined(IMTUBE_VULKAN_DEBUG)
    if (m_instance != VK_NULL_HANDLE && m_debug_report != VK_NULL_HANDLE)
    {
        auto f_vkDestroyDebugReportCallbackEXT =
            (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugReportCallbackEXT");
        f_vkDestroyDebugReportCallbackEXT(m_instance, m_debug_report, m_allocator);
        m_debug_report = VK_NULL_HANDLE;
    }
#endif

    if (m_device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(m_device, m_allocator);
        m_device = VK_NULL_HANDLE;
    }
    if (m_instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(m_instance, m_allocator);
        m_instance = VK_NULL_HANDLE;
    }
}

void VulkanContext::shutdown()
{
    if (!m_initialized)
        return;
    wait_idle();
    if (m_imgui_initialized)
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        m_imgui_initialized = false;
    }
    cleanup_vulkan_window();
    cleanup_vulkan();
    m_initialized = false;
}

} /* namespace imtube */
