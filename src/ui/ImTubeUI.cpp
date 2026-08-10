#include "ui/ImTubeUI.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

namespace imtube {

namespace {

constexpr ImVec4 kColTextDim  = ImVec4(0.588f, 0.588f, 0.608f, 1.0f);
constexpr ImU32  kColLive     = IM_COL32(204, 0, 0, 255);
constexpr ImU32  kColThumbBg  = IM_COL32(26, 26, 30, 255);
constexpr ImU32  kColThumbHover = IM_COL32(255, 77, 77, 255);

std::string to_lower(const std::string& in)
{
    std::string out = in;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return out;
}

// ImGui::Combo() preview getter (this ImGui build only offers the `const char*` variant).
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

} // namespace

ImTubeUI::~ImTubeUI()
{
    m_player.stop();
    if (m_search_thread.joinable())
        m_search_thread.join();
    m_thumbs.shutdown();
}

void ImTubeUI::set_gpu(const GpuContext& gpu)
{
    m_gpu = gpu;
}

void ImTubeUI::set_ytdlp_binary(const std::string& path)
{
    if (path.empty())
        return;
    m_ytdlp_binary = path;
    m_ytdlp = YtDlpHelper(path);
}

void ImTubeUI::ensure_demo_data()
{
    if (!m_demo_videos.empty())
        return;

    // Placeholder data used by the lists tab; search results now come from
    // yt-dlp (see start_search()).
    VideoItem d1;
    d1.id = "d1"; d1.title = "STM32MP257F: Getting Started with OpenSTLinux";
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

void ImTubeUI::render()
{
    ensure_demo_data();

    // Handle finished background work (search results, decoded thumbnails).
    finish_search();
    poll_thumbnails();

    // --- Menu bar (tabs) ----------------------------------------------------
    render_menu_bar();
    const float menubar_height = ImGui::GetFrameHeight();

    ImGuiIO& io = ImGui::GetIO();

    // --- Main window filling the viewport below the menu bar -----------------
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
        switch (m_active_tab)
        {
            case Tab::Search:   render_search_tab();   break;
            case Tab::MyLists:  render_lists_tab();    break;
            case Tab::Settings: render_settings_tab(); break;
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();
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
    // Join any in-flight search before launching a new one.
    if (m_searching)
    {
        if (m_search_thread.joinable())
            m_search_thread.join();
    }

    const std::string query = m_search_buf;
    const std::string binary = m_ytdlp_binary;
    m_last_search = query;
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
            if (!helper.search(query, out))
                err = "yt-dlp search failed. Is yt-dlp installed?";
        }

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

    m_results = std::move(m_search_out);
    m_page = 0;
    cleanup_thumb_textures();
    m_thumbs.request_thumbnails(m_results);

    if (m_search_thread.joinable())
        m_search_thread.join();
}

void ImTubeUI::poll_thumbnails()
{
    if (m_gpu.device == VK_NULL_HANDLE)
        return;

    DecodedThumbnail thumb;
    while (m_thumbs.poll(thumb))
    {
        auto it = m_thumb_textures.find(thumb.video_id);
        if (it != m_thumb_textures.end())
        {
            if (it->second.width() == thumb.width && it->second.height() == thumb.height)
                it->second.upload(thumb.rgba.data());
            continue;
        }

        VulkanTexture tex;
        if (!tex.create(m_gpu, thumb.width, thumb.height))
            continue;
        tex.upload(thumb.rgba.data());
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
    m_live_stream = video.live;

    const std::string url = watch_url_for(video);
    if (!m_player.start(url, video.live, m_resolution, m_ytdlp_binary))
    {
        m_video_failed = true;
        return;
    }

    m_show_player = true;
    m_video_failed = false;

    // Mark as watched everywhere it appears.
    auto mark_watched = [&](std::vector<VideoItem>& items) {
        for (VideoItem& v : items)
            if (v.id == video.id)
                v.watched = true;
    };
    mark_watched(m_results);
    mark_watched(m_demo_videos);
}

void ImTubeUI::stop_video()
{
    m_show_player = false;
    m_video_failed = false;
    m_now_playing.clear();
    m_player.stop();
    m_video_texture.destroy();
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
            (m_video_texture.width() != w || m_video_texture.height() != h))
        {
            m_video_texture.destroy();
        }
        if (!m_video_texture.valid() && m_gpu.device != VK_NULL_HANDLE)
        {
            if (!m_video_texture.create(m_gpu, w, h))
                return;
        }
        if (m_video_texture.valid())
            m_video_texture.upload(m_player.frame_pixels());
    }
}

void ImTubeUI::render_video_view()
{
    update_video_frame();

    const float controls_height = ImGui::GetFrameHeightWithSpacing() * 2 + 12.0f;
    ImGui::BeginChild("##video_area", ImVec2(0.0f, -controls_height));

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 img_size = avail;
    if (m_video_texture.valid())
    {
        const float src_aspect = (float)m_video_texture.width() / (float)m_video_texture.height();
        const float dst_aspect = avail.x / avail.y;
        if (src_aspect > dst_aspect)
            img_size.y = avail.x / src_aspect;
        else
            img_size.x = avail.y * src_aspect;
        if (img_size.x > avail.x) img_size.x = avail.x;
        if (img_size.y > avail.y) img_size.y = avail.y;
    }

    ImGui::SetCursorPos(ImVec2((avail.x - img_size.x) * 0.5f, (avail.y - img_size.y) * 0.5f));

    if (m_video_texture.valid())
        ImGui::Image((ImTextureID)m_video_texture.descriptor_set(), img_size);
    else if (m_player.has_eos())
        ImGui::TextColored(kColTextDim, "Playback ended.");
    else if (m_video_failed)
        ImGui::TextColored(kColTextDim, "Playback failed: %s", m_player.error().c_str());
    else
        ImGui::TextColored(kColTextDim, "Starting playback...");

    ImGui::EndChild();

    // --- Control bar ---------------------------------------------------------
    if (ImGui::Button(m_player.is_paused() ? "Resume" : "Pause", ImVec2(72, 0)))
        m_player.toggle_pause();
    ImGui::SameLine();
    if (ImGui::Button("Stop", ImVec2(72, 0)))
    {
        stop_video();
        return;
    }
    ImGui::SameLine();

    ImGui::TextColored(kColTextDim, "%s%s", m_now_playing.c_str(),
                       m_live_stream ? "   [LIVE]" : "");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120.0f);
    ImGui::TextColored(kColTextDim, "Resolution: %dp", m_resolution);

