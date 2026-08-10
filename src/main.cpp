// ImTube: a lightweight YouTube-style client
// (Dear ImGui + SDL3 + Vulkan, powered by yt-dlp).
// Project layout and behavior are inspired by the FLTube project
// (https://gitlab.com/facuA/fltube).

#include "app/App.h"

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    imtube::App app;
    if (!app.init())
        return 1;

    return app.run();
}
