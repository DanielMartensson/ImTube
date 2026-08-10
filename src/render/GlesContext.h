#pragma once

#include "render/RenderBackend.h"

#include <SDL3/SDL.h>

namespace imtube {

class GlesTexture;

// OpenGL ES 3.2 renderer backend for ImGui, driven through SDL (which manages
// the EGL context on both PC/Mesa and STM32MP25/gcnano). This is the default
// backend because GLES is the one graphics API guaranteed to work on both a
// regular Linux PC and the STM32MP25's VeriSilicon GPU.
class GlesContext : public RenderBackend {
public:
    GlesContext() = default;
    ~GlesContext() override { shutdown(); }

    GlesContext(const GlesContext&) = delete;
    GlesContext& operator=(const GlesContext&) = delete;

    const char* name() const override { return "OpenGL ES 3.2"; }

    SDL_WindowFlags window_flags() const override { return SDL_WINDOW_OPENGL; }

    void prepare_window() override;
    bool init(SDL_Window* window) override;
    bool init_imgui(SDL_Window* window) override;
    void shutdown() override;
    bool is_initialized() const override { return m_initialized; }

    void new_frame() override;
    void render(ImDrawData* draw_data) override;
    void present() override;

    std::unique_ptr<RenderTexture> create_texture(int width, int height) override;

private:
    SDL_Window* m_window = nullptr;
    SDL_GLContext m_gl_context = nullptr;
    bool m_initialized = false;
    bool m_imgui_initialized = false;
};

} // namespace imtube