    const std::string err = m_player.error();
    if (!err.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", err.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Dismiss"))
            stop_video();
    }
}

void ImTubeUI::render_search_tab()
{
    // --- Search bar ----------------------------------------------------------
    ImGui::SetNextItemWidth(-86.0f);
    if (ImGui::InputTextWithHint("##search_input", "Search videos or paste a YouTube URL",
                                 m_search_buf, sizeof(m_search_buf), ImGuiInputTextFlags_EnterReturnsTrue))
        m_search_requested = true;
    ImGui::SameLine();
    if (ImGui::Button("Search", ImVec2(78, 0)))
        m_search_requested = true;

    if (m_search_requested)
    {
        m_search_requested = false;
        m_page = 0;
        start_search();
    }

    // --- Stream resolution selector ------------------------------------------
    ImGui::TextUnformatted("Stream resolution:");
    const int resolutions[] = { 240, 360, 480, 720, 1080 };
    for (int r : resolutions)
    {
        ImGui::SameLine();
        ImGui::RadioButton((std::to_string(r) + "p").c_str(), &m_resolution, r);
    }
    ImGui::SameLine();
    ImGui::TextColored(kColTextDim, "  (used by yt-dlp when streaming)");

    ImGui::Separator();

    // --- Results --------------------------------------------------------------
    const float footer_height = ImGui::GetFrameHeightWithSpacing() + 8.0f;
    ImGui::BeginChild("##results", ImVec2(0.0f, -footer_height), ImGuiChildFlags_Borders);

    if (m_searching)
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
        const int start = m_page * kResultsPerPage;
        const int end = std::min(start + kResultsPerPage, (int)m_results.size());
        for (int i = start; i < end; i++)
        {
            render_result_card(m_results[i], i);
            if (i + 1 < end)
                ImGui::Separator();
        }
    }

    ImGui::EndChild();

    // --- Pagination footer ----------------------------------------------------
    if (m_results.empty())
        return;
    const int page_count = std::max(1, ((int)m_results.size() + kResultsPerPage - 1) / kResultsPerPage);
    const bool has_prev = m_page > 0;
    const bool has_next = m_page + 1 < page_count;

    const char* first_btn = "|< First";
    const char* prev_btn  = "< Previous";
    const char* next_btn  = "Next >";
    const char* last_btn  = "Last >|";
    const float w_first = ImGui::CalcTextSize(first_btn).x + 24.0f;
    const float w_prev  = ImGui::CalcTextSize(prev_btn).x + 24.0f;
    const float w_next  = ImGui::CalcTextSize(next_btn).x + 24.0f;
    const float w_last  = ImGui::CalcTextSize(last_btn).x + 24.0f;
    const float w_total = w_first + w_prev + w_next + w_last;

    ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x * 0.5f - w_total * 0.5f);
    if (ImGui::Button(first_btn, ImVec2(w_first, 0)) && has_prev)
        m_page = 0;
    ImGui::SameLine();
    if (ImGui::Button(prev_btn, ImVec2(w_prev, 0)) && has_prev)
        m_page--;
    ImGui::SameLine();
    ImGui::Text("Page %d/%d", m_page + 1, page_count);
    ImGui::SameLine();
    if (ImGui::Button(next_btn, ImVec2(w_next, 0)) && has_next)
        m_page++;
    ImGui::SameLine();
    if (ImGui::Button(last_btn, ImVec2(w_last, 0)) && has_next)
        m_page = page_count - 1;
}

