#include "app/App.h"

#include "render/RenderBackend.h"
#include "ui/ImTubeUI.h"

#ifdef IMTUBE_RENDERER_GLES
#include "render/GlesContext.h"
#endif
#ifdef IMTUBE_RENDERER_VULKAN
#include "app/VulkanContext.h"
#endif

#include "imgui.h"
#include "imgui_impl_sdl3.h"

#include <cstdio>

namespace imtube {

namespace {

std::unique_ptr<RenderBackend> create_render_backend()
{
#ifdef IMTUBE_RENDERER_VULKAN
    return std::make_unique<VulkanContext>();
#else
    return std::make_unique<GlesContext>();
#endif
}

} // namespace

App::App() = default;

App::~App()
{
    shutdown();
}

bool App::init()
{
    // --- SDL -----------------------------------------------------------------
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        fprintf(stderr, "Error: SDL_Init(): %s\n", SDL_GetError());
        return false;
    }

    // --- Rendering backend (compile-time choice) -----------------------------
    m_backend = create_render_backend();
    m_backend->prepare_window();

    const float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    const SDL_WindowFlags window_flags =
        (SDL_WindowFlags)(m_backend->window_flags() | SDL_WINDOW_RESIZABLE |
                          SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    m_window = SDL_CreateWindow("ImTube", (int)(1280 * main_scale), (int)(800 * main_scale), window_flags);
    if (m_window == nullptr)
    {
        fprintf(stderr, "Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    if (!m_backend->init(m_window))
    {
        fprintf(stderr, "Error: %s initialization failed.\n", m_backend->name());
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
    if (!m_backend->init_imgui(m_window))
    {
        fprintf(stderr, "Error: ImGui backend initialization failed (%s).\n", m_backend->name());
        shutdown();
        return false;
    }

    // --- User interface ------------------------------------------------------
    m_ui = new ImTubeUI();
    m_ui->set_backend(m_backend.get());

    SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(m_window);

    m_running = true;
    return true;
}

int App::run()
{
    ImGuiIO& io = ImGui::GetIO();
    int last_fb_width = 0;
    int last_fb_height = 0;

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

        // Resize swapchain / backbuffer?
        int fb_width, fb_height;
        SDL_GetWindowSizeInPixels(m_window, &fb_width, &fb_height);
        if (fb_width > 0 && fb_height > 0 &&
            (m_backend->needs_recreate() ||
             fb_width != last_fb_width || fb_height != last_fb_height))
        {
            m_backend->recreate(fb_width, fb_height);
            last_fb_width = fb_width;
            last_fb_height = fb_height;
        }

        // Start the Dear ImGui frame
        m_backend->new_frame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        m_ui->render();

        // Rendering
        ImGui::Render();
        ImDrawData* main_draw_data = ImGui::GetDrawData();
        const bool main_is_minimized =
            (main_draw_data->DisplaySize.x <= 0.0f || main_draw_data->DisplaySize.y <= 0.0f);
        if (!main_is_minimized)
            m_backend->render(main_draw_data);

        // Update and Render additional platform windows
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        // Present the main platform window
        if (!main_is_minimized)
            m_backend->present();
    }

    return 0;
}

void App::shutdown()
{
    // Destroy the UI first: it owns textures and worker threads that must be
    // released while the GPU context is still alive.
    delete m_ui;
    m_ui = nullptr;

    if (m_backend != nullptr)
        m_backend->shutdown();
    m_backend.reset();

    ImGui::DestroyContext();

    if (m_window != nullptr)
    {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }

    SDL_Quit();
    m_running = false;
}

} // namespace imtube
