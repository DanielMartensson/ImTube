#include "ui/ImTubeUI.h"

#include "imgui_internal.h"

#include "nlohmann/json.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>

#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace imtube {

namespace {

constexpr ImVec4 kColTextDim  = ImVec4(0.588f, 0.588f, 0.608f, 1.0f);
constexpr ImVec4 kColTextError = ImVec4(1.0f, 0.42f, 0.42f, 1.0f);
constexpr ImU32  kColLive     = IM_COL32(204, 0, 0, 255);
constexpr ImU32  kColThumbBg  = IM_COL32(26, 26, 30, 255);
constexpr ImU32  kColThumbHover = IM_COL32(255, 77, 77, 255);

/* ImGui::Combo() preview getter (this ImGui build only offers the `const char*` variant). */
const char* list_name_getter(void* data, int idx)
{
    auto* names = static_cast<std::vector<std::string>*>(data);
    if (idx < 0 || idx >= (int)names->size())
        return "";
    return (*names)[idx].c_str();
}

std::string watch_url_for(const VideoItem& video)
{
    if (!video.url.empty())
        return video.url;
    return "https://www.youtube.com/watch?v=" + video.id;
}

const char* const kSpeedItems[] = { "0.5x", "0.75x", "1x", "1.25x", "1.5x", "2x" };
const float kSpeedValues[] = { 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
constexpr int kSpeedCount = 6;

std::string format_time_ms(int64_t ms)
{
    if (ms < 0)
        return "0:00";
    const int64_t total = ms / 1000;
    const int64_t h = total / 3600;
    const int64_t m = (total % 3600) / 60;
    const int64_t s = total % 60;
    char buf[32];
    if (h > 0)
        snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", (long long)h, (long long)m, (long long)s);
    else
        snprintf(buf, sizeof buf, "%lld:%02lld", (long long)m, (long long)s);
    return buf;
}

void trim_inplace(std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b]))
        b++;
    while (e > b && std::isspace((unsigned char)s[e - 1]))
        e--;
    s = s.substr(b, e - b);
}

} /* namespace */

ImTubeUI::ImTubeUI()
{
    const char* home = std::getenv("HOME");
    m_download_dir = (home && *home) ? std::string(home) + "/Downloads" : ".";
    load_saved();
}

ImTubeUI::~ImTubeUI()
{
    m_player.stop();
    if (m_search_thread.joinable())
        m_search_thread.join();
    if (m_sub_thread.joinable())
        m_sub_thread.join();
    cancel_download();
    save_saved();
    m_thumbs.shutdown();
}

void ImTubeUI::request_quit()
{
    SDL_Event ev{};
    ev.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&ev);
}

void ImTubeUI::toggle_fullscreen()
{
    if (m_window == nullptr)
        return;
    const bool is_fullscreen = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(m_window, !is_fullscreen);
}

void ImTubeUI::set_backend(RenderBackend* backend)
{
    m_backend = backend;
}

void ImTubeUI::set_ytdlp_binary(const std::string& path)
{
    if (path.empty())
        return;
    m_ytdlp_binary = path;
}

void ImTubeUI::ensure_demo_data()
{
    if (!m_demo_videos.empty())
        return;

    /* Placeholder data used by the lists tab; search results now come from
     * yt-dlp (see start_search()). */
    VideoItem d1;
    d1.id = "d1"; d1.title = "STM32MPU: Getting Started with OpenSTLinux";
    d1.uploader = "STMicroelectronics"; d1.duration = "14:32"; d1.views = "12K views";
    d1.upload_date = "2026-06-12"; d1.channel_id = "ch1"; d1.watched = true;

    VideoItem d2;
    d2.id = "d2"; d2.title = "Dear ImGui + Vulkan: Zero to Hero";
    d2.uploader = "ocornut"; d2.duration = "22:10"; d2.views = "845K views";
    d2.upload_date = "2026-05-30"; d2.channel_id = "ch2"; d2.liked = true; d2.watched = true;

    VideoItem d3;
    d3.id = "d3"; d3.title = "yt-dlp: The Ultimate YouTube Companion";
    d3.uploader = "ytdlp"; d3.duration = "9:41"; d3.views = "1.2M views";
    d3.upload_date = "2026-05-02"; d3.channel_id = "ch3"; d3.watch_later = true;

    VideoItem d4;
    d4.id = "d4"; d4.title = "Building a YouTube Client in C++";
    d4.uploader = "Coding Cave"; d4.duration = "31:05"; d4.views = "210K views";
    d4.upload_date = "2026-04-18"; d4.channel_id = "ch4"; d4.liked = true; d4.watch_later = true; d4.watched = true;

    VideoItem d5;
    d5.id = "d5"; d5.title = "Vulkan 101: Your First Triangle";
    d5.uploader = "vulkan.org"; d5.duration = "18:55"; d5.views = "402K views";
    d5.upload_date = "2026-03-27"; d5.channel_id = "ch5";

    VideoItem d6;
    d6.id = "d6"; d6.title = "SDL3 vs SDL2: What's New";
    d6.uploader = "SDL Developers"; d6.duration = "11:23"; d6.views = "98K views";
    d6.upload_date = "2026-03-09"; d6.channel_id = "ch6"; d6.watched = true;

    VideoItem d7;
    d7.id = "d7"; d7.title = "Embedded Linux Graphics Stack Explained";
    d7.uploader = "OpenSTLinux"; d7.duration = "25:47"; d7.views = "45K views";
    d7.upload_date = "2026-02-14"; d7.channel_id = "ch7";

    VideoItem d8;
    d8.id = "d8"; d8.title = "GStreamer Zero-Copy Pipeline with DMABUF";
    d8.uploader = "GStreamer Team"; d8.duration = "16:12"; d8.views = "67K views";
    d8.upload_date = "2026-02-01"; d8.channel_id = "ch8"; d8.live = true; d8.watched = true;

    VideoItem d9;
    d9.id = "d9"; d9.title = "The C++20 Feature Tour";
    d9.uploader = "cppcon"; d9.duration = "1:04:33"; d9.views = "3.4M views";
    d9.upload_date = "2026-01-20"; d9.channel_id = "ch9"; d9.liked = true;

    VideoItem d10;
    d10.id = "d10"; d10.title = "Wayland vs X11 in 2026";
    d10.uploader = "Linux Desktop"; d10.duration = "13:58"; d10.views = "1.1M views";
    d10.upload_date = "2026-01-05"; d10.channel_id = "ch10";

    m_demo_videos = { d1, d2, d3, d4, d5, d6, d7, d8, d9, d10 };
}

