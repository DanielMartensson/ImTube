#include "app/App.h"

#include "app/VulkanContext.h"
#include "ui/ImTubeUI.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"

#include <cstdio>

namespace imtube {

bool App::init()
{
    // --- SDL -----------------------------------------------------------------
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        fprintf(stderr, "Error: SDL_Init(): %s\n", SDL_GetError());
        return false;
    }

    const float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    const SDL_WindowFlags window_flags =
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    m_window = SDL_CreateWindow("ImTube", (int)(1280 * main_scale), (int)(800 * main_scale), window_flags);
    if (m_window == nullptr)
    {
        fprintf(stderr, "Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    // --- Vulkan --------------------------------------------------------------
    m_vk = new VulkanContext();
    if (!m_vk->init(m_window))
    {
        fprintf(stderr, "Error: Vulkan initialization failed.\n");
        shutdown();
        return false;
    }

    // --- Dear ImGui ----------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Keyboard controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Gamepad controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Multi-viewport / platform windows

    ImGui::StyleColorsDark();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale); // Bake a fixed style scale
    style.FontScaleDpi = main_scale;
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;

    // When viewports are enabled, make platform windows look identical to regular ones.
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // --- Platform / Renderer backends ----------------------------------------
    ImGui_ImplSDL3_InitForVulkan(m_window);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = m_vk->instance();
    init_info.PhysicalDevice = m_vk->physical_device();
    init_info.Device = m_vk->device();
    init_info.QueueFamily = m_vk->queue_family();
    init_info.Queue = m_vk->queue();
    init_info.DescriptorPool = m_vk->descriptor_pool();
    init_info.MinImageCount = m_vk->min_image_count();
    init_info.ImageCount = m_vk->window_data().ImageCount;
    init_info.PipelineInfoMain.RenderPass = m_vk->window_data().RenderPass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = VulkanContext::check_vk_result;
    ImGui_ImplVulkan_Init(&init_info);

    // --- User interface ------------------------------------------------------
    m_ui = new ImTubeUI();
    {
        GpuContext gpu;
        gpu.instance = m_vk->instance();
        gpu.physical_device = m_vk->physical_device();
        gpu.device = m_vk->device();
        gpu.queue = m_vk->queue();
        gpu.command_pool = m_vk->command_pool();
        m_ui->set_gpu(gpu);
    }

    SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(m_window);

    m_running = true;
    return true;
}

int App::run()
{
    ImGuiIO& io = ImGui::GetIO();
    while (m_running)
    {
        // Poll and handle events (inputs, window resize, etc.)
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                m_running = false;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(m_window))
                m_running = false;
        }

        if (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            continue;
        }

        // Resize swapchain?
        int fb_width, fb_height;
        SDL_GetWindowSizeInPixels(m_window, &fb_width, &fb_height);
        if (fb_width > 0 && fb_height > 0 &&
            (m_vk->needs_swapchain_rebuild() ||
             m_vk->window_data().Width != (uint32_t)fb_width ||
             m_vk->window_data().Height != (uint32_t)fb_height))
        {
            m_vk->recreate_swapchain(fb_width, fb_height);
        }

        // Start the Dear ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        m_ui->render();

        // Rendering
        ImGui::Render();
        ImDrawData* main_draw_data = ImGui::GetDrawData();
        const bool main_is_minimized =
            (main_draw_data->DisplaySize.x <= 0.0f || main_draw_data->DisplaySize.y <= 0.0f);
        if (!main_is_minimized)
            m_vk->frame_render(main_draw_data);

        // Update and Render additional platform windows
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        // Present the main platform window
        if (!main_is_minimized)
            m_vk->frame_present();
    }

    return 0;
}

void App::shutdown()
{
    // Destroy the UI first: it owns Vulkan textures and worker threads that
    // must be released while the device is still alive.
    delete m_ui;
    m_ui = nullptr;

    if (m_vk != nullptr && m_vk->is_initialized())
    {
        m_vk->wait_idle();
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

    delete m_vk;
    m_vk = nullptr;

    if (m_window != nullptr)
    {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }

    SDL_Quit();
    m_running = false;
}

} // namespace imtube
