#pragma once

#include <SDL3/SDL.h>

namespace imtube {

class VulkanContext;
class ImTubeUI;

// Application shell: owns the SDL window, the Vulkan context, the Dear ImGui
// context and the ImTube user interface, and drives the main render loop.
class App {
public:
    App() = default;
    ~App() { shutdown(); }

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool init();
    int run();
    void shutdown();

private:
    SDL_Window* m_window = nullptr;
    VulkanContext* m_vk = nullptr;
    ImTubeUI* m_ui = nullptr;
    bool m_running = false;
};

} // namespace imtube