void ImTubeUI::load_saved()
{
    const std::string path = YtDlpHelper::cache_dir() + "/lists.json";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return;
    std::string data;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        data.append(buf, n);
    fclose(f);

    try
    {
        auto j = nlohmann::json::parse(data);
        if (!j.is_array())
            return;
        for (const auto& e : j)
        {
            VideoItem v;
            v.id = e.value("id", std::string());
            v.url = e.value("url", std::string());
            v.title = e.value("title", std::string());
            v.uploader = e.value("uploader", std::string());
            v.upload_date = e.value("upload_date", std::string());
            v.duration = e.value("duration", std::string());
            v.views = e.value("views", std::string());
            v.channel_id = e.value("channel_id", std::string());
            v.thumbnail_url = e.value("thumbnail_url", std::string());
            v.live = e.value("live", false);
            v.duration_seconds = e.value("duration_seconds", (int64_t)0);
            v.view_count = e.value("view_count", (int64_t)0);
            v.liked = e.value("liked", false);
            v.watch_later = e.value("watch_later", false);
            v.watched = e.value("watched", false);
            v.saved_at = e.value("saved_at", (int64_t)0);
            if (!v.id.empty())
                m_saved[v.id] = std::move(v);
        }
    }
    catch (const std::exception&)
    {
        m_saved.clear();
    }
}

void ImTubeUI::save_saved()
{
    try
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& kv : m_saved)
        {
            const VideoItem& v = kv.second;
            arr.push_back({
                { "id", v.id },
                { "url", v.url },
                { "title", v.title },
                { "uploader", v.uploader },
                { "upload_date", v.upload_date },
                { "duration", v.duration },
                { "views", v.views },
                { "channel_id", v.channel_id },
                { "thumbnail_url", v.thumbnail_url },
                { "live", v.live },
                { "duration_seconds", v.duration_seconds },
                { "view_count", v.view_count },
                { "liked", v.liked },
                { "watch_later", v.watch_later },
                { "watched", v.watched },
                { "saved_at", v.saved_at },
            });
        }

        const std::string path = YtDlpHelper::cache_dir() + "/lists.json";
        const std::string tmp = path + ".tmp";
        FILE* f = fopen(tmp.c_str(), "wb");
        if (!f)
            return;
        const std::string dump = arr.dump();
        const bool ok = fwrite(dump.data(), 1, dump.size(), f) == dump.size();
        fclose(f);
        if (ok)
            rename(tmp.c_str(), path.c_str());
        else
            remove(tmp.c_str());
    }
    catch (const std::exception&)
    {
    }
}

void ImTubeUI::render()
{
    ensure_demo_data();

    /* Handle finished background work (search results, decoded thumbnails,
     * subtitles, download subprocess output). */
    finish_search();
    poll_thumbnails();
    finish_subtitle_fetch();
    poll_download();

    /* --- Keyboard shortcuts --------------------------------------------------- */
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_F11, false))
        toggle_fullscreen();
    /* Esc leaves fullscreen mode (only meaningful while fullscreen). */
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
        (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN))
        SDL_SetWindowFullscreen(m_window, false);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q, false))
        request_quit();

    /* --- Menu bar (tabs) ---------------------------------------------------- */
    const bool fullscreen = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (!fullscreen)
        render_menu_bar();
    const float menubar_height = fullscreen ? 0.0f : ImGui::GetFrameHeight();

    /* --- Main window filling the viewport below the menu bar ----------------- */
    ImGui::SetNextWindowPos(ImVec2(0.0f, menubar_height));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - menubar_height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##ImTubeMain", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking);

    if (m_show_player)
    {
        render_video_view();
    }
    else
    {
        m_dbg.in_player = false;
        switch (m_active_tab)
        {
            case Tab::Search:   render_search_tab();   break;
            case Tab::MyLists:  render_lists_tab();    break;
            case Tab::Settings: render_settings_tab(); break;
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();

    /* Debug geometry dump for the automated UI tests (IMTUBE_DEBUG=1). */
    static int dump_counter = 0;
    static const bool debug_geom = std::getenv("IMTUBE_DEBUG") != nullptr;
    if (debug_geom && (++dump_counter % 45) == 0)
    {
        int win_x = 0, win_y = 0, win_w = 0, win_h = 0;
        if (m_window != nullptr)
        {
            SDL_GetWindowPosition(m_window, &win_x, &win_y);
            SDL_GetWindowSize(m_window, &win_w, &win_h);
        }
        fprintf(stderr,
                "[geo] win=%d,%d %dx%d search=(%.0f,%.0f) %.0fx%.0f btn=(%.0f,%.0f) "
                "res=(%.0f,%.0f) results=(%.0f,%.0f) %.0fx%.0f",
                win_x, win_y, win_w, win_h,
                win_x + m_dbg.search_x, win_y + m_dbg.search_y, m_dbg.search_w, m_dbg.search_h,
                win_x + m_dbg.search_btn_x, win_y + m_dbg.search_btn_y,
                win_x + m_dbg.res_x, win_y + m_dbg.res_y,
                win_x + m_dbg.results_x, win_y + m_dbg.results_y,
                m_dbg.results_w, m_dbg.results_h);
        if (m_dbg.has_results)
            fprintf(stderr, " thumb=(%.0f,%.0f) %.0fx%.0f",
                    win_x + m_dbg.thumb_x, win_y + m_dbg.thumb_y, m_dbg.thumb_w, m_dbg.thumb_h);
        if (m_dbg.in_player)
            fprintf(stderr, " bar_y=%.0f bar_x0=%.0f bar_w=%.0f speed=(%.0f,%.0f) pause=(%.0f,%.0f)",
                    win_y + m_dbg.bar_y, win_x + m_dbg.bar_x0, m_dbg.bar_w,
                    win_x + m_dbg.speed_x, win_y + m_dbg.speed_y,
                    win_x + m_dbg.pause_x, win_y + m_dbg.pause_y);
        for (int pi = 0; pi < GImGui->Windows.Size; pi++)
        {
            ImGuiWindow* pw = GImGui->Windows[pi];
            if ((pw->Flags & ImGuiWindowFlags_Popup) && pw->WasActive)
                fprintf(stderr, " popup='%s' (%d,%d)+(%dx%d)",
                        pw->Name, (int)(win_x + pw->Pos.x), (int)(win_y + pw->Pos.y),
                        (int)pw->Size.x, (int)pw->Size.y);
        }
        fprintf(stderr, " state=tab%d searching=%d results=%zu player=%d res=%d err=%s q=[%s]\n",
                (int)m_active_tab, (int)m_searching, m_results.size(),
                (int)m_show_player, m_resolution, m_search_error.c_str(), m_search_buf);
    }
}

void ImTubeUI::render_menu_bar()
{
    if (ImGui::BeginMainMenuBar())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 77, 77, 255));
        ImGui::Text("ImTube");
        ImGui::PopStyleColor();
        ImGui::Separator();

        const bool player_shown = m_show_player;
        if (ImGui::MenuItem("Search", "Alt+S", m_active_tab == Tab::Search) && !player_shown)
            m_active_tab = Tab::Search;
        if (ImGui::MenuItem("My Lists", "Alt+L", m_active_tab == Tab::MyLists) && !player_shown)
            m_active_tab = Tab::MyLists;
        if (ImGui::MenuItem("Settings", "Alt+T", m_active_tab == Tab::Settings) && !player_shown)
            m_active_tab = Tab::Settings;

        ImGui::EndMainMenuBar();
    }
}

