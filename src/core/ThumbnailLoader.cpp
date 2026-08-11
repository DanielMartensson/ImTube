#include "core/ThumbnailLoader.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_PSD
#include "stb_image.h"

#ifndef IMTUBE_WITHOUT_CURL
#include <curl/curl.h>
#endif

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/stat.h>
#include <sys/types.h>

namespace imtube {

namespace {

std::string cache_dir()
{
    const char* home = getenv("HOME");
    if (home == nullptr)
        return "";
    return std::string(home) + "/.cache/imtube/thumbs";
}

void ensure_dir(const std::string& path)
{
    if (path.empty())
        return;
    // Create each path component (e.g. ~/.cache/imtube/thumbs).
    std::string cur;
    const char* p = path.c_str();
    while (*p != '\0')
    {
        if (*p == '/')
        {
            if (!cur.empty() && mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST)
                return;
        }
        cur += *p;
        p++;
    }
    if (!cur.empty())
        mkdir(cur.c_str(), 0755);
}

bool read_file(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr)
        return false;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0)
    {
        fclose(f);
        return false;
    }
    out.resize((size_t)size);
    if (size > 0 && fread(out.data(), 1, (size_t)size, f) != (size_t)size)
    {
        fclose(f);
        out.clear();
        return false;
    }
    fclose(f);
    return true;
}

bool write_file(const std::string& path, const std::vector<uint8_t>& data)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (f == nullptr)
        return false;
    const bool ok = data.empty() || fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

std::string thumbnail_url_for(const VideoItem& item)
{
    if (!item.thumbnail_url.empty())
    {
        // YouTube serves WebP for i.ytimg.com URLs carrying a "?sqp=" query,
        // which our stb_image build cannot decode. Strip the query to get JPEG.
        std::string url = item.thumbnail_url;
        const size_t q = url.find('?');
        if (q != std::string::npos)
            url.resize(q);
        return url;
    }
    if (item.id.empty())
        return "";
    // Fall back to the standard YouTube thumbnail for the id.
    return "https://i.ytimg.com/vi/" + item.id + "/mqdefault.jpg";
}

#ifndef IMTUBE_WITHOUT_CURL
size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* out = static_cast<std::vector<uint8_t>*>(userdata);
    const size_t bytes = size * nmemb;
    out->insert(out->end(), ptr, ptr + bytes);
    return bytes;
}
#endif

} // namespace

ThumbnailLoader::ThumbnailLoader()
    : m_cache_dir(cache_dir())
{
    ensure_dir(m_cache_dir);
#ifndef IMTUBE_WITHOUT_CURL
    curl_global_init(CURL_GLOBAL_DEFAULT); // must happen before worker starts
#endif
    m_running = true;
    m_thread = std::thread(&ThumbnailLoader::worker_loop, this);
}

void ThumbnailLoader::request_thumbnails(const std::vector<VideoItem>& items)
{
    if (!m_running)
        return;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (const VideoItem& item : items)
        {
            if (item.id.empty() || m_known.count(item.id) != 0)
                continue;
            m_known.insert(item.id);
            m_pending.push(item);
        }
    }
    m_cv.notify_one();
}

bool ThumbnailLoader::poll(DecodedThumbnail& out)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_ready.empty())
        return false;
    out = std::move(m_ready.front());
    m_ready.pop();
    return true;
}

void ThumbnailLoader::shutdown()
{
    if (!m_running.exchange(false))
        return;
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
#ifndef IMTUBE_WITHOUT_CURL
    curl_global_cleanup();
#endif
}

void ThumbnailLoader::worker_loop()
{
    while (m_running)
    {
        VideoItem item;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_cv.wait(lk, [this] { return !m_running || !m_pending.empty(); });
            if (!m_running)
                return;
            item = std::move(m_pending.front());
            m_pending.pop();
        }
        load_one(item);
    }
}

void ThumbnailLoader::load_one(const VideoItem& item)
{
    DecodedThumbnail thumb;
    thumb.video_id = item.id;

    const std::string cache_path =
        m_cache_dir.empty() ? "" : m_cache_dir + "/" + item.id + ".jpg";

    auto decode = [&](const std::vector<uint8_t>& bytes) -> bool {
        if (bytes.empty())
            return false;
        int w = 0, h = 0, channels = 0;
        stbi_uc* data = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &channels, 4);
        if (data == nullptr)
            return false;
        thumb.width = w;
        thumb.height = h;
        thumb.rgba.assign(data, data + (size_t)w * h * 4);
        stbi_image_free(data);
        return !thumb.rgba.empty();
    };

    auto download = [&](std::vector<uint8_t>& out) {
        out.clear();
#ifndef IMTUBE_WITHOUT_CURL
        const std::string url = thumbnail_url_for(item);
        if (!url.empty())
        {
            CURL* curl = curl_easy_init();
            if (curl != nullptr)
            {
                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
                curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
                curl_easy_setopt(curl, CURLOPT_USERAGENT, "ImTube/0.1");
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
                const CURLcode res = curl_easy_perform(curl);
                if (res == CURLE_OK && !cache_path.empty() && !out.empty())
                    write_file(cache_path, out);
                curl_easy_cleanup(curl);
            }
        }
#endif
    };

    std::vector<uint8_t> bytes;
    if (cache_path.empty() || !read_file(cache_path, bytes))
        download(bytes);

    // Cache entries may hold WebP data (YouTube served it before we started
    // asking for plain .jpg URLs) which stb_image cannot decode. Drop the file
    // and fetch again rather than showing a grey placeholder.
    if (!decode(bytes))
    {
        if (!cache_path.empty() && !bytes.empty())
        {
            remove(cache_path.c_str());
            bytes.clear();
            download(bytes);
            if (!decode(bytes))
                return;
        }
        else
        {
            return; // download failed / disabled; leave unknown for a retry
        }
    }

    std::lock_guard<std::mutex> lk(m_mutex);
    m_ready.push(std::move(thumb));
}

} // namespace imtube
