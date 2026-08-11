#include "core/YtDlpHelper.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace imtube {

namespace {

using json = nlohmann::json;

/* Capture a command's stdout into a string. Returns false if the command could
 * not be started (exit status ignored here; callers validate the output). */
bool run_capture(const std::string& command, std::string& out)
{
    out.clear();
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr)
        return false;

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0)
        out.append(buf, n);

    int rc = pclose(pipe);
    (void)rc;
    return true;
}

/* Quote a string for safe inclusion in a /bin/sh command line. */
std::string shell_quote(const std::string& s)
{
    std::string out = "'";
    for (char c : s)
    {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += "'";
    return out;
}

/* nlohmann::json::value(key, fallback) throws json::type_error when the key
 * exists but holds JSON null. yt-dlp emits plenty of nulls (live_status,
 * uploader, channel, ...), so read strings defensively: null is treated like a
 * missing key. */
std::string json_string(const json& j, const char* key, const char* fallback = "")
{
    const auto it = j.find(key);
    if (it == j.end() || it->is_null())
        return fallback;
    if (it->is_string())
        return it->get<std::string>();
    return fallback;
}

/* First non-null string found among the given keys (e.g. uploader, channel). */
std::string json_string_any(const json& j, std::initializer_list<const char*> keys,
                            const char* fallback = "")
{
    for (const char* key : keys)
    {
        const auto it = j.find(key);
        if (it != j.end() && !it->is_null() && it->is_string())
            return it->get<std::string>();
    }
    return fallback;
}

/* Pick the most suitable thumbnail URL from the yt-dlp metadata. Prefers a
 * "medium" sized frame (>=120, <=400 px wide) so cards load quickly, and strips
 * the "?sqp=" query YouTube appends for WebP (this stb_image build cannot
 * decode WebP). */
std::string pick_thumbnail_url(const json& j)
{
    if (!j.contains("thumbnails") || !j["thumbnails"].is_array())
    {
        if (j.contains("thumbnail") && j["thumbnail"].is_string())
            return j["thumbnail"].get<std::string>();
        return "";
    }

    const json& thumbs = j["thumbnails"];
    std::string best;
    int best_score = -1;
    for (const json& t : thumbs)
    {
        if (!t.is_object())
            continue;
        int width = t.value("width", 0);
        std::string url = json_string(t, "url");
        if (url.empty())
            continue;
        /* YouTube serves WebP when the i.ytimg.com URL carries a "?sqp=" query
         * string, but our stb_image build has no WebP decoder. Strip the query
         * so the plain .jpg URL is fetched instead. */
        const size_t q = url.find('?');
        if (q != std::string::npos)
            url.resize(q);
        /* Prefer a "medium" sized thumbnail (>=120, <=400 px wide) if available. */
        int score = (width >= 120 && width <= 400) ? 1 : 0;
        if (score >= best_score)
        {
            best_score = score;
            best = url;
        }
    }
    return best;
}

/* Convert one --dump-json entry into a VideoItem. */
void json_to_video_item(const json& j, VideoItem& v)
{
    v.id = json_string(j, "id");
    v.title = json_string(j, "title", "(untitled)");
    v.uploader = json_string_any(j, { "uploader", "channel" });
    v.channel_id = json_string_any(j, { "channel_id", "playlist_channel_id" });

    const std::string ud = json_string(j, "upload_date");
    if (ud.size() == 8)
        v.upload_date = ud.substr(0, 4) + "-" + ud.substr(4, 2) + "-" + ud.substr(6, 2);
    else
        v.upload_date = ud;

    if (j.contains("duration") && j["duration"].is_number())
    {
        v.duration_seconds = j["duration"].get<int64_t>();
        v.duration = format_duration(v.duration_seconds);
    }
    else
    {
        v.duration = json_string(j, "duration_string");
    }

    if (j.contains("view_count") && j["view_count"].is_number())
    {
        v.view_count = j["view_count"].get<int64_t>();
        v.views = format_view_count(v.view_count);
    }

    const std::string live = json_string(j, "live_status", "not_live");
    v.live = (live == "is_live" || live == "is_upcoming" || live == "post_live");

    v.thumbnail_url = pick_thumbnail_url(j);

    std::string url = json_string(j, "webpage_url");
    if (url.empty() && !v.id.empty())
        url = "https://www.youtube.com/watch?v=" + v.id;
    v.url = url;
}

/* One line of --dump-json output may itself contain embedded newlines inside
 * JSON string values, so split on "\n" between '}' and '{' boundaries. */
void split_json_lines(const std::string& text, std::vector<std::string>& lines)
{
    std::string cur;
    for (char c : text)
    {
        if (c == '\r')
            continue; /* tolerate CRLF */
        if (c == '\n' && !cur.empty() && cur.back() == '}')
        {
            lines.push_back(cur);
            cur.clear();
        }
        else
        {
            cur += c;
        }
    }
    if (!cur.empty())
        lines.push_back(cur);
}

/* yt-dlp writes subtitle files as "<id>.<lang>.vtt|srt" (e.g. "abc123.en.vtt").
 * List the ones that exist for a video, sorted for a deterministic order. */
std::vector<std::string> list_subtitle_files(const std::string& dir, const std::string& video_id)
{
    std::vector<std::string> files;
    DIR* d = opendir(dir.c_str());
    if (d == nullptr)
        return files;
    while (struct dirent* e = readdir(d))
    {
        const std::string name = e->d_name;
        if (name.rfind(video_id + ".", 0) != 0)
            continue;
        const bool vtt = name.size() > 4 && name.compare(name.size() - 4, 4, ".vtt") == 0;
        const bool srt = name.size() > 4 && name.compare(name.size() - 4, 4, ".srt") == 0;
        if (vtt || srt)
            files.push_back(dir + "/" + name);
    }
    closedir(d);
    std::sort(files.begin(), files.end());
    return files;
}

/* Remove every cached subtitle file belonging to a video id. */
void remove_subtitle_files(const std::string& dir, const std::string& video_id)
{
    for (const std::string& f : list_subtitle_files(dir, video_id))
        remove(f.c_str());
}

} /* namespace */