void ImTubeUI::start_search()
{
    /* Join any in-flight search before launching a new one. */
    if (m_searching)
    {
        if (m_search_thread.joinable())
            m_search_thread.join();
    }

    const std::string query = m_search_buf;
    const std::string binary = m_ytdlp_binary;
    m_last_search = query;
    m_results.clear();
    m_next_fetch_start = 0;
    m_search_error.clear();
    m_search_done = false;
    m_searching = true;

    m_search_thread = std::thread([this, query, binary] {
        YtDlpHelper helper(binary);
        std::vector<VideoItem> out;
        std::string err;

        if (YtDlpHelper::is_youtube_url(query))
        {
            VideoItem single;
            if (!helper.search_url(query, single))
                err = "yt-dlp could not extract that URL.";
            else
                out.push_back(single);
        }
        else
        {
            if (!helper.search(query, out, kSearchBatchSize, 0))
                err = "yt-dlp search failed. Is yt-dlp installed?";
        }

        m_search_out = std::move(out);
        m_search_error = err;
        m_search_done = true;
        m_searching = false;
    });
}

void ImTubeUI::start_load_more()
{
    if (m_searching || m_search_done || m_last_search.empty())
        return;
    if (YtDlpHelper::is_youtube_url(m_last_search))
        return; /* single-video results have nothing more to load */

    const int start = (int)m_results.size();
    if (start <= m_next_fetch_start)
        return; /* results did not grow since the last fetch (end of results) */

    m_next_fetch_start = start;
    const int end = start + kSearchBatchSize;
    const std::string query = m_last_search;
    const std::string binary = m_ytdlp_binary;
    m_search_done = false;
    m_searching = true;

    m_search_thread = std::thread([this, query, binary, start, end] {
        YtDlpHelper helper(binary);
        std::vector<VideoItem> out;
        std::string err;
        if (!helper.search(query, out, end, start))
            err = "yt-dlp search failed. Is yt-dlp installed?";
        m_search_out = std::move(out);
        m_search_error = err;
        m_search_done = true;
        m_searching = false;
    });
}

void ImTubeUI::finish_search()
{
    if (!m_search_done.exchange(false))
        return;

    /* Append the fetched batch (a fresh search cleared m_results first). */
    std::set<std::string> seen;
    for (const VideoItem& v : m_results)
        seen.insert(v.id);
    for (VideoItem& v : m_search_out)
    {
        if (!seen.insert(v.id).second)
            continue; /* skip duplicates across pages */
        /* Merge persisted bookkeeping (liked / watch-later / watched). */
        auto it = m_saved.find(v.id);
        if (it != m_saved.end())
        {
            v.liked = it->second.liked;
            v.watch_later = it->second.watch_later;
            v.watched = it->second.watched;
        }
        m_results.push_back(std::move(v));
    }
    m_search_out.clear();

    cleanup_thumb_textures();
    m_thumbs.request_thumbnails(m_results);

    if (m_search_thread.joinable())
        m_search_thread.join();
}

void ImTubeUI::poll_thumbnails()
{
    if (m_backend == nullptr)
        return;

    DecodedThumbnail thumb;
    while (m_thumbs.poll(thumb))
    {
        auto it = m_thumb_textures.find(thumb.video_id);
        if (it != m_thumb_textures.end())
        {
            if (it->second->width() == thumb.width && it->second->height() == thumb.height)
                it->second->upload(thumb.rgba.data());
            continue;
        }

        auto tex = m_backend->create_texture(thumb.width, thumb.height);
        if (!tex || !tex->valid())
            continue;
        tex->upload(thumb.rgba.data());
        m_thumb_textures.emplace(thumb.video_id, std::move(tex));
    }
}

void ImTubeUI::cleanup_thumb_textures()
{
    std::set<std::string> keep;
    for (const VideoItem& v : m_results)
        keep.insert(v.id);

    for (auto it = m_thumb_textures.begin(); it != m_thumb_textures.end();)
    {
        if (keep.count(it->first) == 0)
            it = m_thumb_textures.erase(it);
        else
            ++it;
    }
}

void ImTubeUI::play_video(const VideoItem& video)
{
    stop_video();
    m_now_playing = video.title;
    m_now_playing_id = video.id;
    m_live_stream = video.live;
    m_speed_idx = 2; /* reset to 1x on a new video */
    m_seek_message.clear();

    const std::string url = watch_url_for(video);
    m_player.set_known_duration_ms(video.duration_seconds > 0 ? video.duration_seconds * 1000 : 0);
    if (!m_player.start(url, video.live, m_resolution, m_ytdlp_binary))
    {
        m_video_failed = true;
        return;
    }

    m_show_player = true;
    m_video_failed = false;

    /* Mark as watched everywhere it appears (results + saved lists). */
    auto mark_watched = [&](std::vector<VideoItem>& items) {
        for (VideoItem& v : items)
            if (v.id == video.id)
                v.watched = true;
    };
    mark_watched(m_results);

    auto it = m_saved.find(video.id);
    if (it == m_saved.end())
    {
        m_saved[video.id] = video;
        it = m_saved.find(video.id);
    }
    it->second.watched = true;
    it->second.saved_at = (int64_t)time(nullptr);
    save_saved();

    /* Fetch subtitles for the video in the background. */
    start_subtitle_fetch(video);
}

void ImTubeUI::stop_video()
{
    m_show_player = false;
    m_video_failed = false;
    m_now_playing.clear();
    m_now_playing_id.clear();
    m_live_stream = false;
    m_active_subtitle.clear();
    m_seek_message.clear();
    m_seeking = false;
    m_player.stop();
    m_video_texture.reset();
}

