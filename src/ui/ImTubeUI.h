#pragma once

#include "core/GStreamerPlayer.h"
#include "core/SubtitleParser.h"
#include "core/ThumbnailLoader.h"
#include "core/YtDlpHelper.h"
#include "render/RenderBackend.h"
#include "render/RenderTexture.h"

#include "imgui.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct SDL_Window;

namespace imtube {

/* The ImTube user interface. Rendered every frame after ImGui::NewFrame().
 *
 * The search tab drives yt-dlp on a worker thread, thumbnails are downloaded by
 * ThumbnailLoader and uploaded into RenderTextures on the render thread, and
 * playback runs through GStreamerPlayer with its newest frame shown as a
 * texture. All GPU work happens on the render thread only. */
class ImTubeUI {
public:
    ImTubeUI();
    ~ImTubeUI();

    void render();

    /* Called once from the app once the rendering backend exists, so that the
     * UI can create CPU->GPU textures. */
    void set_backend(RenderBackend* backend);

    /* The SDL window, needed for window controls (minimize/maximize/close). */
    void set_window(SDL_Window* window) { m_window = window; }

    void set_ytdlp_binary(const std::string& path);

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
    void start_load_more();
    void poll_thumbnails();
    void cleanup_thumb_textures();

    void play_video(const VideoItem& video);
    void stop_video();
    void update_video_frame();

    void request_quit();
    void toggle_fullscreen();

    void render_progress_bar();
    void start_seek(int64_t ms);

    /* Subtitles */
    void start_subtitle_fetch(const VideoItem& video);
    void finish_subtitle_fetch();
    void load_subtitle_files(const std::vector<std::string>& files);
    void update_active_subtitle(int64_t pos_ms);

    /* Download */
    void start_download(const VideoItem& video);
    void poll_download();
    void cancel_download();

    void ensure_demo_data();

    void load_saved();
    void save_saved();
    void load_settings();
    void save_settings();

    /* --- State --------------------------------------------------------------- */
    Tab m_active_tab = Tab::Search;

    /* Window controls */
    SDL_Window* m_window = nullptr;

    /* Debug geometry (IMTUBE_DEBUG=1): screen positions of key widgets so the
     * automated UI tests can click the right pixels. */
    struct DebugGeometry
    {
        int win_x = 0, win_y = 0, win_w = 0, win_h = 0; /* window on screen */
        float search_x = 0, search_y = 0, search_w = 0, search_h = 0;
        float search_btn_x = 0, search_btn_y = 0;
        float res_x = 0, res_y = 0; /* first (240p) radio button */
        float results_x = 0, results_y = 0, results_w = 0, results_h = 0;
        float thumb_x = 0, thumb_y = 0, thumb_w = 0, thumb_h = 0; /* first card */
        float bar_y = -1.0f, bar_x0 = 0.0f, bar_w = 0.0f;         /* progress bar (video view) */
        float speed_x = 0.0f, speed_y = 0.0f;                     /* speed combo (video view) */
        float pause_x = 0.0f, pause_y = 0.0f;                     /* pause/resume button */
        bool has_results = false;
        bool in_player = false;
    } m_dbg;

    /* Search tab state */
    char m_search_buf[256] = {}; /* raw input buffer (no imgui_stdlib in this ImGui build) */
    std::string m_last_search;
    std::vector<VideoItem> m_results;
    bool m_search_requested = false;
    static constexpr int kSearchBatchSize = 20;

    /* Async yt-dlp search */
    std::thread m_search_thread;
    std::atomic<bool> m_searching{false};
    std::atomic<bool> m_search_done{false};
    std::vector<VideoItem> m_search_out;
    std::string m_search_error;
    int m_next_fetch_start = 0; /* first index the next "load more" must request */

    /* Thumbnails (RenderTextures must only be touched on the render thread) */
    ThumbnailLoader m_thumbs;
    std::map<std::string, std::unique_ptr<RenderTexture>> m_thumb_textures;
    RenderBackend* m_backend = nullptr;

    /* Video playback */
    GStreamerPlayer m_player;
    std::unique_ptr<RenderTexture> m_video_texture;
    bool m_show_player = false;
    bool m_video_failed = false;
    std::string m_now_playing;
    std::string m_now_playing_id;
    bool m_live_stream = false;
    int m_speed_idx = 2; /* index into kSpeedValues[] ({0.5, 0.75, 1.0, 1.25, 1.5, 2.0}) */

    /* Seeking: scrubbing the progress bar restarts the stream at the target. */
    bool m_seeking = false;
    int64_t m_seek_target_ms = 0;
    std::string m_seek_message; /* "", or a "seek unavailable" error */

    /* Subtitles (a single timed cue list for the current video) */
    std::vector<subtitle::Cue> m_sub_cues;
    std::thread m_sub_thread;
    std::atomic<bool> m_sub_fetching{false};
    std::atomic<bool> m_sub_fetch_done{false};
    std::mutex m_sub_mutex;
    std::vector<std::string> m_sub_files; /* fetched subtitle file paths (worker -> UI) */
    std::string m_sub_target_id;          /* video id the in-flight/finished fetch belongs to */
    bool m_subtitles_enabled = true;
    std::string m_sub_message; /* "", "Loading...", "No subtitles available" */
    std::string m_active_subtitle;

    /* Download (yt-dlp subprocess, progress read from a pipe each frame) */
    int m_download_pid = -1;
    int m_download_fd = -1;
    std::string m_download_buf;
    std::atomic<bool> m_download_active{false};
    double m_download_progress = -1.0; /* -1 = indeterminate, else 0..100 */
    std::string m_download_message;    /* "", "Downloading...", "Saved to <path>", error */
    std::string m_download_path;
    std::string m_download_dir;

    /* My Lists tab state */
    std::vector<std::string> m_list_names = { "Navigation History", "Liked", "Watch Later" };
    int m_selected_list = 0;

    /* Settings state */
    /* Default stream resolution. 1080p is the target quality the project
     * aims for (see README); the user can change it per-session from both
     * the Search tab and the Settings tab. */
    int m_resolution = 1080;
    std::string m_ytdlp_binary = "yt-dlp";
    bool m_ytdlp_version_checked = false;
    std::string m_ytdlp_version;
    bool m_navigation_history_enabled = true;
    bool m_cache_enabled = true;

    /* Persisted liked / watch-later / watched videos (keyed by video id) */
    std::map<std::string, VideoItem> m_saved;

    /* Placeholder demo data (lists tab / offline fallback) */
    std::vector<VideoItem> m_demo_videos;
};

} /* namespace imtube */