std::string YtDlpHelper::resolve_binary(const std::string& configured)
{
    if (!configured.empty() && configured != "yt-dlp")
        return configured;
    const char* home = getenv("HOME");
    if (home == nullptr)
        return configured;
    std::string local = std::string(home) + "/.local/bin/yt-dlp";
    if (access(local.c_str(), X_OK) == 0)
        return local;
    return configured;
}

std::string YtDlpHelper::version()
{
    std::string out;
    if (!run_capture(shell_quote(m_binary_path) + " --version", out))
        return "";
    std::string result;
    for (char c : out)
    {
        if (c == '\n' || c == '\r')
            break;
        result += c;
    }
    return result;
}

bool YtDlpHelper::version_at_least(const std::string& version, const std::string& min_ver)
{
    auto nums = [](const std::string& s) {
        std::vector<int> v;
        std::istringstream iss(s);
        std::string part;
        while (std::getline(iss, part, '.'))
            v.push_back(atoi(part.c_str()));
        return v;
    };
    std::vector<int> a = nums(version);
    std::vector<int> b = nums(min_ver);
    const size_t n = std::max(a.size(), b.size());
    a.resize(n, 0);
    b.resize(n, 0);
    for (size_t i = 0; i < n; i++)
    {
        if (a[i] < b[i])
            return false;
        if (a[i] > b[i])
            return true;
    }
    return true;
}

bool YtDlpHelper::search_url(const std::string& url, VideoItem& item)
{
    std::string cmd = shell_quote(m_binary_path) +
                      " --no-warnings --no-playlist --dump-json --socket-timeout 20 " +
                      shell_quote(url);
    std::string out;
    if (!run_capture(cmd, out))
        return false;

    std::vector<std::string> lines;
    split_json_lines(out, lines);
    for (const std::string& line : lines)
    {
        try
        {
            const json j = json::parse(line);
            if (j.contains("id"))
            {
                json_to_video_item(j, item);
                return true;
            }
        }
        catch (const std::exception&)
        {
            /* Ignore non-JSON noise from yt-dlp and keep scanning. */
        }
    }
    return false;
}

bool YtDlpHelper::search(const std::string& query, std::vector<VideoItem>& results,
                         int end_count, int start_index)
{
    results.clear();

    std::string term = query;
    /* A pasted YouTube URL goes through the full extractor instead. */
    if (is_youtube_url(term))
    {
        VideoItem item;
        if (search_url(term, item))
            results.push_back(item);
        return !results.empty();
    }

    /* yt-dlp's "ytsearchN:" pseudo-playlist holds up to N entries and honors
     * --playlist-start/--playlist-end, which is how we page through results. */
    if (end_count <= 0)
        end_count = 20;
    start_index = std::max(0, start_index);
    const int end_index = std::max(start_index + 1, end_count);

    const std::string search_uri = "ytsearch" + std::to_string(end_index) + ":" + term;
    std::string cmd = shell_quote(m_binary_path) +
                      " --no-warnings --flat-playlist --dump-json --socket-timeout 20 "
                      "--playlist-start " + std::to_string(start_index + 1) +
                      " --playlist-end " + std::to_string(end_index) +
                      " --extractor-args \"youtubetab:approximate_date\" " +
                      shell_quote(search_uri);
    std::string out;
    if (!run_capture(cmd, out))
        return false;

    std::vector<std::string> lines;
    split_json_lines(out, lines);
    for (const std::string& line : lines)
    {
        try
        {
            const json j = json::parse(line);
            if (!j.contains("id"))
                continue;
            VideoItem v;
            json_to_video_item(j, v);
            results.push_back(std::move(v));
        }
        catch (const std::exception&)
        {
            /* Ignore non-JSON noise from yt-dlp and keep scanning. */
        }
    }
    return !results.empty();
}