void ImTubeUI::update_video_frame()
{
    m_player.pump_bus();

    if (!m_show_player)
        return;

    if (m_player.is_playing() && m_player.pull_frame())
    {
        const int w = m_player.frame_width();
        const int h = m_player.frame_height();
        if (w > 0 && h > 0 &&
            (!m_video_texture ||
             m_video_texture->width() != w || m_video_texture->height() != h))
        {
            m_video_texture.reset();
        }
        if (!m_video_texture && m_backend != nullptr)
        {
            m_video_texture = m_backend->create_texture(w, h);
            if (!m_video_texture || !m_video_texture->valid())
                return;
        }
        if (m_video_texture)
            m_video_texture->upload(m_player.frame_pixels());
    }
}

void ImTubeUI::render_video_view()
{
    update_video_frame();

    const bool fullscreen = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
    const bool chrome = !fullscreen;

    int64_t pos_ms = -1, dur_ms = -1;
    m_player.get_position_and_duration(&pos_ms, &dur_ms);
    if (pos_ms >= 0)
        update_active_subtitle(pos_ms);

    /* Re-apply the chosen playback rate after a seek or a new video (the
     * pipeline restarts at 1x). Only once the position is known, so the
     * demuxer's initial 1x segment has already been replaced downstream. */
    if (pos_ms >= 0 && m_player.is_playing() &&
        m_player.playback_speed() != kSpeedValues[m_speed_idx])
        m_player.set_playback_speed(kSpeedValues[m_speed_idx]);

    /* In fullscreen the video covers the whole window: no control rows, no
     * progress bar, no menu bar (the menu bar is skipped in render()). */
    const float controls_height = chrome ? ImGui::GetFrameHeightWithSpacing() * 4 + 24.0f : 0.0f;
    ImGui::BeginChild("##video_area", ImVec2(0.0f, -controls_height));

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 img_size = avail;
    if (m_video_texture && m_video_texture->valid())
    {
        const float src_aspect = (float)m_video_texture->width() / (float)m_video_texture->height();
        const float dst_aspect = avail.x / avail.y;
        if (src_aspect > dst_aspect)
            img_size.y = avail.x / src_aspect;
        else
            img_size.x = avail.y * src_aspect;
        if (img_size.x > avail.x) img_size.x = avail.x;
        if (img_size.y > avail.y) img_size.y = avail.y;
    }

    ImGui::SetCursorPos(ImVec2((avail.x - img_size.x) * 0.5f, (avail.y - img_size.y) * 0.5f));

    if (m_video_texture && m_video_texture->valid())
        ImGui::Image(m_video_texture->imgui_id(), img_size);
    else if (m_player.has_eos())
        ImGui::TextColored(kColTextDim, "Playback ended.");
    else if (m_video_failed)
        ImGui::TextColored(kColTextDim, "Playback failed: %s", m_player.error().c_str());
    else if (m_seeking)
        ImGui::TextColored(kColTextDim, "Seeking...");
    else if (!m_seek_message.empty())
        ImGui::TextColored(kColTextDim, "%s", m_seek_message.c_str());
    else
        ImGui::TextColored(kColTextDim, "Starting playback...");

    /* YouTube-style red progress bar at the bottom edge of the video area. */
    if (chrome)
        render_progress_bar();

    /* Subtitles overlay (centered above the progress bar). */
    if (!m_active_subtitle.empty())
    {
        const float wrap = std::min(avail.x - 40.0f, 900.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 ts = ImGui::CalcTextSize(m_active_subtitle.c_str(), nullptr, false, wrap);
        const ImVec2 rmin = ImGui::GetWindowPos();
        const float rmax_x = rmin.x + ImGui::GetWindowSize().x;
        const float rmax_y = rmin.y + ImGui::GetWindowSize().y;
        const ImVec2 pos(rmin.x + (rmax_x - rmin.x - ts.x) * 0.5f, rmax_y - 30.0f - ts.y);
        dl->AddRectFilled(ImVec2(pos.x - 8.0f, pos.y - 3.0f),
                          ImVec2(pos.x + ts.x + 8.0f, pos.y + ts.y + 3.0f), IM_COL32(0, 0, 0, 170));
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), pos, IM_COL32(255, 255, 255, 255),
                    m_active_subtitle.c_str(), nullptr, wrap);
    }

    ImGui::EndChild();

    if (!chrome)
        return;

    /* --- Row 1: transport + title --------------------------------------------- */
    if (ImGui::Button(m_player.is_paused() ? "Resume" : "Pause", ImVec2(76, 0)))
        m_player.toggle_pause();
    const ImVec2 pause_min = ImGui::GetItemRectMin();
    m_dbg.pause_x = pause_min.x;
    m_dbg.pause_y = pause_min.y;
    ImGui::SameLine();
    if (ImGui::Button(m_player.has_eos() ? "Replay" : "Stop", ImVec2(76, 0)))
    {
        if (m_player.has_eos())
        {
            auto it = m_saved.find(m_now_playing_id);
            if (it != m_saved.end())
                play_video(it->second);
        }
        else
        {
            stop_video();
            return;
        }
    }
    ImGui::SameLine();
    const bool fullscreen_active = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (ImGui::Button("Expand", ImVec2(76, 0)))
        toggle_fullscreen();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(fullscreen_active ? "Exit fullscreen (Esc)"
                                            : "Enter fullscreen (F11)");
    ImGui::SameLine();
    if (ImGui::Button(m_player.is_muted() ? "Unmute" : "Mute", ImVec2(64, 0)))
        m_player.toggle_mute();
    ImGui::SameLine();
    float vol_pct = m_player.volume() * 100.0f;
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::SliderFloat("##volume", &vol_pct, 0.0f, 100.0f, "Vol: %.0f%%"))
        m_player.set_volume(vol_pct / 100.0f);
    ImGui::SameLine();
    ImGui::TextColored(kColTextDim, "%s%s", m_now_playing.c_str(),
                       m_live_stream ? "   [LIVE]" : "");

    /* --- Row 2: speed / subtitles / lists ------------------------------------- */
    ImGui::SetNextItemWidth(64.0f);
    if (ImGui::Combo("##speed", &m_speed_idx, kSpeedItems, kSpeedCount))
    {
        if (!m_player.set_playback_speed(kSpeedValues[m_speed_idx]))
            m_speed_idx = 2;
    }
    const ImVec2 speed_min = ImGui::GetItemRectMin();
    m_dbg.speed_x = speed_min.x;
    m_dbg.speed_y = speed_min.y;
    ImGui::SameLine();
    ImGui::TextColored(kColTextDim, "Speed");
    ImGui::SameLine();

    if (ImGui::Checkbox("Subtitles", &m_subtitles_enabled))
    {
        if (!m_subtitles_enabled)
            m_active_subtitle.clear();
    }
    ImGui::SameLine();
    ImGui::TextColored(kColTextDim, "Resolution: %dp", m_resolution);

    /* --- Row 3: download -------------------------------------------------------- */
    if (m_download_active)
    {
        if (ImGui::Button("Cancel", ImVec2(76, 0)))
            cancel_download();
        ImGui::SameLine();
        if (m_download_progress >= 0.0)
            ImGui::ProgressBar((float)(m_download_progress / 100.0), ImVec2(200.0f, 0), "");
        else
            ImGui::TextColored(kColTextDim, "Downloading...");
        ImGui::SameLine();
    }
    else if (!m_now_playing_id.empty())
    {
        if (ImGui::Button("Download", ImVec2(76, 0)))
        {
            auto it = m_saved.find(m_now_playing_id);
            if (it != m_saved.end())
                start_download(it->second);
        }
        ImGui::SameLine();
    }
    else
    {
        ImGui::SameLine();
    }
    if (!m_download_message.empty())
        ImGui::TextColored(kColTextDim, "%s", m_download_message.c_str());
    ImGui::SameLine();
    ImGui::TextColored(kColTextDim, "Folder: %s", m_download_dir.c_str());

    /* --- Row 4: like / later / error ------------------------------------------- */
    const bool has_id = !m_now_playing_id.empty();
    bool liked = false, later = false;
    if (has_id)
    {
        auto sit = m_saved.find(m_now_playing_id);
        if (sit != m_saved.end())
        {
            liked = sit->second.liked;
            later = sit->second.watch_later;
        }
    }
    ImGui::BeginDisabled(!has_id);
    if (ImGui::Checkbox("Like", &liked))
    {
        if (m_saved[m_now_playing_id].liked != liked)
        {
            m_saved[m_now_playing_id].liked = liked;
            save_saved();
        }
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Watch Later", &later))
    {
        if (m_saved[m_now_playing_id].watch_later != later)
        {
            m_saved[m_now_playing_id].watch_later = later;
            save_saved();
        }
    }
    ImGui::EndDisabled();

    /* Subtitle status message (bottom of the controls, under Like / Watch Later). */
    if (m_sub_fetching)
        ImGui::TextColored(kColTextDim, "Loading subs...");
    else if (!m_sub_message.empty())
        ImGui::TextColored(kColTextDim, "%s", m_sub_message.c_str());

    const std::string err = m_player.error();
    if (!err.empty())
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", err.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Dismiss"))
            stop_video();
    }
}

