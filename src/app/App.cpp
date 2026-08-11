#include "app/App.h"

#include "render/RenderBackend.h"
#include "ui/ImTubeUI.h"

#ifdef IMTUBE_RENDERER_VULKAN
#include "app/VulkanContext.h"
#else
#include "render/GlesContext.h"
#endif

#include "imgui.h"
#include "imgui_impl_sdl3.h"

#include <cstdio>
#include <memory>

namespace imtube {

/* Compile-time choice of rendering backend (see CMakeLists.txt). */
std::unique_ptr<RenderBackend> create_render_backend()
{
#ifdef IMTUBE_RENDERER_VULKAN
    return std::make_unique<VulkanContext>();
#else
    return std::make_unique<GlesContext>();
#endif
}

App::App() = default;
App::~App() { shutdown(); }

bool App::init()
{
    /* SDL3's SDL_Init returns bool: true on success, false on failure. */
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");

    m_backend = create_render_backend();
    if (!m_backend)
    {
        std::fprintf(stderr, "no rendering backend available\n");
        return false;
    }

    /* Let the backend configure the window (GL attributes, ...) before the
     * window is created. */
    m_backend->prepare_window();

    m_window = SDL_CreateWindow("ImTube", 1280, 760,
                                SDL_WINDOW_RESIZABLE | m_backend->window_flags());
    if (m_window == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }

    if (!m_backend->init(m_window))
    {
        std::fprintf(stderr, "failed to initialise the rendering backend\n");
        shutdown();
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = "imgui.ini";
    ImGui::StyleColorsDark();

    if (!m_backend->init_imgui(m_window))
    {
        std::fprintf(stderr, "failed to initialise the ImGui backends\n");
        shutdown();
        return false;
    }

    m_ui = new ImTubeUI;
    m_ui->set_backend(m_backend.get());
    m_ui->set_window(m_window);

    m_running = true;
    return true;
}

int App::run()
{
    while (m_running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                m_running = false;
            else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                     event.window.windowID == SDL_GetWindowID(m_window))
                m_running = false;
        }

        if (m_backend->needs_recreate())
        {
            int w = 0, h = 0;
            SDL_GetWindowSize(m_window, &w, &h);
            m_backend->recreate(w, h);
        }

        m_backend->new_frame();      /* renderer NewFrame (e.g. GL clear) */
        ImGui_ImplSDL3_NewFrame();   /* platform NewFrame */
        ImGui::NewFrame();

        m_ui->render();

        ImGui::Render();
        m_backend->render(ImGui::GetDrawData());
        m_backend->present();
    }
    return 0;
}

void App::shutdown()
{
    if (m_ui != nullptr)
    {
        delete m_ui; /* stops playback, joins worker threads, saves lists */
        m_ui = nullptr;
    }
    if (m_backend)
    {
        m_backend->wait_idle();
        m_backend->shutdown();
        m_backend.reset();
    }
    if (ImGui::GetCurrentContext() != nullptr)
        ImGui::DestroyContext();
    if (m_window != nullptr)
    {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
}

} /* namespace imtube */