int YtDlpHelper::launch_stream(const std::string& url, bool live, int max_height, int* out_pid)
{
    std::vector<std::string> args;
    args.push_back(m_binary_path);
    args.push_back("--no-warnings");
    args.push_back("--no-playlist");
    args.push_back("--socket-timeout");
    args.push_back("20");

    if (live)
    {
        args.push_back("-f");
        args.push_back("best");
    }
    else
    {
#ifdef IMTUBE_EMBEDDED
        /* Embedded (STM32MPU): pick a single H.264 stream (progressive/combined
         * when available) so playback needs no ffmpeg merge; GStreamer feeds it
         * straight to the VPU hardware decoder. */
        char fmt[160];
        snprintf(fmt, sizeof(fmt),
                 "b[height<=%d][vcodec^=avc1]/b[height<=%d]/b/best", max_height, max_height);
        args.push_back("-f");
        args.push_back(fmt);
#else
        /* Desktop: honor the requested height. Above 360p YouTube only offers
         * separate DASH video+audio streams (combined files cap out at 360p /
         * 720p), so prefer the merged bv+ba pair first for full resolution; a
         * single progressive file is the fallback for instant playback and for
         * seeking (the seek restart needs a single stream). "mkv" (not
         * "matroska") is the value yt-dlp requires for --merge-output-format. */
        char fmt[192];
        if (max_height <= 360)
        {
            /* Low resolution: combined streams cover it, keep instant playback. */
            snprintf(fmt, sizeof(fmt),
                     "b[height<=%d][vcodec^=avc1]/b[height<=%d]/b/best",
                     max_height, max_height);
        }
        else
        {
            snprintf(fmt, sizeof(fmt),
                     "bv*[height<=%d][vcodec^=avc1]+ba/bv*[height<=%d]+ba/"
                     "b[height<=%d][vcodec^=avc1]/b[height<=%d]/b/best",
                     max_height, max_height, max_height, max_height);
        }
        args.push_back("-f");
        args.push_back(fmt);
        args.push_back("--merge-output-format");
        args.push_back("mkv");
#endif
    }

    args.push_back("-o");
    args.push_back("-");
    args.push_back(url);

    int pipefd[2];
    if (pipe(pipefd) != 0)
        return -1;

    /* Unique log path per attempt so concurrent streams never clobber each other. */
    char log_path[96];
    snprintf(log_path, sizeof(log_path), "/tmp/imtube-ytdlp-%d.log", (int)getpid());
    m_stderr_log = log_path;

    const pid_t pid = fork();
    if (pid < 0)
    {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0)
    {
        /* Child: stdout -> pipe, stderr -> log file (so failures can be shown
         * to the user instead of being lost). */
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        const int logfd = open(m_stderr_log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (logfd >= 0)
        {
            dup2(logfd, STDERR_FILENO);
            close(logfd);
        }

        std::vector<char*> argv;
        for (std::string& a : args)
            argv.push_back(a.data());
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(pipefd[1]);
    if (out_pid)
        *out_pid = (int)pid;
    return pipefd[0];
}

std::string YtDlpHelper::launch_direct_url_fetch(const std::string& url, int max_height,
                                                 int* out_pid)
{
    /* Resolve the direct URL(s) for the seek restart. On desktop this is the
     * DASH video+audio pair (the same tracks playback uses) so a jump keeps the
     * chosen resolution: ffmpeg restarts both at the offset and merges them.
     * Videos without separate audio fall back to a single progressive file.
     * "yt-dlp -g" prints one URL per line: video first, audio second. The
     * embedded target keeps a single H.264 stream (no ffmpeg merge; VPU). */
    std::vector<std::string> args;
    args.push_back(m_binary_path);
    args.push_back("-q");
    args.push_back("--no-warnings");
    args.push_back("--no-playlist");
    args.push_back("-f");
    char fmt[160];
#ifdef IMTUBE_EMBEDDED
    snprintf(fmt, sizeof(fmt), "b[height<=%d][vcodec^=avc1]/b[height<=%d]/b",
             max_height, max_height);
#else
    snprintf(fmt, sizeof(fmt),
             "bv*[height<=%d][vcodec^=avc1]+ba/b[height<=%d][vcodec^=avc1]/b[height<=%d]/b",
             max_height, max_height, max_height);
#endif
    args.push_back(fmt);
    args.push_back("-g");
    args.push_back(url);

    /* Unique output path per attempt (one player, so the process pid is enough). */
    char out_path[96];
    snprintf(out_path, sizeof(out_path), "/tmp/imtube-direct-%d.txt", (int)getpid());

    const pid_t pid = fork();
    if (pid < 0)
        return "";

    if (pid == 0)
    {
        const int outfd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (outfd >= 0)
        {
            dup2(outfd, STDOUT_FILENO);
            close(outfd);
        }
        const int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0)
        {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        std::vector<char*> argv;
        for (std::string& a : args)
            argv.push_back(a.data());
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        _exit(127);
    }

    if (out_pid)
        *out_pid = (int)pid;
    return out_path;
}

bool YtDlpHelper::read_direct_urls(const std::string& file, std::string* out_video_url,
                                   std::string* out_audio_url)
{
    if (out_video_url == nullptr)
        return false;
    out_video_url->clear();
    if (out_audio_url)
        out_audio_url->clear();

    FILE* f = fopen(file.c_str(), "rb");
    if (f == nullptr)
        return false;

    std::string data;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);

    /* Split into lines and keep the non-empty (trimmed) URLs: the first is the
     * video stream, the second (when present) the audio stream. */
    const auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    std::vector<std::string> urls;
    size_t start = 0;
    while (start < data.size())
    {
        size_t end = data.find('\n', start);
        if (end == std::string::npos)
            end = data.size();
        std::string line = data.substr(start, end - start);
        while (!line.empty() && is_space(line.front()))
            line.erase(line.begin());
        while (!line.empty() && is_space(line.back()))
            line.pop_back();
        if (!line.empty())
            urls.push_back(line);
        start = end + 1;
    }

    if (urls.empty())
        return false;
    *out_video_url = urls[0];
    if (out_audio_url && urls.size() > 1)
        *out_audio_url = urls[1];
    return true;
}

int YtDlpHelper::launch_seek_stream(const std::string& video_url, const std::string& audio_url,
                                    int64_t start_ms, int* out_pid)
{
    std::vector<std::string> args;
    args.push_back("ffmpeg");
    args.push_back("-nostdin");
    args.push_back("-hide_banner");
    args.push_back("-loglevel");
    args.push_back("error");
    char sec[64];
    snprintf(sec, sizeof(sec), "%.3f", (double)start_ms / 1000.0);
    if (!audio_url.empty())
    {
        /* Full-resolution seek: restart the DASH video and audio at the target
         * and merge them, so a jump keeps the resolution the user picked. */
        args.push_back("-ss");
        args.push_back(sec);
        args.push_back("-i");
        args.push_back(video_url);
        args.push_back("-ss");
        args.push_back(sec);
        args.push_back("-i");
        args.push_back(audio_url);
        args.push_back("-map");
        args.push_back("0:v");
        args.push_back("-map");
        args.push_back("1:a");
        args.push_back("-c");
        args.push_back("copy");
        args.push_back("-avoid_negative_ts");
        args.push_back("make_zero");
        args.push_back("-f");
        args.push_back("matroska");
        args.push_back("pipe:1");
    }
    else
    {
        /* Single progressive file (videos without separate audio): range-seek
         * it in place. */
        args.push_back("-ss");
        args.push_back(sec);
        args.push_back("-i");
        args.push_back(video_url);
        args.push_back("-c");
        args.push_back("copy");
        args.push_back("-avoid_negative_ts");
        args.push_back("make_zero");
        args.push_back("-f");
        args.push_back("matroska");
        args.push_back("pipe:1");
    }

    int pipefd[2];
    if (pipe(pipefd) != 0)
        return -1;

    /* ffmpeg's stderr is captured like yt-dlp's, so a failed seek shows why. */
    char log_path[96];
    snprintf(log_path, sizeof(log_path), "/tmp/imtube-ffmpeg-%d.log", (int)getpid());
    m_stderr_log = log_path;

    const pid_t pid = fork();
    if (pid < 0)
    {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0)
    {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        const int logfd = open(m_stderr_log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (logfd >= 0)
        {
            dup2(logfd, STDERR_FILENO);
            close(logfd);
        }

        std::vector<char*> argv;
        for (std::string& a : args)
            argv.push_back(a.data());
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(pipefd[1]);
    if (out_pid)
        *out_pid = (int)pid;
    return pipefd[0];
}

std::string YtDlpHelper::cache_dir()
{
    const char* home = getenv("HOME");
    std::string dir = home && *home ? std::string(home) + "/.cache/imtube" : "/tmp/imtube-cache";
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST)
        fprintf(stderr, "[imtube] cannot create cache dir '%s'\n", dir.c_str());
    return dir;
}

int YtDlpHelper::launch_download(const std::string& url, int max_height,
                                 const std::string& out_dir, int* out_pid)
{
    std::vector<std::string> args;
    args.push_back(m_binary_path);
    args.push_back("--no-warnings");
    args.push_back("--no-playlist");
    args.push_back("--newline");
    args.push_back("--progress");
    args.push_back("-f");
    char fmt[192];
    snprintf(fmt, sizeof(fmt),
             "bv*[height<=%d][ext=mp4]+ba[ext=m4a]/b[height<=%d][ext=mp4]/b/"
             "bv*[height<=%d]+ba/best[ext=mp4]/best",
             max_height, max_height, max_height);
    args.push_back(fmt);
    args.push_back("--merge-output-format");
    args.push_back("mp4");
    args.push_back("-o");
    args.push_back(out_dir + "/%(id)s.%(ext)s");
    args.push_back(url);

    int pipefd[2];
    if (pipe(pipefd) != 0)
        return -1;

    const pid_t pid = fork();
    if (pid < 0)
    {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0)
    {
        /* Child: stdout -> /dev/null, stderr -> pipe (yt-dlp progress lines). */
        const int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0)
        {
            dup2(devnull, STDOUT_FILENO);
            close(devnull);
        }
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        std::vector<char*> argv;
        for (std::string& a : args)
            argv.push_back(a.data());
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(pipefd[1]);
    if (out_pid)
        *out_pid = (int)pid;
    return pipefd[0];
}

std::vector<std::string> YtDlpHelper::fetch_subtitles(const std::string& url,
                                                      const std::string& video_id)
{
    const std::string dir = cache_dir();

    /* Drop subtitle files from earlier attempts: a partial or failed fetch must
     * never surface stale cues, and they would be mixed in by the scan below. */
    remove_subtitle_files(dir, video_id);

    const std::string out_template = dir + "/" + video_id + ".%(ext)s";

    /* Only the plain English track and its "original" variant are fetched.
     * Broad patterns like "en.*" match every en-* variant yt-dlp knows
     * (en, en-en, en-orig, en-de-DE, ...); downloading them all in a burst
     * gets us rate-limited (HTTP 429) and no subtitles arrive. */
    std::string cmd = shell_quote(m_binary_path) +
        " --no-warnings --no-playlist --skip-download"
        " --write-auto-subs --write-subs"
        " --sub-langs 'en(-orig)?' --sub-format 'vtt/srt/best'"
        " -o " + shell_quote(out_template) + " " + shell_quote(url);
    std::string out;
    run_capture(cmd, out);

    return list_subtitle_files(dir, video_id);
}

bool YtDlpHelper::is_youtube_url(const std::string& url)
{
    return url.rfind("https://www.youtube.com/", 0) == 0 ||
           url.rfind("https://youtube.com/", 0) == 0 ||
           url.rfind("https://youtu.be/", 0) == 0 ||
           url.rfind("http://www.youtube.com/", 0) == 0 ||
           url.rfind("http://youtu.be/", 0) == 0 ||
           url.rfind("www.youtube.com/", 0) == 0;
}

std::string format_view_count(int64_t views)
{
    char buf[48];
    if (views < 1000)
        snprintf(buf, sizeof(buf), "%lld views", (long long)views);
    else if (views < 1000000)
        snprintf(buf, sizeof(buf), "%.1fK views", views / 1000.0);
    else
        snprintf(buf, sizeof(buf), "%.1fM views", views / 1000000.0);
    return buf;
}

std::string format_duration(int64_t seconds)
{
    if (seconds <= 0)
        return "";
    const int64_t h = seconds / 3600;
    const int64_t m = (seconds % 3600) / 60;
    const int64_t s = seconds % 60;
    char buf[32];
    if (h > 0)
        snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", (long long)h, (long long)m, (long long)s);
    else
        snprintf(buf, sizeof(buf), "%lld:%02lld", (long long)m, (long long)s);
    return buf;
}

} /* namespace imtube */