void ImTubeUI::render_progress_bar()
{
    int64_t pos_ms = -1, dur_ms = -1;
    const bool have = m_player.get_position_and_duration(&pos_ms, &dur_ms);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 rmin = ImGui::GetWindowPos();
    const float width = ImGui::GetWindowSize().x;
    const float rmax_y = rmin.y + ImGui::GetWindowSize().y;
    const float bar_h = 3.0f;
    const float bar_y = rmax_y - bar_h - 1.0f;
    const float hit_h = 9.0f; /* invisible click/drag target around the thin bar */

    float f = 0.0f;
    if (have && dur_ms > 0 && pos_ms >= 0)
    {
        f = (float)((double)pos_ms / dur_ms);
        if (f > 1.0f)
            f = 1.0f;
    }

    dl->AddRectFilled(ImVec2(rmin.x, bar_y), ImVec2(rmin.x + width, bar_y + bar_h),
                      IM_COL32(60, 60, 65, 255));
    if (f > 0.0f)
        dl->AddRectFilled(ImVec2(rmin.x, bar_y), ImVec2(rmin.x + f * width, bar_y + bar_h),
                          IM_COL32(230, 33, 23, 255));

    static int dbg_frame = 0;
    if ((++dbg_frame % 30) == 0 && std::getenv("IMTUBE_DEBUG") != nullptr)
        fprintf(stderr, "[ui] seek bar: winPos=(%.0f,%.0f) size=(%.0f,%.0f) bar_y=%.1f pos=%lld dur=%lld\n",
                rmin.x, rmin.y, ImGui::GetWindowSize().x, ImGui::GetWindowSize().y,
                bar_y, (long long)pos_ms, (long long)dur_ms);
    m_dbg.bar_y = bar_y;
    m_dbg.bar_x0 = rmin.x;
    m_dbg.bar_w = width;
    m_dbg.in_player = true;

    /* Drag/click target: drag to scrub, release to jump. The 9px hit target is
     * centered on the 3px bar; clamp it inside the window so its bottom never
     * passes the bottom edge (that would grow the content rect and pop in a
     * vertical scrollbar over the video area). */
    ImGui::SetCursorScreenPos(ImVec2(rmin.x, rmax_y - hit_h - 1.0f));
    ImGui::InvisibleButton("##seek_bar", ImVec2(width, hit_h));

    const bool seekable = have && dur_ms > 0 && !m_live_stream &&
                          m_player.is_playing() && !m_seeking;
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (hovered || active)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    if ((hovered || active) && dur_ms > 0)
    {
        float hf = (ImGui::GetIO().MousePos.x - rmin.x) / width;
        if (hf < 0.0f) hf = 0.0f;
        if (hf > 1.0f) hf = 1.0f;
        if (seekable)
        {
            m_seek_target_ms = (int64_t)(hf * dur_ms);
            ImGui::SetTooltip("Jump to %s", format_time_ms(m_seek_target_ms).c_str());
        }
        else
        {
            ImGui::SetTooltip("%s", format_time_ms((int64_t)(hf * dur_ms)).c_str());
        }
    }

    if (seekable && ImGui::IsItemDeactivated())
        start_seek(m_seek_target_ms);

    if (have && pos_ms >= 0)
    {
        std::string t = format_time_ms(pos_ms);
        if (dur_ms > 0)
            t += " / " + format_time_ms(dur_ms);
        dl->AddText(ImVec2(rmin.x + 8.0f, bar_y - 18.0f), IM_COL32(255, 255, 255, 200), t.c_str());
    }
}

void ImTubeUI::start_seek(int64_t ms)
{
    if (m_seeking || m_live_stream)
        return;

    m_seeking = true;
    m_video_texture.reset(); /* drop the stale frame from the old stream */
    const bool ok = m_player.seek_to_ms(ms);
    m_seeking = false;

    if (!ok)
    {
        m_seek_message = "Seeking is not available for this video (no single-file stream).";
        m_video_failed = true;
    }
    else
    {
        m_seek_message.clear();
        m_video_failed = false;
    }
}

/* ---------------------------------------------------------------------------
 * Subtitles
 * --------------------------------------------------------------------------- */