void ImTubeUI::render_result_card(const VideoItem& video, int index)
{
    ImGui::PushID(index);

    const ImVec2 thumb_size(128.0f, 72.0f);

    // Make the thumbnail area a button that starts playback.
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, kColThumbBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kColThumbBg);
    if (ImGui::Button("##thumb_btn", thumb_size))
        play_video(m_results[index]);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    const ImVec2 thumb_tl = ImGui::GetItemRectMin();
    const ImVec2 thumb_br = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Thumbnail image (aspect matches 16:9 thumbnails), if available.
    const auto tex_it = m_thumb_textures.find(video.id);
    if (tex_it != m_thumb_textures.end() && tex_it->second.valid())
    {
        dl->AddImage((ImTextureID)tex_it->second.descriptor_set(),
                     thumb_tl, thumb_br,
                     ImVec2(0, 0), ImVec2(1, 1));
        if (ImGui::IsItemHovered())
            dl->AddRect(thumb_tl, thumb_br, kColThumbHover);
    }

    // Duration overlay (bottom-right)
    if (!video.duration.empty())
    {
        const ImVec2 ts = ImGui::CalcTextSize(video.duration.c_str());
        dl->AddRectFilled(ImVec2(thumb_br.x - ts.x - 10.0f, thumb_br.y - 18.0f),
                          ImVec2(thumb_br.x - 3.0f, thumb_br.y - 3.0f), IM_COL32(0, 0, 0, 210));
        dl->AddText(ImVec2(thumb_br.x - ts.x - 5.0f, thumb_br.y - 16.0f),
                    IM_COL32(255, 255, 255, 255), video.duration.c_str());
    }

    // LIVE badge (top-left)
    if (video.live)
    {
        const char* live = "LIVE";
        const ImVec2 ls = ImGui::CalcTextSize(live);
        dl->AddRectFilled(ImVec2(thumb_tl.x + 3.0f, thumb_tl.y + 3.0f),
                          ImVec2(thumb_tl.x + 3.0f + ls.x + 8.0f, thumb_tl.y + 3.0f + ls.y + 5.0f), kColLive);
        dl->AddText(ImVec2(thumb_tl.x + 7.0f, thumb_tl.y + 5.0f), IM_COL32(255, 255, 255, 255), live);
    }

    ImGui::SameLine();

    // --- Text column ----------------------------------------------------------
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 170.0f);
    ImGui::TextUnformatted(video.title.c_str());
    ImGui::PopTextWrapPos();
    ImGui::TextColored(kColTextDim, "%s", video.uploader.c_str());
    ImGui::TextColored(kColTextDim, "%s  .  %s", video.views.c_str(), video.upload_date.c_str());
    ImGui::EndGroup();

    ImGui::SameLine();

    // --- Action buttons (right) ------------------------------------------------
    ImGui::BeginGroup();
    if (ImGui::Button("Play", ImVec2(56, 0)))
        play_video(m_results[index]);
    if (ImGui::Button("Like", ImVec2(56, 0)))
        m_results[index].liked = !m_results[index].liked;
    if (ImGui::Button("Later", ImVec2(56, 0)))
        m_results[index].watch_later = !m_results[index].watch_later;
    ImGui::EndGroup();

    ImGui::PopID();
}

