#include "render/GlesContext.h"

#include "render/GlesTexture.h"

#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"

#include <cstdio>

namespace imtube {

void GlesContext::prepare_window()
{
    /* Request an OpenGL ES 3.x context (supports the GLES3 core profile used by
     * imgui_impl_opengl3). SDL3 must see these attributes before the window is
     * created, so this runs ahead of SDL_CreateWindow(). */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    /* ImGui writes colours that are already sRGB-encoded, so the default
     * framebuffer must be a plain (linear) RGB buffer. SDL3's default of
     * SDL_GL_FRAMEBUFFER_SRGB_CAPABLE=-1 is treated as "request sRGB" by the
     * X11 backend, which makes the GPU re-encode every pixel on write and
     * visibly brightens the whole UI. Force a non-sRGB config. */
    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 0);
    SDL_SetHint(SDL_HINT_OPENGL_FORCE_SRGB_FRAMEBUFFER, "0");
}

bool GlesContext::init(SDL_Window* window)
{
    if (window == nullptr)
        return false;

    m_gl_context = SDL_GL_CreateContext(window);
    if (m_gl_context == nullptr)
    {
        fprintf(stderr, "Error: SDL_GL_CreateContext(): %s\n", SDL_GetError());
        return false;
    }
    if (!SDL_GL_MakeCurrent(window, m_gl_context))
    {
        fprintf(stderr, "Error: SDL_GL_MakeCurrent(): %s\n", SDL_GetError());
        SDL_GL_DestroyContext(m_gl_context);
        m_gl_context = nullptr;
        return false;
    }

    /* Best-effort vsync; not all drivers support it. */
    SDL_GL_SetSwapInterval(1);

    m_window = window;
    m_initialized = true;
    return true;
}

bool GlesContext::init_imgui(SDL_Window* window)
{
    if (!m_initialized)
        return false;

    if (!ImGui_ImplSDL3_InitForOpenGL(window, m_gl_context))
    {
        fprintf(stderr, "Error: ImGui_ImplSDL3_InitForOpenGL() failed\n");
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 300 es"))
    {
        fprintf(stderr, "Error: ImGui_ImplOpenGL3_Init() failed\n");
        return false;
    }
    m_imgui_initialized = true;
    return true;
}

void GlesContext::shutdown()
{
    if (m_imgui_initialized)
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        m_imgui_initialized = false;
    }
    if (m_gl_context != nullptr)
    {
        SDL_GL_DestroyContext(m_gl_context);
        m_gl_context = nullptr;
    }
    m_window = nullptr;
    m_initialized = false;
}

void GlesContext::new_frame()
{
    ImGui_ImplOpenGL3_NewFrame();
}

void GlesContext::render(ImDrawData* draw_data)
{
    ImGui_ImplOpenGL3_RenderDrawData(draw_data);
}

void GlesContext::present()
{
    if (m_window != nullptr)
        SDL_GL_SwapWindow(m_window);
}

std::unique_ptr<RenderTexture> GlesContext::create_texture(int width, int height)
{
    return std::make_unique<GlesTexture>(width, height);
}

} /* namespace imtube */