void ImTubeUI::start_subtitle_fetch(const VideoItem& video)
{
    /* Drop any stale cues from a previous video immediately. */
    m_sub_cues.clear();
    m_active_subtitle.clear();
    m_sub_message = "Loading subtitles...";

    /* Join a fetch that already finished but was not drained yet. */
    if (m_sub_fetch_done.exchange(false))
    {
        if (m_sub_thread.joinable())
            m_sub_thread.join();
    }
    /* An older fetch is still running for a different video: it cannot be
     * cancelled; its results are discarded by the target-id check in
     * finish_subtitle_fetch(). */
    if (m_sub_fetching)
    {
        m_sub_message = "Subtitle fetch skipped for this video.";
        return;
    }

    m_sub_target_id = video.id;
    m_sub_fetching = true;

    const std::string url = watch_url_for(video);
    const std::string binary = m_ytdlp_binary;
    const std::string vid = video.id;

    m_sub_thread = std::thread([this, url, binary, vid] {
        YtDlpHelper helper(binary);
        std::vector<std::string> files = helper.fetch_subtitles(url, vid);
        {
            std::lock_guard<std::mutex> lk(m_sub_mutex);
            m_sub_files = std::move(files);
        }
        m_sub_fetching = false;
        m_sub_fetch_done = true;
    });
}

void ImTubeUI::finish_subtitle_fetch()
{
    if (!m_sub_fetch_done.exchange(false))
        return;

    if (m_sub_thread.joinable())
        m_sub_thread.join();

    /* The fetch belongs to a video the user already navigated away from. */
    if (m_sub_target_id != m_now_playing_id)
    {
        m_sub_message.clear();
        return;
    }

    std::vector<std::string> files;
    {
        std::lock_guard<std::mutex> lk(m_sub_mutex);
        files = std::move(m_sub_files);
    }
    load_subtitle_files(files);
}

void ImTubeUI::load_subtitle_files(const std::vector<std::string>& files)
{
    m_sub_cues.clear();
    for (const std::string& f : files)
    {
        std::vector<subtitle::Cue> cues = subtitle::parse_file(f);
        m_sub_cues.insert(m_sub_cues.end(), cues.begin(), cues.end());
    }
    if (m_sub_cues.empty())
        m_sub_message = "No subtitles available";
    else
        m_sub_message.clear();
}

void ImTubeUI::update_active_subtitle(int64_t pos_ms)
{
    if (!m_subtitles_enabled || m_sub_cues.empty())
    {
        m_active_subtitle.clear();
        return;
    }
    const double t = pos_ms / 1000.0;
    m_active_subtitle.clear();
    if (const subtitle::Cue* cue = subtitle::cue_at(m_sub_cues, t))
        m_active_subtitle = cue->text;
}

/* ---------------------------------------------------------------------------
 * Download
 * --------------------------------------------------------------------------- */

void ImTubeUI::start_download(const VideoItem& video)
{
    if (m_download_active)
        cancel_download();

    if (mkdir(m_download_dir.c_str(), 0755) != 0 && errno != EEXIST)
    {
        m_download_message = "Cannot create download folder: " + m_download_dir;
        return;
    }

    const std::string url = watch_url_for(video);
    YtDlpHelper helper(m_ytdlp_binary);
    int pid = -1;
    const int fd = helper.launch_download(url, m_resolution, m_download_dir, &pid);
    if (fd < 0)
    {
        m_download_message = "Failed to start download (is yt-dlp installed?).";
        return;
    }

    /* The pipe is drained each frame; read must not block the UI thread. */
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

    m_download_pid = pid;
    m_download_fd = fd;
    m_download_buf.clear();
    m_download_progress = -1.0;
    m_download_path = m_download_dir + "/" + video.id + ".mp4";
    m_download_message = "Downloading...";
    m_download_active = true;
}

void ImTubeUI::poll_download()
{
    if (!m_download_active || m_download_fd < 0)
        return;

    char tmp[4096];
    ssize_t n;
    while ((n = read(m_download_fd, tmp, sizeof tmp - 1)) > 0)
    {
        tmp[n] = '\0';
        m_download_buf += tmp;
        if (m_download_buf.size() > 65536)
            m_download_buf.erase(0, m_download_buf.size() - 65536);

        size_t nl;
        while ((nl = m_download_buf.find('\n')) != std::string::npos)
        {
            std::string line = m_download_buf.substr(0, nl);
            m_download_buf.erase(0, nl + 1);
            trim_inplace(line);

            /* "[download]  42.3% of 100.00MiB at 2.13MiB/s ETA 00:42" */
            if (line.rfind("[download]", 0) == 0)
            {
                const size_t pct = line.find('%');
                if (pct != std::string::npos)
                {
                    size_t begin = pct;
                    while (begin > 0 && (isdigit((unsigned char)line[begin - 1]) || line[begin - 1] == '.'))
                        begin--;
                    m_download_progress = atof(line.substr(begin, pct - begin).c_str());
                }
            }
        }
    }

    if (n == 0)
    {
        /* EOF: the yt-dlp child has exited. */
        close(m_download_fd);
        m_download_fd = -1;
        int status = 0;
        waitpid(m_download_pid, &status, 0);
        m_download_pid = -1;
        m_download_active = false;

        const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        std::string saved = m_download_path;
        if (access(saved.c_str(), R_OK) != 0)
        {
            const std::string webm =
                m_download_path.substr(0, m_download_path.size() - 4) + ".webm";
            if (access(webm.c_str(), R_OK) == 0)
                saved = webm;
        }
        if (ok && access(saved.c_str(), R_OK) == 0)
        {
            m_download_progress = 100.0;
            m_download_message = "Saved to " + saved;
        }
        else
        {
            m_download_progress = -1.0;
            m_download_message = "Download failed. Check the yt-dlp output.";
        }
    }
    else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
    {
        close(m_download_fd);
        m_download_fd = -1;
        waitpid(m_download_pid, nullptr, 0);
        m_download_pid = -1;
        m_download_active = false;
        m_download_progress = -1.0;
        m_download_message = "Download failed (I/O error).";
    }
}

void ImTubeUI::cancel_download()
{
    if (m_download_pid > 0)
        kill(m_download_pid, SIGTERM);
    if (m_download_fd >= 0)
    {
        close(m_download_fd);
        m_download_fd = -1;
    }
    if (m_download_pid > 0)
        waitpid(m_download_pid, nullptr, 0);
    m_download_pid = -1;
    m_download_active = false;
    m_download_progress = -1.0;
    m_download_message.clear();
}