void ImTubeUI::render_lists_tab()
{
    ensure_demo_data();
    ImGui::TextColored(kColTextDim,
        "Saved videos. (History / Liked / Watch Later; stored in memory for now)");
    ImGui::Separator();

    // --- List selector --------------------------------------------------------
    ImGui::SetNextItemWidth(260.0f);
    ImGui::Combo("List", &m_selected_list, list_name_getter, &m_list_names, (int)m_list_names.size());

    // --- Filter demo data + live search results for the selected list ----------
    std::vector<const VideoItem*> list_videos;
    const auto in_list = [this](const VideoItem& v) {
        return (m_selected_list == 0 && v.watched) ||
               (m_selected_list == 1 && v.liked) ||
               (m_selected_list == 2 && v.watch_later);
    };
    for (const VideoItem& v : m_results)
        if (in_list(v))
            list_videos.push_back(&v);
    for (const VideoItem& v : m_demo_videos)
        if (in_list(v))
            list_videos.push_back(&v);

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
    ImGui::Button("##list_thumb", thumb_size);
    const auto tex_it = m_thumb_textures.find(video.id);
    if (tex_it != m_thumb_textures.end() && tex_it->second.valid())
    {
        const ImVec2 tl = ImGui::GetItemRectMin();
        const ImVec2 br = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddImage(
            (ImTextureID)tex_it->second.descriptor_set(), tl, br, ImVec2(0, 0), ImVec2(1, 1));
    }
    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 130.0f);
    ImGui::TextUnformatted(video.title.c_str());
    ImGui::PopTextWrapPos();
    ImGui::TextColored(kColTextDim, "%s", video.uploader.c_str());
    ImGui::EndGroup();

    ImGui::SameLine();
    if (ImGui::Button("Play", ImVec2(64, 0)))
        play_video(video);
    ImGui::SameLine();
    if (ImGui::Button("Remove", ImVec2(64, 0)))
    {
        // TODO: remove from the selected list (see FLTube removeFromVideoList_cb)
    }

    ImGui::PopID();
}

void ImTubeUI::render_settings_tab()
{
    if (ImGui::CollapsingHeader("Streaming", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextUnformatted("Default stream resolution:");
        const int resolutions[] = { 240, 360, 480, 720, 1080 };
        for (int r : resolutions)
        {
            ImGui::SameLine();
            ImGui::RadioButton((std::to_string(r) + "p").c_str(), &m_resolution, r);
        }
        ImGui::TextColored(kColTextDim,
            "Resolution used by yt-dlp when streaming videos (360p default, like FLTube).");
    }

    if (ImGui::CollapsingHeader("Recording", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Navigation History enabled", &m_navigation_history_enabled);
        ImGui::Checkbox("Video URL cache enabled", &m_cache_enabled);
        ImGui::TextColored(kColTextDim,
            "History / Liked / Watch Later lists and the URL cache are stored locally "
            "on disk (mirrors FLTube's userdata.txt + url cache).");
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

        static std::string version;
        if (ImGui::Button("Check version"))
        {
            version = YtDlpHelper(m_ytdlp_binary).version();
            if (version.empty())
                version = "yt-dlp not found at '" + m_ytdlp_binary + "'.";
        }
        if (!version.empty())
            ImGui::TextColored(kColTextDim, "%s", version.c_str());
    }

    if (ImGui::CollapsingHeader("About", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("ImTube v%s", IMTUBE_VERSION);
        ImGui::TextColored(kColTextDim,
            "A lightweight YouTube-style client built with Dear ImGui, SDL3 and Vulkan, "
            "powered by yt-dlp.");
        ImGui::TextColored(kColTextDim, "FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Separator();
        ImGui::TextColored(kColTextDim, "Concept inspired by FLTube (gitlab.com/facuA/fltube).");
        ImGui::TextColored(kColTextDim, "Backends: Dear ImGui (ocornut/imgui), SDL3 (libsdl-org/SDL), Vulkan.");
    }
}

} // namespace imtube
