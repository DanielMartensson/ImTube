# ImTube dependency resolution.
#
# Resolves the ImTube dependencies:
#   * GLES/EGL     - system OpenGL ES 3.2 dev packages (glesv2/egl pkg-config
#                    modules). Used by the default renderer.
#   * Vulkan       - system loader + headers preferred; falls back to a fetched
#                    copy of KhronosGroup/Vulkan-Headers plus the runtime loader.
#                    Only resolved when IMTUBE_RENDERER=vulkan.
#   * SDL3         - system install preferred; otherwise built from source
#                    (FetchContent) with a trimmed-down configuration.
#   * Dear ImGui   - always fetched, docking branch, pinned to a release tag.
#
# Exposes the following variables:
#   IMTUBE_SDL3_TARGET          (target)   SDL3 library to link against
#   IMTUBE_VULKAN_TARGETS       (targets)  Vulkan headers (+ loader) to link against
#   IMTUBE_IMGUI_CORE_SOURCES   (file list)
#   IMTUBE_IMGUI_BACKEND_SOURCES(file list)
#   IMTUBE_IMGUI_OPENGL3_SOURCE (file or empty)
#   IMTUBE_IMGUI_DEMO_SOURCE    (file)
#   IMTUBE_IMGUI_INCLUDE_DIRS   (dirs)
#
# Override tags with, e.g.:
#   cmake -DIMTUBE_SDL3_TAG=release-3.4.2 -DIMTUBE_IMGUI_TAG=v1.92.9-docking ...

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# ============================================================================
# Vulkan (only needed for the optional Vulkan renderer)
# ============================================================================

if(IMTUBE_RENDERER STREQUAL "vulkan")
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        message(STATUS "ImTube: using system Vulkan (${Vulkan_VERSION})")
        set(IMTUBE_VULKAN_TARGETS Vulkan::Vulkan)
    else()
        message(STATUS "ImTube: no system Vulkan dev package found; fetching Vulkan-Headers")
        set(IMTUBE_VULKAN_HEADERS_TAG "v1.3.296" CACHE STRING "Vulkan-Headers tag to fetch")
        FetchContent_Declare(VulkanHeaders
            GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
            GIT_TAG        ${IMTUBE_VULKAN_HEADERS_TAG}
            GIT_SHALLOW    TRUE
            GIT_PROGRESS   TRUE
        )
        FetchContent_MakeAvailable(VulkanHeaders)

        # Many distros ship only the ABI loader (libvulkan.so.1) without a dev
        # symlink; find the shared object directly.
        find_library(IMTUBE_VULKAN_LOADER
            NAMES vulkan vulkan.so.1 libvulkan.so.1
            PATHS /usr/lib /usr/lib64 /usr/local/lib /usr/lib/x86_64-linux-gnu
        )
        if(NOT IMTUBE_VULKAN_LOADER)
            message(FATAL_ERROR "ImTube: could not locate the Vulkan loader (libvulkan.so.1). "
                                "Install a Vulkan loader + ICD runtime to proceed.")
        endif()
        message(STATUS "ImTube: Vulkan loader found at ${IMTUBE_VULKAN_LOADER}")
        set(IMTUBE_VULKAN_TARGETS Vulkan::Headers "${IMTUBE_VULKAN_LOADER}")
    endif()
else()
    set(IMTUBE_VULKAN_TARGETS "")
endif()

# ============================================================================
# SDL3
# ============================================================================

option(IMTUBE_SDL_WAYLAND "Build SDL3 with the Wayland video driver (set ON for the STM32MPU cross build)" OFF)

find_package(SDL3 QUIET)
if(SDL3_FOUND)
    message(STATUS "ImTube: using system SDL3")
    set(IMTUBE_SDL3_TARGET SDL3::SDL3)
else()
    message(STATUS "ImTube: SDL3 not found on system; building SDL3 from source "
                   "(this takes a minute on the first configure)")

    # Trim the SDL3 build to what ImTube needs.
    set(SDL_SHARED        ON  CACHE BOOL "Build shared SDL3"          FORCE)
    set(SDL_STATIC        OFF CACHE BOOL "Build static SDL3"          FORCE)
    set(SDL_INSTALL       OFF CACHE BOOL "Install SDL3"               FORCE)
    set(SDL_TESTS         OFF CACHE BOOL "Build SDL3 tests"           FORCE)
    set(SDL_TEST_LIBRARY  OFF CACHE BOOL "Build SDL3 test library"    FORCE)
    set(SDL_EXAMPLES      OFF CACHE BOOL "Build SDL3 examples"        FORCE)
    set(SDL_WAYLAND       ${IMTUBE_SDL_WAYLAND} CACHE BOOL "Use Wayland video driver" FORCE)
    set(SDL_X11_XTEST     OFF CACHE BOOL "Use XInput2 XTEST support"  FORCE) # needs libxtst-dev
    if(IMTUBE_RENDERER STREQUAL "vulkan")
        set(SDL_VULKAN        ON  CACHE BOOL "Enable Vulkan support"      FORCE)
        set(SDL_RENDER_VULKAN ON  CACHE BOOL "Enable the Vulkan render driver" FORCE)
    else()
        set(SDL_VULKAN        OFF CACHE BOOL "Enable Vulkan support"      FORCE)
        set(SDL_RENDER_VULKAN OFF CACHE BOOL "Enable the Vulkan render driver" FORCE)
    endif()
    set(SDL_RENDER        ON  CACHE BOOL "Build SDL3 renderer"        FORCE)

    set(IMTUBE_SDL3_TAG "release-3.4.2" CACHE STRING "SDL3 tag to fetch")
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        ${IMTUBE_SDL3_TAG}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
    )
    FetchContent_MakeAvailable(SDL3)
    set(IMTUBE_SDL3_TARGET SDL3::SDL3)
endif()

# ============================================================================
# Dear ImGui (docking branch)
# ============================================================================

set(IMTUBE_IMGUI_TAG "v1.92.9-docking" CACHE STRING "Dear ImGui tag to fetch")

FetchContent_Declare(ImGui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        ${IMTUBE_IMGUI_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(ImGui)

set(IMTUBE_IMGUI_CORE_SOURCES
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
)
if(IMTUBE_RENDERER STREQUAL "vulkan")
    set(IMTUBE_IMGUI_BACKEND_SOURCES
        "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
    )
    set(IMTUBE_IMGUI_OPENGL3_SOURCE "")
else()
    set(IMTUBE_IMGUI_BACKEND_SOURCES
        "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
    )
    set(IMTUBE_IMGUI_OPENGL3_SOURCE "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp")
endif()
set(IMTUBE_IMGUI_DEMO_SOURCE "${imgui_SOURCE_DIR}/imgui_demo.cpp")

set(IMTUBE_IMGUI_INCLUDE_DIRS
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
)
