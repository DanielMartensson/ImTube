cmake_minimum_required(VERSION 3.24)

project(ImTube
    VERSION 0.1.0
    DESCRIPTION "Lightweight YouTube-style client built with Dear ImGui, SDL3, Vulkan and yt-dlp"
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

# --- Target -----------------------------------------------------------------
add_executable(imtube
    src/main.cpp
    src/app/App.cpp
    src/app/VulkanContext.cpp
    src/core/GStreamerPlayer.cpp
    src/core/YtDlpHelper.cpp
    src/core/ThumbnailLoader.cpp
    src/render/VulkanTexture.cpp
    src/ui/ImTubeUI.cpp
    ${IMTUBE_IMGUI_CORE_SOURCES}
    ${IMTUBE_IMGUI_BACKEND_SOURCES}
)

if(IMTUBE_BUILD_IMGUI_DEMO)
    target_sources(imtube PRIVATE ${IMTUBE_IMGUI_DEMO_SOURCE})
endif()

target_compile_definitions(imtube PRIVATE
    IMTUBE_VERSION="${PROJECT_VERSION}"
    $<$<BOOL:${IMTUBE_BUILD_IMGUI_DEMO}>:IMTUBE_ENABLE_IMGUI_DEMO>
    $<$<CONFIG:Debug>:IMTUBE_VULKAN_DEBUG>
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
    ${IMTUBE_VULKAN_TARGETS}
    PkgConfig::GSTREAMER
    nlohmann_json::nlohmann_json
    $<$<BOOL:${CURL_FOUND}>:PkgConfig::CURL>
    Threads::Threads
    dl
)