void ImTubeUI::render_search_tab()
{
    /* --- Search bar ---------------------------------------------------------- */
    ImGui::SetNextItemWidth(-86.0f);
    if (ImGui::InputTextWithHint("##search_input", "Search videos or paste a YouTube URL",
                                 m_search_buf, sizeof(m_search_buf), ImGuiInputTextFlags_EnterReturnsTrue))
        m_search_requested = true;
    const ImVec2 si_min = ImGui::GetItemRectMin();
    const ImVec2 si_max = ImGui::GetItemRectMax();
    m_dbg.search_x = si_min.x;
    m_dbg.search_y = si_min.y;
    m_dbg.search_w = si_max.x - si_min.x;
    m_dbg.search_h = si_max.y - si_min.y;
    ImGui::SameLine();
    if (ImGui::Button("Search", ImVec2(78, 0)))
        m_search_requested = true;
    const ImVec2 sb_min = ImGui::GetItemRectMin();
    m_dbg.search_btn_x = sb_min.x;
    m_dbg.search_btn_y = sb_min.y;

    if (m_search_requested)
    {
        m_search_requested = false;
        start_search();
    }

    ImGui::Separator();

    /* --- Results -------------------------------------------------------------- */
    ImGui::BeginChild("##results", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    const ImVec2 res_min = ImGui::GetWindowPos();
    const ImVec2 res_size = ImGui::GetWindowSize();
    m_dbg.results_x = res_min.x;
    m_dbg.results_y = res_min.y;
    m_dbg.results_w = res_size.x;
    m_dbg.results_h = res_size.y;
    m_dbg.has_results = false;

    if (m_searching && m_results.empty())
    {
        ImGui::TextColored(kColTextDim, "Searching...");
    }
    else if (!m_search_error.empty() && m_results.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_search_error.c_str());
        ImGui::TextColored(kColTextDim, "Search for videos or paste a YouTube URL above.");
    }
    else if (m_results.empty())
    {
        if (m_last_search.empty())
            ImGui::TextColored(kColTextDim, "Search for videos or paste a YouTube URL above.");
        else
            ImGui::TextColored(kColTextDim, "No results for '%s'.", m_last_search.c_str());
    }
    else
    {
        for (int i = 0; i < (int)m_results.size(); i++)
        {
            render_result_card(m_results[i], i);
            if (i + 1 < (int)m_results.size())
                ImGui::Separator();
        }

        /* Infinite scroll: fetch the next page when the user nears the bottom. */
        if (m_searching)
            ImGui::TextColored(kColTextDim, "Loading more...");
        else if (ImGui::GetScrollMaxY() > 0.0f &&
                 ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40.0f)
            start_load_more();
    }

    ImGui::EndChild();
}

void ImTubeUI::render_result_card(const VideoItem& video, int index)
{
    ImGui::PushID(index);

    const ImVec2 thumb_size(128.0f, 72.0f);

    /* Make the thumbnail area a button that starts playback. */
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, kColThumbBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kColThumbBg);
    if (ImGui::Button("##thumb_btn", thumb_size))
        play_video(m_results[index]);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    const ImVec2 thumb_tl = ImGui::GetItemRectMin();
    const ImVec2 thumb_br = ImGui::GetItemRectMax();
    if (index == 0)
    {
        m_dbg.has_results = true;
        m_dbg.thumb_x = thumb_tl.x;
        m_dbg.thumb_y = thumb_tl.y;
        m_dbg.thumb_w = thumb_br.x - thumb_tl.x;
        m_dbg.thumb_h = thumb_br.y - thumb_tl.y;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();

    /* Thumbnail image (aspect matches 16:9 thumbnails), if available. */
    const auto tex_it = m_thumb_textures.find(video.id);
    if (tex_it != m_thumb_textures.end() && tex_it->second->valid())
    {
        dl->AddImage(tex_it->second->imgui_id(),
                     thumb_tl, thumb_br,
                     ImVec2(0, 0), ImVec2(1, 1));
        if (ImGui::IsItemHovered())
            dl->AddRect(thumb_tl, thumb_br, kColThumbHover);
    }

    /* Duration overlay (bottom-right) */
    if (!video.duration.empty())
    {
        const ImVec2 ts = ImGui::CalcTextSize(video.duration.c_str());
        dl->AddRectFilled(ImVec2(thumb_br.x - ts.x - 10.0f, thumb_br.y - 18.0f),
                          ImVec2(thumb_br.x - 3.0f, thumb_br.y - 3.0f), IM_COL32(0, 0, 0, 210));
        dl->AddText(ImVec2(thumb_br.x - ts.x - 5.0f, thumb_br.y - 16.0f),
                    IM_COL32(255, 255, 255, 255), video.duration.c_str());
    }

    /* LIVE badge (top-left) */
    if (video.live)
    {
        const char* live = "LIVE";
        const ImVec2 ls = ImGui::CalcTextSize(live);
        dl->AddRectFilled(ImVec2(thumb_tl.x + 3.0f, thumb_tl.y + 3.0f),
                          ImVec2(thumb_tl.x + 3.0f + ls.x + 8.0f, thumb_tl.y + 3.0f + ls.y + 5.0f), kColLive);
        dl->AddText(ImVec2(thumb_tl.x + 7.0f, thumb_tl.y + 5.0f), IM_COL32(255, 255, 255, 255), live);
    }

    ImGui::SameLine();

    /* --- Text column ---------------------------------------------------------- */
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(video.title.c_str());
    ImGui::PopTextWrapPos();
    ImGui::TextColored(kColTextDim, "%s", video.uploader.c_str());
    ImGui::TextColored(kColTextDim, "%s  .  %s", video.views.c_str(), video.upload_date.c_str());
    ImGui::EndGroup();

    ImGui::PopID();
}

