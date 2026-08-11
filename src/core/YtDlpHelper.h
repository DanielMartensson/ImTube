#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace imtube {

// A single search result / video to stream. Populated by YtDlpHelper from the
// yt-dlp JSON output (mirrors FLTube's YTDLP_Video_Metadata) and consumed by
// the UI. UI-only bookkeeping flags (liked / watch_later / watched) live here
// so the core and UI share one definition.
struct VideoItem {
    std::string id;
    std::string url;             // watch URL, e.g. https://www.youtube.com/watch?v=<id>
    std::string title;
    std::string uploader;
    std::string upload_date;     // YYYY-MM-DD ("" if unknown)
    std::string duration;        // "M:SS" or "H:MM:SS" text
    std::string views;           // formatted, e.g. "1.2M views"
    std::string channel_id;
    std::string thumbnail_url;   // https://i.ytimg.com/... ("" if unknown)
    bool live = false;
    int64_t duration_seconds = 0;
    int64_t view_count = 0;
    // UI bookkeeping
    bool liked = false;
    bool watch_later = false;
    bool watched = false;
    int64_t saved_at = 0; // unix time when the video was last saved/watched (for history order)
};

// Wraps the yt-dlp command line tool (same approach as FLTube):
//   * search(query)  -> runs "yt-dlp --flat-playlist --dump-json ytsearchN:<query>"
//   * search_url(url)-> runs "yt-dlp --no-playlist --dump-json <url>"
//   * launch_stream  -> forks yt-dlp and pipes its merged media stream to stdout.
//
// The subprocess helpers are thread-safe so search can run on a worker thread.
class YtDlpHelper {
public:
    // When the configured path is the bare default "yt-dlp", a newer personal
    // copy at ~/.local/bin/yt-dlp is preferred over the (often stale) distro
    // package found via PATH. An explicit user path is always honored.
    explicit YtDlpHelper(std::string binary_path = "yt-dlp")
        : m_binary_path(resolve_binary(std::move(binary_path))) {}

    // The resolved binary path (default "yt-dlp" resolves to ~/.local/bin/yt-dlp
    // when present). Useful to show in the UI what is actually being used.
    const std::string& binary() const { return m_binary_path; }

    // Runs "<binary> --version". Returns the version string, or "" on failure.
    std::string version();

    // Compares dotted version strings (yt-dlp uses "YYYY.MM.DD", possibly with
    // a trailing ".NN"). Returns true when version >= min_ver.
    static bool version_at_least(const std::string& version, const std::string& min_ver);

    // Full single-video metadata for a pasted YouTube URL. Fills item; returns
    // false if yt-dlp could not extract the video.
    bool search_url(const std::string& url, VideoItem& item);

    // Search results for a free-text query. Returns false on yt-dlp failure.
    // Fetches entries in the 1-based range (start+1, end] so the UI can load
    // more results incrementally as the user scrolls. For the first page call
    // search(query, out, 20, 0).
    bool search(const std::string& query, std::vector<VideoItem>& results,
                int end_count, int start_index);

    // Launches yt-dlp streaming the given URL to stdout and returns the read end
    // of the child's stdout pipe. Caller feeds the fd into the GStreamer
    // pipeline and closes it afterwards.
    //   max_height: requested video height (240/360/480/720/1080).
    //   live:       use the livestream format selection.
    // Returns fd >= 0 on success, or -1 on failure (*out_pid is valid then).
    int launch_stream(const std::string& url, bool live, int max_height, int* out_pid);

    // True when the URL looks like a YouTube video / playlist / channel URL.
    static bool is_youtube_url(const std::string& url);

    // Path of the log file the last launch_stream wrote the child's stderr to
    // (empty if the last attempt did not produce one). Callers show its tail to
    // the user when playback fails right after starting.
    const std::string& last_stderr_log() const { return m_stderr_log; }

    // Downloads the video (best stream up to max_height, merged into mp4) into
    // out_dir, saving "<id>.mp4". Returns the read end of a pipe carrying
    // yt-dlp's stderr progress ("[download] 42.3% ..."), or -1 on failure
    // (*out_pid is valid then). The caller drains the pipe until EOF and then
    // waitpid()s the child.
    int launch_download(const std::string& url, int max_height,
                        const std::string& out_dir, int* out_pid);

    // Runs yt-dlp to fetch English (auto)subtitles for the video into the cache
    // dir, saving "<id>.<lang>.vtt|srt". Returns the subtitle file paths that
    // were produced (empty on failure). Blocking: call from a worker thread.
    std::vector<std::string> fetch_subtitles(const std::string& url,
                                             const std::string& video_id);

    // Directory where per-video extras (subtitles, ...) are cached on disk.
    static std::string cache_dir();

private:
    static std::string resolve_binary(const std::string& configured);

    std::string m_binary_path;
    std::string m_stderr_log;
};

// Format helpers shared with the UI.
std::string format_view_count(int64_t views);
std::string format_duration(int64_t seconds);

} // namespace imtube
