cmake_minimum_required(VERSION 3.24)

project(ImTube
    VERSION 0.1.0
    DESCRIPTION "Lightweight YouTube-style client built with Dear ImGui, SDL3, GLES/Vulkan and yt-dlp"
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- Options ---------------------------------------------------------------
option(IMTUBE_BUILD_IMGUI_DEMO "Compile the Dear ImGui demo window into the app" OFF)
option(IMTUBE_WITH_CURL "Enable libcurl thumbnail downloads (thumbnails degrade to placeholders otherwise)" ON)
option(IMTUBE_EMBEDDED "Tune the app for the embedded STM32MPU target (set by the toolchain file)" OFF)

# Rendering backend. The GLES (OpenGL ES 3.2) backend is the portable default:
# it runs on a regular Linux PC via Mesa and on the STM32MPU via the
# VeriSilicon gcnano driver. Vulkan is optional (PC only).
set(IMTUBE_RENDERER "gles" CACHE STRING "Rendering backend: gles or vulkan")
set_property(CACHE IMTUBE_RENDERER PROPERTY STRINGS gles vulkan)
if(NOT IMTUBE_RENDERER STREQUAL "gles" AND NOT IMTUBE_RENDERER STREQUAL "vulkan")
    message(FATAL_ERROR "ImTube: IMTUBE_RENDERER must be 'gles' or 'vulkan' (got '${IMTUBE_RENDERER}')")
endif()

# --- Speed --------------------------------------------------------------------
# Release already defaults to -O3 -DNDEBUG. On the native desktop build we also
# target the host CPU and enable link-time optimization for the fastest binary.
# The embedded toolchain cross-compiles for the STM32MPU (Cortex-A35), so it is
# excluded (the portable Release -O3 default still applies there), and Debug
# builds keep -O0 for debugging.
if(NOT IMTUBE_EMBEDDED AND NOT CMAKE_CROSSCOMPILING)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-march=native -mtune=native)
        if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
            add_compile_options(-O3 -flto)
            add_link_options(-flto)
        endif()
    endif()
endif()

# --- Dependencies (SDL3, Vulkan, Dear ImGui, GStreamer) ----------------------
include(ImTubeDeps)

# --- Core dependencies (GStreamer, libcurl, stb_image) ------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
    gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0
)

if(IMTUBE_WITH_CURL)
    pkg_check_modules(CURL QUIET IMPORTED_TARGET libcurl)
    if(NOT CURL_FOUND)
        message(STATUS "ImTube: libcurl not found; thumbnails will degrade to placeholders")
    endif()
endif()

# stb_image: header-only image decode for thumbnails (fetched, like Dear ImGui).
# The stb repo carries no release tags, so pin the upstream commit.
set(IMTUBE_STB_COMMIT "2c980bb" CACHE STRING "stb commit to fetch")
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        ${IMTUBE_STB_COMMIT}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(stb)
set(IMTUBE_STB_INCLUDE_DIR "${stb_SOURCE_DIR}")

# nlohmann/json: header-only JSON parsing for yt-dlp output.
set(IMTUBE_NLOHMANN_TAG "v3.11.3" CACHE STRING "nlohmann/json release tag to fetch")
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        ${IMTUBE_NLOHMANN_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# --- Renderer backend (sources, defines, link targets) ----------------------
if(IMTUBE_RENDERER STREQUAL "vulkan")
    set(IMTUBE_RENDERER_SOURCES
        src/app/VulkanContext.cpp
        src/render/VulkanTexture.cpp
    )
    set(IMTUBE_RENDERER_DEFINE IMTUBE_RENDERER_VULKAN)
    set(IMTUBE_RENDERER_LIBS ${IMTUBE_VULKAN_TARGETS})
else()
    pkg_check_modules(GLES2 REQUIRED IMPORTED_TARGET glesv2)
    pkg_check_modules(EGL   REQUIRED IMPORTED_TARGET egl)

    set(IMTUBE_RENDERER_SOURCES
        src/render/GlesContext.cpp
        src/render/GlesTexture.cpp
    )
    set(IMTUBE_RENDERER_DEFINE IMTUBE_RENDERER_GLES)
    set(IMTUBE_RENDERER_LIBS PkgConfig::GLES2 PkgConfig::EGL)
    set_source_files_properties(${IMTUBE_IMGUI_OPENGL3_SOURCE} PROPERTIES
        COMPILE_DEFINITIONS IMGUI_IMPL_OPENGL_ES3
    )
endif()

# --- Target -----------------------------------------------------------------
add_executable(imtube
    src/main.cpp
    src/app/App.cpp
    src/core/GStreamerPlayer.cpp
    src/core/PlaybackRate.cpp
    src/core/SubtitleParser.cpp
    src/core/YtDlpHelper.cpp
    src/core/ThumbnailLoader.cpp
    src/ui/ImTubeUI.cpp
    ${IMTUBE_RENDERER_SOURCES}
    ${IMTUBE_IMGUI_CORE_SOURCES}
    ${IMTUBE_IMGUI_BACKEND_SOURCES}
)

if(IMTUBE_BUILD_IMGUI_DEMO)
    target_sources(imtube PRIVATE ${IMTUBE_IMGUI_DEMO_SOURCE})
endif()

target_compile_definitions(imtube PRIVATE
    IMTUBE_VERSION="${PROJECT_VERSION}"
    ${IMTUBE_RENDERER_DEFINE}
    $<$<BOOL:${IMTUBE_BUILD_IMGUI_DEMO}>:IMTUBE_ENABLE_IMGUI_DEMO>
    $<$<BOOL:${IMTUBE_EMBEDDED}>:IMTUBE_EMBEDDED>
    $<$<AND:$<CONFIG:Debug>,$<STREQUAL:${IMTUBE_RENDERER},vulkan>>:IMTUBE_VULKAN_DEBUG>
    $<$<NOT:$<BOOL:${IMTUBE_WITH_CURL}>>:IMTUBE_WITHOUT_CURL>
)

target_include_directories(imtube PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${IMTUBE_IMGUI_INCLUDE_DIRS}
    ${IMTUBE_STB_INCLUDE_DIR}
)

find_package(Threads REQUIRED)

target_link_libraries(imtube PRIVATE
    ${IMTUBE_SDL3_TARGET}
    ${IMTUBE_RENDERER_LIBS}
    PkgConfig::GSTREAMER
    nlohmann_json::nlohmann_json
    $<$<BOOL:${CURL_FOUND}>:PkgConfig::CURL>
    Threads::Threads
    dl
)

# --- Tests ------------------------------------------------------------------
# The embedded build targets the STM32MPU toolchain, which cannot run tests.
if(NOT IMTUBE_EMBEDDED)
    enable_testing()

    add_executable(imtube_tests
        tests/test_main.cpp
        src/core/PlaybackRate.cpp
        src/core/SubtitleParser.cpp
        src/core/YtDlpHelper.cpp
    )
    target_include_directories(imtube_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(imtube_tests PRIVATE
        PkgConfig::GSTREAMER
        nlohmann_json::nlohmann_json
        Threads::Threads
        dl
    )

    add_test(NAME imtube_tests COMMAND imtube_tests)
endif()
