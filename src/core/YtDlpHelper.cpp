#include "core/YtDlpHelper.h"

#include "nlohmann/json.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sstream>

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace imtube {

namespace {

using json = nlohmann::json;

// Capture a command's stdout into a string. Returns false if the command could
// not be started (exit status ignored here; callers validate the output).
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
        std::string url = t.value("url", "");
        if (url.empty())
            continue;
        // Prefer a "medium" sized thumbnail (>=120, <=400 px wide) if available.
        int score = (width >= 120 && width <= 400) ? 1 : 0;
        if (score >= best_score)
        {
            best_score = score;
            best = url;
        }
    }
    return best;
}

void json_to_video_item(const json& j, VideoItem& v)
{
    v.id = j.value("id", "");
    v.title = j.value("title", "(untitled)");
    v.uploader = j.value("uploader", j.value("channel", ""));
    v.channel_id = j.value("channel_id", j.value("playlist_channel_id", ""));

    const std::string ud = j.value("upload_date", "");
    if (ud.size() == 8)
        v.upload_date = ud.substr(0, 4) + "-" + ud.substr(4, 2) + "-" + ud.substr(6, 2);
    else
        v.upload_date = ud;

    if (j.contains("duration") && j["duration"].is_number())
    {
        v.duration_seconds = j["duration"].get<int64_t>();
        v.duration = format_duration(v.duration_seconds);
    }
    else if (j.contains("duration_string") && j["duration_string"].is_string())
    {
        v.duration = j["duration_string"].get<std::string>();
    }

    if (j.contains("view_count") && j["view_count"].is_number())
    {
        v.view_count = j["view_count"].get<int64_t>();
        v.views = format_view_count(v.view_count);
    }

    const std::string live = j.value("live_status", "not_live");
    v.live = (live == "is_live" || live == "is_upcoming" || live == "post_live");

    v.thumbnail_url = pick_thumbnail_url(j);

    std::string url = j.value("webpage_url", "");
    if (url.empty() && !v.id.empty())
        url = "https://www.youtube.com/watch?v=" + v.id;
    v.url = url;
}

// One line of --dump-json output may itself contain embedded newlines inside
// JSON string values, so split on "\n" between '}' and '{' boundaries.
void split_json_lines(const std::string& text, std::vector<std::string>& lines)
{
    std::string cur;
    for (char c : text)
    {
        if (c == '\r')
            continue; // tolerate CRLF
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

} // namespace

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
            // Ignore non-JSON noise from yt-dlp and keep scanning.
        }
    }
    return false;
}

bool YtDlpHelper::search(const std::string& query, std::vector<VideoItem>& results)
{
    results.clear();

    std::string term = query;
    // A pasted YouTube URL goes through the full extractor instead.
    if (is_youtube_url(term))
    {
        VideoItem item;
        if (search_url(term, item))
            results.push_back(item);
        return !results.empty();
    }

    const std::string search_uri = "ytsearch12:" + term;
    std::string cmd = shell_quote(m_binary_path) +
                      " --no-warnings --flat-playlist --dump-json --socket-timeout 20 "
                      "--extractor-args \"youtubetab:approximate_date\" " +
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
            // Ignore non-JSON noise from yt-dlp and keep scanning.
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
        // Embedded (STM32MP25): pick a single H.264 stream (progressive/combined
        // when available) so playback needs no ffmpeg merge; GStreamer feeds it
        // straight to the VPU hardware decoder.
        char fmt[160];
        snprintf(fmt, sizeof(fmt),
                 "b[height<=%d][vcodec^=avc1]/b[height<=%d]/b/best", max_height, max_height);
        args.push_back("-f");
        args.push_back(fmt);
#else
        // Desktop: prefer a single H.264 (avc1) progressive/merged stream no
        // taller than max_height, falling back to the best available otherwise.
        // H.264 is chosen so the STM32MP25 VPU / GStreamer hardware decoder can
        // be used. Best video + audio are merged with ffmpeg into matroska.
        char fmt[160];
        snprintf(fmt, sizeof(fmt),
                 "bv*[height<=%d][vcodec^=avc1]+ba/b[height<=%d]/b/best", max_height, max_height);
        args.push_back("-f");
        args.push_back(fmt);
        args.push_back("--merge-output-format");
        args.push_back("matroska");
#endif
    }

    args.push_back("-o");
    args.push_back("-");
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
        // Child: stdout -> pipe, stderr -> /dev/null.
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

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

    close(pipefd[1]);
    if (out_pid)
        *out_pid = (int)pid;
    return pipefd[0];
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

} // namespace imtube
