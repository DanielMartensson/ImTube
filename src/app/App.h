#pragma once

#include <SDL3/SDL.h>

#include <memory>

namespace imtube {

class RenderBackend;
class ImTubeUI;

/* Application shell: owns the SDL window, the rendering backend, the Dear ImGui
 * context and the ImTube user interface, and drives the main render loop.
 *
 * The rendering backend is selected at compile time (default: OpenGL ES 3.2 so
 * the same binary path runs on a regular Linux PC and on the STM32MP257F). */
class App {
public:
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool init();
    int run();
    void shutdown();

private:
    SDL_Window* m_window = nullptr;
    std::unique_ptr<RenderBackend> m_backend;
    ImTubeUI* m_ui = nullptr;
    bool m_running = false;
};

} /* namespace imtube */