void ImTubeUI::render_lists_tab()
{
    ensure_demo_data();
    ImGui::TextColored(kColTextDim,
        "Saved videos. (History / Liked / Watch Later; stored in ~/.cache/imtube/lists.json)");
    ImGui::Separator();

    /* --- List selector -------------------------------------------------------- */
    ImGui::SetNextItemWidth(260.0f);
    ImGui::Combo("List", &m_selected_list, list_name_getter, &m_list_names, (int)m_list_names.size());

    /* --- Filter the saved videos for the selected list (most recent first) ----- */
    std::vector<const VideoItem*> list_videos;
    const auto in_list = [this](const VideoItem& v) {
        return (m_selected_list == 0 && v.watched) ||
               (m_selected_list == 1 && v.liked) ||
               (m_selected_list == 2 && v.watch_later);
    };
    for (const auto& kv : m_saved)
        if (in_list(kv.second))
            list_videos.push_back(&kv.second);
    std::sort(list_videos.begin(), list_videos.end(),
              [](const VideoItem* a, const VideoItem* b) { return a->saved_at > b->saved_at; });

    ImGui::TextColored(kColTextDim, "%zu video(s)", list_videos.size());
    ImGui::Separator();

    ImGui::BeginChild("##list_rows", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (list_videos.empty())
    {
        ImGui::TextColored(kColTextDim, "This list is empty.");
    }
    else
    {
        for (int i = 0; i < (int)list_videos.size(); i++)
        {
            render_list_row(*list_videos[i], i);
            if (i + 1 < (int)list_videos.size())
                ImGui::Separator();
        }
    }
    ImGui::EndChild();
}

void ImTubeUI::render_list_row(const VideoItem& video, int index)
{
    ImGui::PushID(index);

    const ImVec2 thumb_size(96.0f, 54.0f);
    if (ImGui::Button("##list_thumb", thumb_size))
        play_video(video);
    const auto tex_it = m_thumb_textures.find(video.id);
    if (tex_it != m_thumb_textures.end() && tex_it->second->valid())
    {
        const ImVec2 tl = ImGui::GetItemRectMin();
        const ImVec2 br = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddImage(
            tex_it->second->imgui_id(), tl, br, ImVec2(0, 0), ImVec2(1, 1));
    }
    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 130.0f);
    ImGui::TextUnformatted(video.title.c_str());
    ImGui::PopTextWrapPos();
    ImGui::TextColored(kColTextDim, "%s", video.uploader.c_str());
    ImGui::EndGroup();

    ImGui::SameLine();
    auto it = m_saved.find(video.id);
    if (it != m_saved.end())
    {
        ImGui::SameLine();
        if (m_selected_list == 1)
        {
            if (ImGui::Button("Unlike", ImVec2(56, 0)))
            {
                it->second.liked = false;
                save_saved();
            }
        }
        else if (m_selected_list == 2)
        {
            if (ImGui::Button("Remove", ImVec2(56, 0)))
            {
                it->second.watch_later = false;
                save_saved();
            }
        }
        else
        {
            if (ImGui::Button("Remove", ImVec2(56, 0)))
            {
                it->second.watched = false;
                save_saved();
            }
        }
    }

    ImGui::PopID();
}

void ImTubeUI::render_settings_tab()
{
    if (ImGui::CollapsingHeader("Streaming", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextUnformatted("Default stream resolution:");
        const int resolutions[] = { 240, 360, 480, 720, 1080 };
        bool first_radio = true;
        for (int r : resolutions)
        {
            ImGui::SameLine();
            ImGui::RadioButton((std::to_string(r) + "p").c_str(), &m_resolution, r);
            if (first_radio)
            {
                const ImVec2 rb_min = ImGui::GetItemRectMin();
                m_dbg.res_x = rb_min.x;
                m_dbg.res_y = rb_min.y;
                first_radio = false;
            }
        }
        ImGui::TextColored(kColTextDim,
            "Resolution used by yt-dlp when streaming videos (1080p default). "
            "Above 360p YouTube streams require ffmpeg to merge the DASH video and audio tracks.");
    }

    if (ImGui::CollapsingHeader("Recording", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Navigation History enabled", &m_navigation_history_enabled);
        ImGui::Checkbox("Video URL cache enabled", &m_cache_enabled);
        ImGui::TextColored(kColTextDim,
            "History / Liked / Watch Later lists and the URL cache are stored locally "
            "on disk (mirrors FLTube's userdata.txt + url cache).");
    }

    if (ImGui::CollapsingHeader("Downloads", ImGuiTreeNodeFlags_DefaultOpen))
    {
        char dir_buf[1024];
        strncpy(dir_buf, m_download_dir.c_str(), sizeof(dir_buf) - 1);
        dir_buf[sizeof(dir_buf) - 1] = '\0';
        ImGui::SetNextItemWidth(500.0f);
        if (ImGui::InputText("Download folder", dir_buf, sizeof(dir_buf),
                             ImGuiInputTextFlags_EnterReturnsTrue))
        {
            std::string dir = dir_buf;
            while (!dir.empty() && (dir.back() == '/' || dir.back() == ' '))
                dir.pop_back();
            if (!dir.empty())
                m_download_dir = dir;
        }
        ImGui::TextColored(kColTextDim,
            "Videos are saved here by the player's Download button as <video_id>.mp4.");
    }

    if (ImGui::CollapsingHeader("yt-dlp", ImGuiTreeNodeFlags_DefaultOpen))
    {
        char binary_buf[256];
        strncpy(binary_buf, m_ytdlp_binary.c_str(), sizeof(binary_buf) - 1);
        binary_buf[sizeof(binary_buf) - 1] = '\0';
        ImGui::SetNextItemWidth(400.0f);
        if (ImGui::InputText("yt-dlp binary", binary_buf, sizeof(binary_buf),
                             ImGuiInputTextFlags_EnterReturnsTrue))
            set_ytdlp_binary(binary_buf);

        if (!m_ytdlp_version_checked)
        {
            m_ytdlp_version = YtDlpHelper(m_ytdlp_binary).version();
            m_ytdlp_version_checked = true;
        }
        if (ImGui::Button("Check version"))
        {
            m_ytdlp_version = YtDlpHelper(m_ytdlp_binary).version();
            if (m_ytdlp_version.empty())
                m_ytdlp_version = "yt-dlp not found at '" + m_ytdlp_binary + "'.";
        }
        ImGui::TextColored(kColTextDim, "Using: %s", YtDlpHelper(m_ytdlp_binary).binary().c_str());
        if (!m_ytdlp_version.empty())
        {
            if (YtDlpHelper::version_at_least(m_ytdlp_version, "2025.01.01"))
                ImGui::TextColored(kColTextDim, "Version: %s", m_ytdlp_version.c_str());
            else
                ImGui::TextColored(kColTextError,
                    "%s\nYouTube has broken yt-dlp versions before 2025. Install a "
                    "recent release (e.g. from github.com/yt-dlp/yt-dlp/releases), for "
                    "instance as ~/.local/bin/yt-dlp.", m_ytdlp_version.c_str());
        }
    }

    if (ImGui::CollapsingHeader("About", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("ImTube v%s", IMTUBE_VERSION);
        ImGui::TextColored(kColTextDim,
            "A lightweight YouTube-style client built with Dear ImGui, SDL3 and Vulkan, "
            "powered by yt-dlp.");
        ImGui::TextColored(kColTextDim, "FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Separator();
        ImGui::TextColored(kColTextDim, "Backends: Dear ImGui (ocornut/imgui), SDL3 (libsdl-org/SDL), Vulkan, GStreamer, yt-dlp.");
    }
}

} /* namespace imtube */
