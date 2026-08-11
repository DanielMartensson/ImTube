#pragma once

#include "imgui.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>

namespace imtube {

class RenderTexture;

/* Backend-agnostic renderer interface. Everything lives on the render (main)
 * thread, mirroring the single-threaded Vulkan-only flow the project started
 * from.
 *
 * Implementations:
 *   * GlesContext   - OpenGL ES 3.2 via SDL (default; runs on any Linux PC and
 *                     on the STM32MP25 VeriSilicon GPU through OpenSTLinux).
 *   * VulkanContext - Vulkan via SDL (optional, PC-focused backend).
 *
 * Lifecycle driven by the App shell:
 *   1. create_render_backend()                  (compile-time choice)
 *   2. backend->prepare_window()                before SDL_CreateWindow
 *   3. backend->init(window)                    GPU context / swapchain
 *   4. ImGui::CreateContext() + IO configuration
 *   5. backend->init_imgui(window)              ImGui platform + renderer backends
 *   6. per frame: new_frame() -> render() -> present()
 *   7. backend->shutdown()                      release ImGui backends + GPU
 */
class RenderBackend {
public:
    virtual ~RenderBackend() = default;

    /* Human-readable backend name for logs and the About screen. */
    virtual const char* name() const = 0;

    /* Extra SDL_WindowFlags the window must be created with
     * (e.g. SDL_WINDOW_OPENGL). */
    virtual SDL_WindowFlags window_flags() const = 0;

    /* Called before SDL_CreateWindow() so the backend can configure the window
     * (e.g. GL attributes). Default: nothing. */
    virtual void prepare_window() {}

    /* Create the GPU context / swapchain for the window. Does not touch ImGui. */
    virtual bool init(SDL_Window* window) = 0;

    /* Initialize the ImGui platform + renderer backends. Must be called after
     * ImGui::CreateContext() and after init(). */
    virtual bool init_imgui(SDL_Window* window) = 0;

    /* Tear the ImGui backends and the GPU context down. Idempotent. */
    virtual void shutdown() = 0;

    virtual bool is_initialized() const = 0;

    /* True once the swapchain/backbuffer must be recreated at the current size. */
    virtual bool needs_recreate() const { return false; }
    virtual void recreate(int width, int height) {}

    /* ImGui renderer NewFrame, called before ImGui_ImplSDL3_NewFrame(). */
    virtual void new_frame() = 0;

    /* Render the ImGui draw data into the backbuffer. */
    virtual void render(ImDrawData* draw_data) = 0;

    /* Present the backbuffer to the window. */
    virtual void present() = 0;

    /* Wait for pending GPU work (used during shutdown). */
    virtual void wait_idle() {}

    /* Create a CPU->GPU RGBA8 texture. Render thread only. */
    virtual std::unique_ptr<RenderTexture> create_texture(int width, int height) = 0;
};

/* Creates the compile-time-selected backend (GlesContext unless
 * IMTUBE_RENDERER_VULKAN is defined). Defined in App.cpp. */
std::unique_ptr<RenderBackend> create_render_backend();

} /* namespace imtube */
