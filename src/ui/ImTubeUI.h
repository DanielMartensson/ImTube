#pragma once

#include "core/GStreamerPlayer.h"
#include "core/ThumbnailLoader.h"
#include "core/YtDlpHelper.h"
#include "render/VulkanTexture.h"

#include "imgui.h"

#include <atomic>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace imtube {

// The ImTube user interface. Rendered every frame after ImGui::NewFrame().
//
// The search tab drives yt-dlp on a worker thread, thumbnails are downloaded by
// ThumbnailLoader and uploaded into VulkanTexture on the render thread, and
// playback runs through GStreamerPlayer with its newest frame shown as a
// texture. All Vulkan work happens on the render thread only.
class ImTubeUI {
public:
    ImTubeUI() = default;
    ~ImTubeUI();

    void render();

    // Called once from the app once the Vulkan context exists, so that the UI
    // can create CPU->GPU textures.
    void set_gpu(const GpuContext& gpu);

    void set_ytdlp_binary(const std::string& path);
    const std::string& ytdlp_binary() const { return m_ytdlp_binary; }

private:
    enum class Tab { Search, MyLists, Settings };

    void render_menu_bar();
    void render_search_tab();
    void render_lists_tab();
    void render_settings_tab();

    void render_result_card(const VideoItem& video, int index);
    void render_list_row(const VideoItem& video, int index);
    void render_video_view();

    void start_search();
    void finish_search();
    void poll_thumbnails();
    void cleanup_thumb_textures();

    void play_video(const VideoItem& video);
    void stop_video();
    void update_video_frame();

    void ensure_demo_data();

    // --- State ---------------------------------------------------------------
    Tab m_active_tab = Tab::Search;

    // Search tab state
    char m_search_buf[256] = {}; // raw input buffer (no imgui_stdlib in this ImGui build)
    std::string m_last_search;
    std::vector<VideoItem> m_results;
    int m_page = 0;
    bool m_search_requested = false;
    static constexpr int kResultsPerPage = 4;

    // Async yt-dlp search
    YtDlpHelper m_ytdlp;
    std::thread m_search_thread;
    std::atomic<bool> m_searching{false};
    std::atomic<bool> m_search_done{false};
    std::vector<VideoItem> m_search_out;
    std::string m_search_error;

    // Thumbnails (VulkanTexture must only be touched on the render thread)
    ThumbnailLoader m_thumbs;
    std::map<std::string, VulkanTexture> m_thumb_textures;
    GpuContext m_gpu;

    // Video playback
    GStreamerPlayer m_player;
    VulkanTexture m_video_texture;
    bool m_show_player = false;
    bool m_video_failed = false;
    std::string m_now_playing;
    bool m_live_stream = false;

    // My Lists tab state
    std::vector<std::string> m_list_names = { "Navigation History", "Liked", "Watch Later" };
    int m_selected_list = 0;

    // Settings state
    int m_resolution = 360;
    std::string m_ytdlp_binary = "yt-dlp";
    bool m_navigation_history_enabled = true;
    bool m_cache_enabled = true;

    // Placeholder demo data (lists tab / offline fallback)
    std::vector<VideoItem> m_demo_videos;
};

} // namespace imtube
